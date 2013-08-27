// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Inktank Storage, Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef REPBACKEND_H
#define REPBACKEND_H

#include "OSD.h"
#include "PGBackend.h"
#include "osd_types.h"

class ReplicatedBackend : public PGBackend {
  struct RPGHandle : public PGBackend::RecoveryHandle {
    map<int, vector<PushOp> > pushes;
    map<int, vector<PushReplyOp> > push_replies;
    map<int, vector<PullOp> > pulls;
  };
private:
  bool temp_created;
  coll_t temp_coll;
  coll_t get_temp_coll(ObjectStore::Transaction *t);
  coll_t get_temp_coll() const {
    return temp_coll;
  }
  bool have_temp_coll() const { return temp_created; }

  // Track contents of temp collection, clear on reset
  set<hobject_t> temp_contents;
public:
  coll_t coll;
  OSDService *osd;

  ReplicatedBackend(PGBackend::Listener *pg, coll_t coll, OSDService *osd) :
    PGBackend(pg), temp_created(false), coll(coll), osd(osd) {}

  /// @see PGBackend::open_recovery_op
  PGBackend::RecoveryHandle *open_recovery_op() {
    return new RPGHandle();
  }

  /// @see PGBackend::run_recovery_op
  void run_recovery_op(
    RecoveryPriority prio,
    PGBackend::RecoveryHandle *h) {}

  /// @see PGBackend::recover_object
  void recover_object(
    const hobject_t &hoid,
    const ObjectRecoveryInfo &recovery_info,
    RecoveryHandle *h
    );

  /// @see PGBackend::handle_message
  bool handle_message(
    OpRequestRef op
    );

  void on_change(ObjectStore::Transaction *t);
  void on_flushed();

  void temp_colls(list<coll_t> *out) {
    if (temp_created)
      out->push_back(temp_coll);
  }
  void split_colls(
    pg_t child,
    int split_bits,
    int seed,
    ObjectStore::Transaction *t) {
    if (!temp_created)
      return;
    t->create_collection(temp_coll);
    t->split_collection(
      temp_coll,
      split_bits,
      seed,
      coll_t::make_temp_coll(child));
  }
};

#endif
