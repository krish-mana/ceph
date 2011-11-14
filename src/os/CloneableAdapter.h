// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#ifndef CLONEABLE_LAYER_H
#define CLONEABLE_LAYER_H

#include "boost/scoped_ptr.hpp"
#include "CloneableDB.h"
#include "KeyValueDB.h"

class CloneableAdapter : public CloneableDB {
public:
  /// Retrieve Keys
  int get(
    const string &prefix,        ///< [in] Prefix for key
    const set<string> &key,      ///< [in] Keys to retrieve
    map<string, bufferlist> *out ///< [out] Key value retrieved
    );

  /// Set Keys
  int set(
    const string &prefix,                 ///< [in] Prefix for keys
    const map<string, bufferlist> &to_set ///< [in] keys/values to set
    );

  /// Get Keys by prefix
  int get_keys_by_prefix(
    const string &prefix, ///< [in] Prefix to search for
    int max,              ///< [in] Max entries to return, 0 for no limit
    const string &start,  ///< [in] Start after start, "" for beginning
    set<string> *out      ///< [out] Keys prefixed by prefix
    );

  /// Get keys and values by prefix
  int get_by_prefix(
    const string &prefix, ///< [in] Prefix to search for
    size_t max,           ///< [in] Max size to return, 0 for no limit
    const string &start,  ///< [in] Start after start, "" for beginning
    map<string, bufferlist> *out ///< [out] Keys prefixed by prefix
    );

  /// Removes Keys
  int rmkey(
    const string &prefix,   ///< [in] Prefix for keys
    const set<string> &keys ///< [in] Keys to remove
    );

  /// Removes keys beginning with prefix
  int rmkey_by_prefix(
    const string &prefix ///< [in] Prefix by which to remove keys
    );

  /// Clones keys from one prefix to another
  int clone(
    const string &from_prefix, ///< [in] Source prefix
    const string &to_prefix,   ///< [in] Dest prefix
    );

private:
  boost::scoped_ptr<KeyValueDB> db;
  /// Constructer, pass KeyValueDB
  CloneableAdapter(KeyValueDB *kvdb) : db(kvdb) {}

  int get_prefix_status(const string &prefix, prefix_status *out);
  int set_prefix_status(const string &prefix, const prefix_status &in);

  int _get(
    const string &actual_prefix,
    size_t level,
    const set<string> keys,
    map<string, bufferlist> *out);
}

#endif
