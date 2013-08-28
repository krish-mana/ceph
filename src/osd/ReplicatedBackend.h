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
  void clear_state();
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

  virtual void dump_recovery_info(Formatter *f) const {
  }
private:
  // push
  struct PushInfo {
    ObjectRecoveryProgress recovery_progress;
    ObjectRecoveryInfo recovery_info;
    int priority;

    void dump(Formatter *f) const {
      {
	f->open_object_section("recovery_progress");
	recovery_progress.dump(f);
	f->close_section();
      }
      {
	f->open_object_section("recovery_info");
	recovery_info.dump(f);
	f->close_section();
      }
    }
  };
  map<hobject_t, map<int, PushInfo> > pushing;

  // pull
  struct PullInfo {
    ObjectRecoveryProgress recovery_progress;
    ObjectRecoveryInfo recovery_info;
    int priority;

    void dump(Formatter *f) const {
      {
	f->open_object_section("recovery_progress");
	recovery_progress.dump(f);
	f->close_section();
      }
      {
	f->open_object_section("recovery_info");
	recovery_info.dump(f);
	f->close_section();
      }
    }

    bool is_complete() const {
      return recovery_progress.is_complete(recovery_info);
    }
  };
  map<hobject_t, PullInfo> pulling;

  void sub_op_push(OpRequestRef op);
  void sub_op_push_reply(OpRequestRef op);
  void sub_op_pull(OpRequestRef op);

  void _do_push(OpRequestRef op);
  void _do_pull_response(OpRequestRef op);
  void do_push(OpRequestRef op) {
    if (is_primary()) {
      _do_pull_response(op);
    } else {
      _do_push(op);
    }
  }
  void do_pull(OpRequestRef op);
  void do_push_reply(OpRequestRef op);

  bool handle_push_reply(int peer, PushReplyOp &op, PushOp *reply);
  void handle_pull(int peer, PullOp &op, PushOp *reply);
  bool handle_pull_response(int from, PushOp &op, PullOp *response,
			    ObjectStore::Transaction *t);
  void handle_push(int from, PushOp &op, PushReplyOp *response,
		   ObjectStore::Transaction *t);

  static void trim_pushed_data(const interval_set<uint64_t> &copy_subset,
			       const interval_set<uint64_t> &intervals_received,
			       bufferlist data_received,
			       interval_set<uint64_t> *intervals_usable,
			       bufferlist *data_usable);
  void _failed_push(int from, const hobject_t &soid);

  void send_pushes(int prio, map<int, vector<PushOp> > &pushes);
  int send_push(int priority, int peer,
		const ObjectRecoveryInfo& recovery_info,
		const ObjectRecoveryProgress &progress,
		ObjectRecoveryProgress *out_progress = 0);
  int send_push_op_legacy(int priority, int peer,
			  PushOp &pop);
  int send_pull_legacy(int priority, int peer,
		       const ObjectRecoveryInfo& recovery_info,
		       ObjectRecoveryProgress progress);
  void send_pulls(
    int priority,
    map<int, vector<PullOp> > &pulls);

  int build_push_op(const ObjectRecoveryInfo &recovery_info,
		    const ObjectRecoveryProgress &progress,
		    ObjectRecoveryProgress *out_progress,
		    PushOp *out_op);
  void submit_push_data(ObjectRecoveryInfo &recovery_info,
			bool first,
			bool complete,
			const interval_set<uint64_t> &intervals_included,
			bufferlist data_included,
			bufferlist omap_header,
			map<string, bufferptr> &attrs,
			map<string, bufferlist> &omap_entries,
			ObjectStore::Transaction *t);
  void submit_push_complete(ObjectRecoveryInfo &recovery_info,
			    ObjectStore::Transaction *t);
};

#endif
