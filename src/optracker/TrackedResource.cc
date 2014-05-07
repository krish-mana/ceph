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

#include "TrackedResource.h"

#ifdef HAVE_SYSTEMTAP
#include "optracker_probes.h"
#endif

static tracked_op_t nullop(
  "null",
  "null");

void TrackedResource::log_event(
  TrackedOpRef op,
  const string &evt)
{
#ifdef HAVE_SYSTEMTAP
  if (CEPH_OPTRACKER_RES_EVENT_ENABLED()) {
    JSONFormatter f;
    get_status(&f);
    stringstream ss;
    f.flush(ss);
    string status = ss.str();
    CEPH_OP_TRACKER_RES_EVENT(
      get_res_id(),
      op ? op->get_op_id() : &nullop,
      evt.c_str(),
      status.c_str());
  }
#endif
}
