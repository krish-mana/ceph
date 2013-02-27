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

class SnapMapper {
  typedef uint64_t snapid_t;
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

public:
  SnapMapper(MapCacher::StoreDriver<std::string, bufferlist> *driver)
    : backend(driver) {}

  void update_snaps(
    const hobject_t &oid,       ///< [in] oid to update
    std::set<snapid_t> new_snaps, ///< [in] new snap set
    std::set<snapid_t> old_snaps, ///< [in] old snaps (for debugging)
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
};
WRITE_CLASS_ENCODER(SnapMapper::object_snaps)

#endif
