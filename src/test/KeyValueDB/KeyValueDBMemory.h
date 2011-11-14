// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include <map>

#include "os/KeyValueDB.h"
#include "include/buffer.h"

class KeyValueDBMemory : public KeyValueDB {
  map<string, map<string, bufferlist> > db;

  int get(
    const string &prefix,
    const set<string> &key,
    map<string, bufferlist> *out
    );

  
  int get_keys(
    const string &prefix,   
    const set<string> &key, 
    set<string> *out        
    );
  
  
  int get_keys_by_prefix(
    const string &prefix, 
    size_t max,           
    const string &start,  
    set<string> *out      
    );
  
  
  int get_by_prefix(
    const string &prefix, 
    size_t max,           
    const string &start,  
    map<string, bufferlist> *out 
    );

  
  int set(
    const string &prefix,                 
    const map<string, bufferlist> &to_set 
    );

  
  int rmkeys(
    const string &prefix,   
    const set<string> &keys 
    );

  
  int rmkeys_by_prefix(
    const string &prefix 
    );
}
