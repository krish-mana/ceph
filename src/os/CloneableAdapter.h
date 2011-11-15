// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#ifndef CLONEABLE_LAYER_H
#define CLONEABLE_LAYER_H

#include <string>
#include <set>
#include <map>
#include "boost/scoped_ptr.hpp"
#include "CloneableDB.h"
#include "KeyValueDB.h"

using std::string;

class CloneableAdapter : public CloneableDB {
public:
  /// Retrieve Keys
  virtual int get(
    const string &prefix,        ///< [in] Prefix for key
    const std::set<string> &key,      ///< [in] Key to retrieve
    std::map<string, bufferlist> *out ///< [out] Key value retrieved
    );

  /// Filter keys for existence
  virtual int get_keys(
    const string &prefix,   ///< [in] Prefix for key
    const std::set<string> &key, ///< [in] Keys to check
    std::set<string> *out        ///< [out] Key value retrieved
    );

  /// Get Keys by prefix
  virtual int get_keys_by_prefix(
    const string &prefix, ///< [in] Prefix to search for
    size_t max,           ///< [in] Max entries to return, 0 for no limit
    const string &start,  ///< [in] Start after start, "" for beginning
    std::set<string> *out      ///< [out] Keys prefixed by prefix
    );

  /// Get keys and values by prefix
  virtual int get_by_prefix(
    const string &prefix, ///< [in] Prefix to search for
    size_t max,           ///< [in] Max size to return, 0 for no limit
    const string &start,  ///< [in] Start after start, "" for beginning
    std::map<string, bufferlist> *out ///< [out] Keys prefixed by prefix
    );

  /// Set Keys
  virtual int set(
    const string &prefix,                 ///< [in] Prefix for keys
    const std::map<string, bufferlist> &to_set ///< [in] keys/values to set
    );

  /// Removes Keys
  virtual int rmkeys(
    const string &prefix,   ///< [in] Prefix to search for
    const std::set<string> &keys ///< [in] Keys to remove
    );

  /// Removes keys beginning with prefix
  virtual int rmkeys_by_prefix(
    const string &prefix ///< [in] Prefix by which to remove keys
    );

  /// Clones keys from one prefix to another
  int clone(
    const string &from_prefix, ///< [in] Source prefix
    const string &to_prefix    ///< [in] Dest prefix
    );

  CloneableAdapter(KeyValueDB *kvdb) : db(kvdb) {}
private:
  struct prefix_status;
  boost::scoped_ptr<KeyValueDB> db;
  /// Constructer, pass KeyValueDB

  int get_prefix_status(const string &prefix, prefix_status *out);
  int set_prefix_status(const string &prefix, const prefix_status &in);

  int _get(
    const string &actual_prefix,
    size_t level,
    const std::set<string> keys,
    std::map<string, bufferlist> *out);
};

#endif
