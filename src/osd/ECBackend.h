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
#include "ECUtil.h"
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
  bool can_handle_while_inactive(
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
    ECSubWrite &op,
    Context *on_local_applied_sync = 0
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
    ECSubReadReply &op,
    RecoveryMessages *m
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

  friend struct CallClientContexts;
  void objects_read_async(
    const hobject_t &hoid,
    const list<pair<pair<uint64_t, uint64_t>,
		    pair<bufferlist*, Context*> > > &to_read,
    Context *on_complete);

private:
  friend struct ECRecoveryHandle;
  uint64_t get_recovery_chunk_size() const {
    uint64_t max = cct->_conf->osd_recovery_max_chunk;
    max -= max % sinfo.get_stripe_width();
    max += sinfo.get_stripe_width();
    return max;
  }
  struct RecoveryOp {
    hobject_t hoid;
    eversion_t v;
    set<pg_shard_t> missing_on;
    set<shard_id_t> missing_on_shards;

    ObjectRecoveryInfo recovery_info;
    ObjectRecoveryProgress recovery_progress;

    bool pending_read;
    enum { IDLE, READING, WRITING, COMPLETE } state;

    // must be filled if state == WRITING
    map<shard_id_t, bufferlist> returned_data;
    map<string, bufferlist> xattrs;
    ObjectContextRef obc;
    set<pg_shard_t> waiting_on_pushes;

    // valid in state READING
    pair<uint64_t, uint64_t> extent_requested;

    RecoveryOp() : pending_read(false), state(IDLE) {}
  };
  friend ostream &operator<<(ostream &lhs, const RecoveryOp &rhs);
  map<hobject_t, RecoveryOp> recovery_ops;

public:
  struct read_result_t {
    int r;
    map<pg_shard_t, int> errors;
    boost::optional<map<string, bufferlist> > attrs;
    list<
      boost::tuple<
	uint64_t, uint64_t, map<pg_shard_t, bufferlist> > > returned;
    read_result_t() : r(0) {}
  };
  struct read_request_t {
    const list<pair<uint64_t, uint64_t> > to_read;
    const set<pg_shard_t> need;
    const bool want_attrs;
    GenContext<pair<RecoveryMessages *, read_result_t& > &> *cb;
    read_request_t(
      const hobject_t &hoid,
      const list<pair<uint64_t, uint64_t> > &to_read,
      const set<pg_shard_t> &need,
      bool want_attrs,
      GenContext<pair<RecoveryMessages *, read_result_t& > &> *cb)
      : to_read(to_read), need(need), want_attrs(want_attrs),
	cb(cb) {}
  };
  friend ostream &operator<<(ostream &lhs, const read_request_t &rhs);

  struct ReadOp {
    tid_t tid;
    OpRequestRef op; // may be null if not on behalf of a client

    map<hobject_t, read_request_t> to_read;
    map<hobject_t, read_result_t> complete;

    map<hobject_t, set<pg_shard_t> > obj_to_source;
    map<pg_shard_t, set<hobject_t> > source_to_obj;

    set<pg_shard_t> in_progress;
  };
  friend struct FinishReadOp;
  void filter_read_op(
    const OSDMapRef osdmap,
    ReadOp &op);
  void complete_read_op(ReadOp &rop, RecoveryMessages *m);
  friend ostream &operator<<(ostream &lhs, const ReadOp &rhs);
  map<tid_t, ReadOp> tid_to_read_map;
  map<pg_shard_t, set<tid_t> > shard_to_read_map;
  void start_read_op(
    map<hobject_t, read_request_t> &to_read,
    OpRequestRef op);

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
  friend ostream &operator<<(ostream &lhs, const Op &rhs);

  void continue_recovery_op(
    RecoveryOp &op,
    RecoveryMessages *m);

  void dispatch_recovery_messages(RecoveryMessages &m);
  friend struct OnRecoveryReadComplete;
  void handle_recovery_read_cancel(
    const hobject_t &hoid);
  void handle_recovery_read_complete(
    const hobject_t &hoid,
    boost::tuple<uint64_t, uint64_t, map<pg_shard_t, bufferlist> > &to_read,
    boost::optional<map<string, bufferlist> > attrs,
    RecoveryMessages *m);
  void handle_recovery_push(
    PushOp &op,
    RecoveryMessages *m);
  void handle_recovery_push_reply(
    PushReplyOp &op,
    pg_shard_t from,
    RecoveryMessages *m);

  set<hobject_t> unstable;

  map<tid_t, Op> tid_to_op_map; /// lists below point into here
  list<Op*> writing;

  CephContext *cct;
  ErasureCodeInterfaceRef ec_impl;

  class ECRecPred : public IsRecoverablePredicate {
    set<int> want;
    ErasureCodeInterfaceRef ec_impl;
  public:
    ECRecPred(ErasureCodeInterfaceRef ec_impl) : ec_impl(ec_impl) {
      for (unsigned i = 0; i < ec_impl->get_data_chunk_count(); ++i) {
	want.insert(i);
      }
    }
    bool operator()(const set<pg_shard_t> &_have) const {
      set<int> have;
      for (set<pg_shard_t>::const_iterator i = _have.begin();
	   i != _have.end();
	   ++i) {
	have.insert(i->shard);
      }
      set<int> min;
      return ec_impl->minimum_to_decode(want, have, &min) == 0;
    }
  };
  IsRecoverablePredicate *get_is_recoverable_predicate() {
    return new ECRecPred(ec_impl);
  }

  class ECReadPred : public IsReadablePredicate {
    pg_shard_t whoami;
    ECRecPred rec_pred;
  public:
    ECReadPred(
      pg_shard_t whoami,
      ErasureCodeInterfaceRef ec_impl) : whoami(whoami), rec_pred(ec_impl) {}
    bool operator()(const set<pg_shard_t> &_have) const {
      return _have.count(whoami) && rec_pred(_have);
    }
  };
  IsReadablePredicate *get_is_readable_predicate() {
    return new ECReadPred(get_parent()->whoami_shard(), ec_impl);
  }


  const ECUtil::stripe_info_t sinfo;

  friend struct ReadCB;
  void check_op(Op *op);
  void start_write(Op *op);
public:
  ECBackend(
    PGBackend::Listener *pg,
    coll_t coll,
    coll_t temp_coll,
    ObjectStore *store,
    CephContext *cct,
    ErasureCodeInterfaceRef ec_impl)
    : PGBackend(pg, store, coll, temp_coll),
      cct(cct),
      ec_impl(ec_impl),
      stripe_width(ec_impl->get_chunk_count()),
      stripe_size(4*(2<<10) /* TODO: make more flexible */) {}

  int get_min_avail_to_read_shards(
    const hobject_t &hoid,
    const set<int> &want,
    bool for_recovery,
    set<pg_shard_t> *to_read);

  void rollback_append(
    const hobject_t &hoid,
    uint64_t old_size,
    ObjectStore::Transaction *t);

  struct ECContext {
    list<pair<pg_shard_t, Message*> > to_send;
    list<Context*> to_run;
  };
};

#endif
