// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 Red Hat
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_TRACKED_MUTEX_H
#define CEPH_TRACKED_MUTEX_H

#include "TrackedResource.h"
#include "common/Formatter.h"
#include "common/Mutex.h"

class TrackedMutex : public TrackedResource {
  const string class_id;
  const string inst_id;

  const tracked_res_t res_id; 

  Mutex lock;
public:
  TrackedMutex(
    const string &_class_id,
    const string &_inst_id,
    bool r=false, bool ld=true, bool bt=false,
    CephContext *cct=0)
    : class_id(_class_id),
      inst_id(_inst_id),
      res_id("mutex", class_id.c_str(), inst_id.c_str()),
      lock(string(class_id + "/" + inst_id).c_str(), r, ld, bt, cct) {}

  bool is_locked() const { return lock.is_locked(); }
  bool is_locked_by_me() const { return lock.is_locked_by_me(); }
  void Lock(
    TrackedOpRef op,
    bool no_lockdep=false);
  void Unlock(
    TrackedOpRef op);

  void get_status(Formatter *f) const {}

  const tracked_res_t *get_res_id() const {
    return &res_id;
  }

  class Locker {
    TrackedOpRef &op;
    TrackedMutex &mutex;
  public:
    Locker(TrackedOpRef &op, TrackedMutex& m) : op(op), mutex(m) {
      mutex.Lock(op);
    }
    ~Locker() {
      mutex.Unlock(op);
    }
  };
};

#endif
