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

#ifndef CEPH_TRACKED_RESOURCE_H
#define CEPH_TRACKED_RESOURCE_H

#include "TrackedOp.h"
#include "common/Formatter.h"

struct tracked_res_t {
  const char *type_id;
  const char *class_id;
  const char *inst_id;
  tracked_res_t(
    const char *ti,
    const char *ci,
    const char *ii)
    : type_id(ti), class_id(ci), inst_id(ii) {}
};

class TrackedResource {
public:
  void get_status(Formatter *f) const = 0;
  const tracked_res_t *get_res_id() const = 0;

  void log_event(TrackedOpRef op, const string &evt);
};

#endif
