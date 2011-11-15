// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "include/encoding.h"
#include "KeyValueDBMemory.h"
#include <map>
#include <set>

using namespace std;

int KeyValueDBMemory::get(const string &prefix,
			  const std::set<string> &key,
			  map<string, bufferlist> *out) {
  if (!db.count(prefix))
    return 0;

  for (std::set<string>::const_iterator i = key.begin();
       i != key.end();
       ++i) {
    if (db[prefix].count(*i))
      (*out)[*i] = db[prefix][*i];
  }
  return 0;
}

int KeyValueDBMemory::get_keys(const string &prefix,
			       const std::set<string> &key,
			       std::set<string> *out) {
  if (!db.count(prefix))
    return 0;

  for (std::set<string>::const_iterator i = key.begin();
       i != key.end();
       ++i) {
    if (db[prefix].count(*i))
      out->insert(*i);
  }
  return 0;
}

int KeyValueDBMemory::set(const string &prefix,
			  const map<string, bufferlist> &to_set) {
  db[prefix].insert(to_set.begin(), to_set.end());
  return 0;
}

int KeyValueDBMemory::rmkeys(const string &prefix,
			     const std::set<string> &keys) {
  if (!db.count(prefix))
    return 0;
  for (std::set<string>::const_iterator i = keys.begin();
       i != keys.end();
       ++i) {
    db[prefix].erase(*i);
  }
  return 0;
}

int KeyValueDBMemory::rmkeys_by_prefix(const string &prefix) {
  db.erase(prefix);
  return 0;
}
  
