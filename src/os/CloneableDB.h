// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#ifndef CLONEABLE_DB_H 
#define CLONEABLE_DB_H 

#include "KeyValueDB.h"
/**
 * Defines interface for a cloneable key value backend
 */
class CloneableDB : public KeyValueDB {
 public:
  /// Clones keys from one prefix to another
  virtual int clone(
    const string &from_prefix, ///< [in] Source prefix
    const string &to_prefix    ///< [in] Dest prefix
    ) = 0;

  virtual ~CloneableDB() {};
};
#endif
