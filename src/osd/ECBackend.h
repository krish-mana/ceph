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

#ifndef ECBACKEND_H
#define ECBACKEND_H

#include "OSD.h"
#include "PGBackend.h"
#include "osd_types.h"
#include <boost/optional.hpp>
#include "ErasureCodeInterface.h"
#include "ECTransaction.h"
#include "ECMsgTypes.h"
#include "messages/MOSDECSubOpWrite.h"
#include "messages/MOSDECSubOpWriteReply.h"
#include "messages/MOSDECSubOpRead.h"
#include "messages/MOSDECSubOpReadReply.h"

struct RecoveryMessages;
class ECBackend : public PGBackend {
public:
  RecoveryHandle *open_recovery_op();

  void run_recovery_op(
    RecoveryHandle *h,
    int priority
    );

  void recover_object(
    const hobject_t &hoid,
    eversion_t v,
    ObjectContextRef head,
    ObjectContextRef obc,
    RecoveryHandle *h
    );

  bool handle_message(
    OpRequestRef op
    );
  friend struct SubWriteApplied;
  friend struct SubWriteCommitted;
  void sub_write_applied(
    tid_t tid, eversion_t version);
  void sub_write_committed(
    tid_t tid, eversion_t version);
  void handle_sub_write(
    pg_shard_t from,
    OpRequestRef msg,
    ECSubWrite &op
    );
  void handle_sub_read(
    pg_shard_t from,
    ECSubRead &op,
    ECSubReadReply *reply
    );
  void handle_sub_write_reply(
    pg_shard_t from,
    ECSubWriteReply &op
    );
  void handle_sub_read_reply(
    pg_shard_t from,
    ECSubReadReply &op
    );

  void check_recovery_sources(const OSDMapRef osdmap);

  void _on_change(ObjectStore::Transaction *t);
  void clear_state();

  void on_flushed();

  void dump_recovery_info(Formatter *f) const;

  PGTransaction *get_transaction();

  void submit_transaction(
    const hobject_t &hoid,
    const eversion_t &at_version,
    PGTransaction *t,
    const eversion_t &trim_to,
    vector<pg_log_entry_t> &log_entries,
    Context *on_local_applied_sync,
    Context *on_all_applied,
    Context *on_all_commit,
    tid_t tid,
    osd_reqid_t reqid,
    OpRequestRef op
    );

  int objects_read_sync(
    const hobject_t &hoid,
    uint64_t off,
    uint64_t len,
    bufferlist *bl);

  void objects_read_async(
    const hobject_t &hoid,
    const list<pair<pair<uint64_t, uint64_t>,
		    pair<bufferlist*, Context*> > > &to_read,
    Context *on_complete);

private:
  friend struct ECRecoveryHandle;
  struct RecoveryOp {
    hobject_t hoid;
    eversion_t v;
    ObjectContextRef obc;
    map<string, bufferlist> xattrs;
    
    ObjectRecoveryInfo recovery_info;
    ObjectRecoveryProgress recovery_progress;
  };
  struct ReadOp {
    tid_t tid;
    list<
      pair<
	hobject_t,
	boost::tuple<uint64_t, uint64_t, bufferlist*>
	>
      > to_read;
    map<
      pg_shard_t,
      list<
	pair<
	  hobject_t,
	  pair<uint64_t, bufferlist>
	  >
	>
      > complete;
    map<hobject_t, map<string, bufferlist> *> attrs_to_read;
    set<pg_shard_t> in_progress;
    Context *on_complete;
  };
  map<tid_t, ReadOp> tid_to_read_map;
  void start_read_op(
    tid_t tid,
    const list<
      pair<
	hobject_t,
	boost::tuple<uint64_t, uint64_t, bufferlist*>
	>
      > &to_read,
    const map<hobject_t, map<string, bufferlist> *> &attrs_to_read,
    Context *c);

  struct Op {
    hobject_t hoid;
    eversion_t version;
    eversion_t trim_to;
    vector<pg_log_entry_t> log_entries;
    Context *on_local_applied_sync;
    Context *on_all_applied;
    Context *on_all_commit;
    tid_t tid;
    osd_reqid_t reqid;
    OpRequestRef client_op;

    ECTransaction *t;

    map<hobject_t, uint64_t> must_read;
    set<hobject_t> writes;

    map<hobject_t, pair<uint64_t, bufferlist> > reads_completed;

    set<hobject_t> temp_added;
    set<hobject_t> temp_cleared;

    set<pg_shard_t> pending_commit;
    set<pg_shard_t> pending_apply;
    ~Op() {
      delete t;
      delete on_local_applied_sync;
      delete on_all_applied;
      delete on_all_commit;
    }
  };

  void dispatch_recovery_messages(RecoveryMessages *m);
  ObjectStore *store;
  set<hobject_t> unstable;

  map<tid_t, Op> tid_to_op_map; /// lists below point into here
  list<Op*> waiting;
  list<Op*> reading;
  list<Op*> writing;

  CephContext *cct;
  ErasureCodeInterfaceRef ec_impl;
  const uint64_t stripe_width;
  const uint64_t stripe_size;
  set<pg_shard_t> actingbackfill;
  set<pg_shard_t> min_to_read;

  void call_commit_apply_cbs();
  void check_pending_ops();
  bool can_read(Op *op);
  friend struct ReadCB;
  uint64_t round_prev_stripe_boundary(uint64_t from) const {
    return (from / stripe_width) * stripe_size;
  }
  uint64_t round_next_stripe_boundary(uint64_t to) const {
    return ((to + stripe_width - 1) / stripe_width) * stripe_size;
  }
  void start_read(Op *op);
  void start_write(Op *op);
public:
  ECBackend(
    CephContext *cct,
    ObjectStore *store,
    coll_t coll,
    coll_t temp_coll,
    PGBackend::Listener *pg,
    ErasureCodeInterfaceRef ec_impl,
    uint64_t stripe_width,
    uint64_t stripe_size)
    : PGBackend(pg, store, coll, temp_coll),
      cct(cct),
      ec_impl(ec_impl), stripe_width(stripe_width),
      stripe_size(stripe_size) {}
  void go_active(
    const set<pg_shard_t> &_acting,
    const set<pg_shard_t> &_backfill) {
    actingbackfill = _acting;
    actingbackfill.insert(_backfill.begin(), _backfill.end());
    set<int> want;
    for (int i = 0; i < 0/*ec_impl->get_data_chunk_count()*/; ++i)
      want.insert(i);

    set<int> have;
    for (set<pg_shard_t>::iterator i = _acting.begin();
	 i != _acting.end();
	 ++i) {
      have.insert(i->shard);
    }
    set<int> _min_to_read;
    ec_impl->minimum_to_decode(want, have, &_min_to_read);
    for (set<pg_shard_t>::iterator i = _acting.begin();
	 i != _acting.end();
	 ++i) {
      if (_min_to_read.count(i->shard))
	min_to_read.insert(*i);
    }
  }
  void clear() {
    actingbackfill.clear();
    min_to_read.clear();
  }

  struct ECContext {
    list<pair<pg_shard_t, Message*> > to_send;
    list<Context*> to_run;
  };
};

#endif
