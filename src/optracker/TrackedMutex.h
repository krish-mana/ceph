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

#ifdef CEPH_TRACKED_MUTEX_H
#define CEPH_TRACKED_MUTEX_H

#include "common/Mutex.h"

#ifdef ENABLE_SYSTEMTAP
#include "optracker_probes.h"
#endif

class TrackedMutex {
  Mutex lock;
public:
  TrackedMutex(
    const char *n, bool r=false, bool ld=true, bool bt=false,
    CephContext *cct=0)
    : lock(n, r, ld, bt, cct) {}

  bool is_locked() const { return lock.is_locked(); }
  bool is_locked_by_me() const { return lock.is_locked_by_me(); }
  void Lock(bool no_lockdep=false) { return lock.lock(no_lockdep); }
  void Unlock() { return lock.Unlock(); }

  class Locker {
    TrackedMutex &mutex;
  public:
    Locker(Mutex& m) : mutex(m) {
      mutex.Lock();
    }
    ~Locker() {
      mutex.Unlock();
    }
  };
};

#endif
