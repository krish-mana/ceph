// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "KeyValueDBMemory.h"
#include <map>

int KeyValueDBMemory::get(const string &prefix,
			  const set<string> &key,
			  map<string, bufferlist> *out) {
  if (!db.count(prefix))
    return 0;

  for (set<string>::const_iterator i = key.begin();
       i != key.end();
       ++i) {
    if (db[prefix].contains(*i))
      (*out)[*i] = db[prefix][*i];
  }
  return 0;
}

int KeyValueDBMemory::get_keys(const string &prefix,
			       const set<string> &key,
			       set<string> *out) {
  if (!db.count(prefix))
    return 0;

  for (set<string>::const_iterator i = key.begin();
       i != key.end();
       ++i) {
    if (db[prefix].contains(*i))
      out->insert(*i);
  }
  return 0;
}

int KeyValueDBMemory::get_keys_by_prefix(const string &prefix,
					 size_t max,
					 const set<string> &start,
					 set<string> *out) {
  if (!db.count(prefix))
    return 0;

  map<string, bufferlist> &pmap = db[prefix];
  map<string, bufferlist>::iterator i = pmap.lower_bound(prefix);
  size_t copied = 0;
  while (i != pmap.end() && (!max || copied < max)) {
    out->insert(i->first);
    ++copied;
    ++i;
  }
  return 0;
}
  
int KeyValueDBMemory::get_by_prefix(const string &prefix,
				    size_t max,
				    const set<string> &start,
				    map<string, bufferlist> *out) {
  if (!db.count(prefix))
    return 0;
  map<string, bufferlist> &pmap = db[prefix];
  map<string, bufferlist>::iterator i = pmap.lower_bound(prefix);
  size_t copied = 0;
  while (i != pmap.end() && (!max || copied < max)) {
    copied += i->first.size() + i->second.size();
    (*out)[i->first] = i->second;
    ++i;
  }
  if (max && copied > max)
    out->erase((--i)->fist);
  return 0;
}

int KeyValueDBMemory::set(const string &prefix,
			  const map<string, bufferlist> &to_set) {
  db[prefix].insert(to_set.begin(), to_set.end());
  return 0;
}

int KeyValueDBMemory::rmkeys(const string &prefix,
			     const set<string> &keys) {
  if (!db.count(prefix))
    return 0;
  for (set<string>::const_iterator i = keys.begin();
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
  
