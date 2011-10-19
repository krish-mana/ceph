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

#ifndef OS_KEYVALUESTORE_H
#define OS_KEYVALUESTORE_H

#include <string>
#include <vector>
#include <tr1/memory>

#include "CollectionIndex.h"

/**
 * Encapsulates the FileStore key value store
 * 
 * Implementations of this interface will be used to implement TMAP
 */
class KeyValueStore {
public:
  /// Set keys and values from specified map
  virtual int set_keys(
    const hobject_t &hoid,              ///< [in] object containing tmap
    CollectionIndex::IndexedPath path,  ///< [in] Path to hoid
    const map<string, bufferptr> &set   ///< [in] key to value map to set
    ) = 0;

  /// Set header
  virtual int set_header(
    const hobject_t &hoid,              ///< [in] object containing tmap
    CollectionIndex::IndexedPath path,  ///< [in] Path to hoid
    const bufferlist &bl                ///< [in] header to set
    ) = 0;

  /// Retrieve header
  virtual int get_header(
    const hobject_t &hoid,              ///< [in] object containing tmap
    CollectionIndex::IndexedPath path,  ///< [in] Path to hoid
    bufferlist *bl                      ///< [out] header to set
    ) = 0;

  /// Clear all tmap keys and values from hoid
  virtual int clear(
    const hobject_t &hoid,              ///< [in] object containing tmap
    CollectionIndex::IndexedPath path   ///< [in] Path to hoid
    ) = 0;

  /// Clear all tmap keys and values from hoid
  virtual int rm_keys(
    const hobject_t &hoid,              ///< [in] object containing tmap
    CollectionIndex::IndexedPath path,  ///< [in] Path to hoid
    const set<string> &to_clear         ///< [in] Keys to clear
    ) = 0;

  /// Get all keys and values
  virtual int get(
    const hobject_t &hoid,             ///< [in] object containing tmap
    CollectionIndex::IndexedPath path, ///< [in] Path to hoid
    bufferlist *header,                ///< [out] Returned Header
    map<string, bufferlist> *out       ///< [out] Returned keys and values
    ) = 0;

  /// Get values for supplied keys
  virtual int get_keys(
    const hobject_t &hoid,             ///< [in] object containing tmap
    CollectionIndex::IndexedPath path, ///< [in] Path to hoid
    set<string> *keys                  ///< [out] Keys defined on hoid
    ) = 0;

  /// Get values for supplied keys
  virtual int get_values(
    const hobject_t &hoid,             ///< [in] object containing tmap
    CollectionIndex::IndexedPath path, ///< [in] Path to hoid
    const set<string> &keys,           ///< [in] Keys to get
    map<string, bufferlist> *out       ///< [out] Returned keys and values
    ) = 0;

  /// Check key existence
  virtual int check_keys(
    const hobject_t &hoid,             ///< [in] object containing tmap
    CollectionIndex::IndexedPath path, ///< [in] Path to hoid
    const set<string> &keys,           ///< [in] Keys to check
    set<string> *out                   ///< [out] Subset of keys defined on hoid
    ) = 0;

  virtual ~KeyValueStore() {}
};

#endif
