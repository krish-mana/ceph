// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef SNAPMAPPER_H
#define SNAPMAPPER_H

#include <string>
#include <set>
#include <utility>
#include <string.h>

#include "common/map_cacher.hpp"
#include "os/hobject.h"
#include "include/buffer.h"
#include "include/encoding.h"
#include "include/object.h"
#include "os/ObjectStore.h"

class OSDriver : public MapCacher::StoreDriver<std::string, bufferlist> {
  ObjectStore *os;
  coll_t cid;
  hobject_t hoid;

public:
  class OSTransaction : public MapCacher::Transaction<std::string, bufferlist> {
    friend class OSDriver;
    coll_t cid;
    hobject_t hoid;
    ObjectStore::Transaction *t;
    OSTransaction(
      coll_t cid,
      const hobject_t &hoid,
      ObjectStore::Transaction *t)
      : cid(cid), hoid(hoid), t(t) {}
  public:
    void set_keys(
      const std::map<std::string, bufferlist> &to_set) {
      t->omap_setkeys(cid, hoid, to_set);
    }
    void remove_keys(
      const std::set<std::string> &to_remove) {
      t->omap_rmkeys(cid, hoid, to_remove);
    }
    void add_callback(
      Context *c) {
      t->register_on_applied(c);
    }
  };

  OSTransaction get_transaction(
    ObjectStore::Transaction *t) {
    return OSTransaction(cid, hoid, t);
  }

  OSDriver(ObjectStore *os, coll_t cid, const hobject_t &hoid) :
    os(os), cid(cid), hoid(hoid) {}
  int get_keys(
    const std::set<std::string> &keys,
    std::map<std::string, bufferlist> *out);
  int get_next(
    const std::string &key,
    pair<std::string, bufferlist> *next);
};

class SnapMapper {
public:
  struct object_snaps {
    hobject_t oid;
    std::set<snapid_t> snaps;
    object_snaps(hobject_t oid, const std::set<snapid_t> &snaps)
      : oid(oid), snaps(snaps) {}
    object_snaps() {}
    void encode(bufferlist &bl) const;
    void decode(bufferlist::iterator &bp);
  };

private:
  MapCacher::MapCacher<std::string, bufferlist> backend;

  static const std::string MAPPING_PREFIX;
  static const std::string OBJECT_PREFIX;

  static std::string get_prefix(snapid_t snap);

  static std::string to_raw_key(
    const std::pair<snapid_t, hobject_t> &to_map);

  static std::pair<std::string, bufferlist> to_raw(
    const std::pair<snapid_t, hobject_t> &to_map);

  static bool is_mapping(const std::string &to_test);

  std::pair<snapid_t, hobject_t> from_raw(
    const std::pair<std::string, bufferlist> &image);

  std::string to_object_key(const hobject_t &hoid);

  int get_snaps(const hobject_t &oid, object_snaps *out);

  void set_snaps(
    const hobject_t &oid,
    const object_snaps &out,
    MapCacher::Transaction<std::string, bufferlist> *t);

  void clear_snaps(
    const hobject_t &oid,
    MapCacher::Transaction<std::string, bufferlist> *t);

  // True if hoid has not yet been split out
  bool check(const hobject_t &hoid) const {
    return hoid.match(mask_bits, match);
  }

  int _remove_oid(
    const hobject_t &oid,    ///< [in] oid to remove
    MapCacher::Transaction<std::string, bufferlist> *t ///< [out] transaction
    );

public:
  uint32_t mask_bits;
  uint32_t match;
  string last_key_checked;
  enum {
    NOTSPLITTING,
    SPLITTING
  } split_state;
  SnapMapper(
    MapCacher::StoreDriver<std::string, bufferlist> *driver,
    uint32_t match,
    uint32_t bits)
    : backend(driver), mask_bits(bits), match(match),
      split_state(NOTSPLITTING) {
    update_bits(mask_bits);
  }

  set<string> prefixes;
  void update_bits(uint32_t new_bits) {
    assert(new_bits >= mask_bits);
    mask_bits = new_bits;
    prefixes = hobject_t::get_prefixes(
      mask_bits,
      match);
  }

  int update_snaps(
    const hobject_t &oid,       ///< [in] oid to update
    const std::set<snapid_t> &new_snaps, ///< [in] new snap set
    const std::set<snapid_t> *old_snaps, ///< [in] old snaps (for debugging)
    MapCacher::Transaction<std::string, bufferlist> *t ///< [out] transaction
    );

  void add_oid(
    const hobject_t &oid,       ///< [in] oid to add
    std::set<snapid_t> new_snaps, ///< [in] snaps
    MapCacher::Transaction<std::string, bufferlist> *t ///< [out] transaction
    );

  int get_next_object_to_trim(
    snapid_t snap,              ///< [in] snap to check
    hobject_t *hoid             ///< [out] next hoid to trim
    );  ///< @return error, -ENOENT if no more snaps

  int remove_oid(
    const hobject_t &oid,    ///< [in] oid to remove
    MapCacher::Transaction<std::string, bufferlist> *t ///< [out] transaction
    );

  int get_snaps(
    const hobject_t &oid,     ///< [in] oid to get snaps for
    std::set<snapid_t> *snaps ///< [out] snaps
    ); ///< @return error, -ENOENT if oid is not recorded
};
WRITE_CLASS_ENCODER(SnapMapper::object_snaps)

#endif
