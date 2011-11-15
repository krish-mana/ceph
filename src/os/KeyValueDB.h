// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#ifndef KEY_VALUE_DB_H
#define KEY_VALUE_DB_H

#include <set>
#include <map>
#include <string>

using std::string;
/**
 * Defines virtual interface to be implemented by key value store
 *
 * Kyoto Cabinet or LevelDB should implement this
 */
class KeyValueDB {
public:
  /// Retrieve Keys
  virtual int get(
    const string &prefix,        ///< [in] Prefix for key
    const std::set<string> &key,      ///< [in] Key to retrieve
    std::map<string, bufferlist> *out ///< [out] Key value retrieved
    ) = 0;

  /// Filter keys for existence
  virtual int get_keys(
    const string &prefix,   ///< [in] Prefix for key
    const std::set<string> &key, ///< [in] Keys to check
    std::set<string> *out        ///< [out] Key value retrieved
    ) = 0;

  /// Get Keys by prefix
  virtual int get_keys_by_prefix(
    const string &prefix, ///< [in] Prefix to search for
    size_t max,           ///< [in] Max entries to return, 0 for no limit
    const string &start,  ///< [in] Start after start, "" for beginning
    std::set<string> *out      ///< [out] Keys prefixed by prefix
    ) = 0;

  /// Get keys and values by prefix
  virtual int get_by_prefix(
    const string &prefix, ///< [in] Prefix to search for
    size_t max,           ///< [in] Max size to return, 0 for no limit
    const string &start,  ///< [in] Start after start, "" for beginning
    std::map<string, bufferlist> *out ///< [out] Keys prefixed by prefix
    ) = 0;

  /// Set Keys
  virtual int set(
    const string &prefix,                 ///< [in] Prefix for keys
    const std::map<string, bufferlist> &to_set ///< [in] keys/values to set
    ) = 0;

  /// Removes Keys
  virtual int rmkeys(
    const string &prefix,   ///< [in] Prefix to search for
    const std::set<string> &keys ///< [in] Keys to remove
    ) = 0;

  /// Removes keys beginning with prefix
  virtual int rmkeys_by_prefix(
    const string &prefix ///< [in] Prefix by which to remove keys
    ) = 0;

  virtual ~KeyValueDB();
};

#endif
