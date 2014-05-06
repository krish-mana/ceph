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

#include "TrackedMutex.h"

#ifdef ENABLE_SYSTEMTAP
#include "optracker_probes.h"
#endif

void TrackedMutex::Unlock(
  TrackedOpRef op) {
  return lock.Unlock();
}

void TrackedMutex::Lock(
  TrackedOpRef op,
  bool no_lockdep) {
  return lock.Lock(no_lockdep);
}
