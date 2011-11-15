// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#ifndef CLONEABLE_LAYER_H
#define CLONEABLE_LAYER_H

#include <string>
#include <set>
#include <map>
#include "boost/scoped_ptr.hpp"
#include "CloneableDB.h"
#include "KeyValueDB.h"
#include <tr1/memory>

using std::string;

class CloneableAdapter : public CloneableDB {
public:
  /// Retrieve Keys
  int get(
    const string &prefix,        ///< [in] Prefix for key
    const std::set<string> &key,      ///< [in] Key to retrieve
    std::map<string, bufferlist> *out ///< [out] Key value retrieved
    );

  /// Filter keys for existence
  int get_keys(
    const string &prefix,   ///< [in] Prefix for key
    const std::set<string> &key, ///< [in] Keys to check
    std::set<string> *out        ///< [out] Key value retrieved
    );

  /// Set Keys
  int set(
    const string &prefix,                 ///< [in] Prefix for keys
    const std::map<string, bufferlist> &to_set ///< [in] keys/values to set
    );

  /// Removes Keys
  int rmkeys(
    const string &prefix,   ///< [in] Prefix to search for
    const std::set<string> &keys ///< [in] Keys to remove
    );

  /// Removes keys beginning with prefix
  int rmkeys_by_prefix(
    const string &prefix ///< [in] Prefix by which to remove keys
    );

  /// Clones keys from one prefix to another
  int clone(
    const string &from_prefix, ///< [in] Source prefix
    const string &to_prefix    ///< [in] Dest prefix
    );

  Iterator get_iterator(const string &prefix) { return std::tr1::shared_ptr<IteratorInterface>(); }

  CloneableAdapter(KeyValueDB *kvdb) : db(kvdb) {}
private:
  struct prefix_status;
  boost::scoped_ptr<KeyValueDB> db;
  /// Constructer, pass KeyValueDB

  int _get(
    const string &actual_prefix,
    size_t level,
    const std::set<string> keys,
    std::map<string, bufferlist> *out);

  // Helpers
  int get_prefix_status(const string &prefix,
			prefix_status *out);
  int set_prefix_status(const string &prefix,
			const prefix_status &in);
  int _get(const string &prefix,
	   const std::set<string> &keys,
	   std::map<string, bufferlist> *out);
  int _get_keys(const string &prefix,
		const std::set<string> &keys,
		std::set<string> *out);
};

#endif
