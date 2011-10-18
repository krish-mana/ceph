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

#ifndef OS_TMAP_H
#define OS_TMAP_H

#include <string>
#include <vector>
#include <tr1/memory>

#include "KeyValueStore.h"
#include "CollectionIndex.h"

/// Original ReplicatedPG functionality
class TMap : public KeyValueStore {
public:
  /// Set keys and values from specified map
  int set_keys(
    const hobject_t &hoid,              ///< [in] object containing tmap
    CollectionIndex::IndexedPath path,  ///< [in] Path to hoid
    const map<string, bufferptr> &set   ///< [in] key to value map to set
    );

  /// Clear all tmap keys and values from hoid
  int clear(
    const hobject_t &hoid,              ///< [in] object containing tmap
    CollectionIndex::IndexedPath path   ///< [in] Path to hoid
    );

  /// Clear all tmap keys and values from hoid
  int rm_keys(
    const hobject_t &hoid,              ///< [in] object containing tmap
    CollectionIndex::IndexedPath path,  ///< [in] Path to hoid
    const set<string> &to_clear         ///< [in] Keys to clear
    );

  /// Get all keys and values
  int get(
    const hobject_t &hoid,             ///< [in] object containing tmap
    CollectionIndex::IndexedPath path, ///< [in] Path to hoid
    map<string, bufferlist> *out       ///< [out] Returned keys and values
    );

  /// Get values for supplied keys
  int get_keys(
    const hobject_t &hoid,             ///< [in] object containing tmap
    CollectionIndex::IndexedPath path, ///< [in] Path to hoid
    set<string> *keys                  ///< [out] Keys defined on hoid
    );

  /// Get values for supplied keys
  int get_values(
    const hobject_t &hoid,             ///< [in] object containing tmap
    CollectionIndex::IndexedPath path, ///< [in] Path to hoid
    const set<string> &keys,           ///< [in] Keys to get
    map<string, bufferlist> *out       ///< [out] Returned keys and values
    );

  /// Check key existence
  int check_keys(
    const hobject_t &hoid,             ///< [in] object containing tmap
    CollectionIndex::IndexedPath path, ///< [in] Path to hoid
    const set<string> &keys,           ///< [in] Keys to check
    set<string> *out                   ///< [out] Subset of keys defined on hoid
    );
};

#endif
