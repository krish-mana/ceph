// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#include <map>
#include <set>
#include <string>

#include "os/KeyValueDB.h"
#include "include/buffer.h"

using std::string;

class KeyValueDBMemory : public KeyValueDB {
  std::map<string, std::map<string, bufferlist> > db;

  int get(
    const string &prefix,
    const std::set<string> &key,
    std::map<string, bufferlist> *out
    );

  int get_keys(
    const string &prefix,
    const std::set<string> &key,
    std::set<string> *out
    );

  int set(
    const string &prefix,
    const std::map<string, bufferlist> &to_set
    );

  int rmkeys(
    const string &prefix,
    const std::set<string> &keys
    );

  int rmkeys_by_prefix(
    const string &prefix
    );

  friend class MemIterator;
  Iterator get_iterator(const string &prefix);
};
