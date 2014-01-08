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

#include <boost/variant.hpp>
#include <boost/optional.hpp>
#include <iostream>
#include <sstream>

#include "ECUtil.h"
#include "ECBackend.h"
#include "messages/MOSDPGPush.h"
#include "messages/MOSDPGPushReply.h"

#define dout_subsys ceph_subsys_osd
#define DOUT_PREFIX_ARGS this
#undef dout_prefix
#define dout_prefix _prefix(_dout, this)
static ostream& _prefix(std::ostream *_dout, ECBackend *pgb) {
  return *_dout << pgb->get_parent()->gen_dbg_prefix();
}

struct ECRecoveryHandle : public PGBackend::RecoveryHandle {
  list<ECBackend::RecoveryOp> ops;
};

PGBackend::RecoveryHandle *open_recovery_op()
{
  return new ECRecoveryHandle;
}

struct RecoveryMessages {
  map<hobject_t, list<boost::tuple<uint64_t, uint64_t, bufferlist> > to_read;
  map<hobject_t, map<string, bufferlist> > xattrs_to_read;

  void read(const hobject_t &hoid, uint64_t off, uint64_t len) {
    to_read[hoid].push_back(boost::make_tuple(off, len, bufferlist()));
  }
  void fetch_xattrs(
    const hobject_t &hoid) {
    to_read[hoid];
    xattrs_to_read[hoid];
  }

  map<pg_shard_t, vector<PushOp> > pushes;
  map<pg_shard_t, vector<PushReplyOp> > push_replies;
};

void ECBackend::handle_recovery_read_complete(
  list<boost::tuple<uint64_t, uint64_t, bufferlist> > *to_read,
  map<string, bufferlist> *attrs)
{
  
}

struct OnRecoveryReadComplete : public Context {
  map<
    hobject_t,
    boost::tuple<uint64_t, uint64_t, bufferlist>
    > data;
  map<hobject_t, map<string, bufferlist> > attrs;
  ECBackend *pg;
  void finish(int) {
    for (map<hobject_t,
	   boost::tuple<uint64_t, uint64_t, bufferlist> > ::iterator i =
	   data.begin();
	 i != data.end();
	 data.erase(i++);
  }
};

void ECBackend::dispatch_recovery_messages(RecoveryMessages *m)
{
  for (map<pg_shard_t, vector<PushOp> >::iterator i = m->pushes.begin();
       i != m->pushes.end();
       m->pushes.erase(i++)) {
    MOSDPGPush *msg = new MOSDPGPush();
    msg->pgid = spg_t(get_parent()->get_info().pgid, i->first.shard);
    msg->pushes.swap(i->second);
    msg->compute_cost(cct);
    get_parent()->send_message(
      i->first.osd,
      msg);
  }
  for (map<pg_shard_t, vector<PushReplyOp> >::iterator i = m->push_replies.begin();
       i != m->push_replies.end();
       m->push_replies.erase(i++)) {
    MOSDPGPushReply *msg = new MOSDPGPushReply();
    msg->pgid = spg_t(get_parent()->get_info().pgid, i->first.shard);
    msg->replies.swap(i->second);
    msg->compute_cost(cct);
    get_parent()->send_message(
      i->first.osd,
      msg);
  }
  
}

void ECBackend::run_recovery_op(
  RecoveryHandle *_h,
  int priority)
{
  list<
    pair<
      hobject_t,
      boost::tuple<uint64_t, uint64_t, bufferlist *>
      >
    > to_read;
  set<hobject_t> xattrs_to_fetch;
  ECRecoveryHandle *h = static_cast<ECRecoveryHandle*>(_h);
  for (list<RecoveryOp>::iterator i = h->ops.begin();
       i != h->ops.end();
       ++i) {
    //start_recovery_op(*i);
  }
}

void ECBackend::recover_object(
  const hobject_t &hoid,
  eversion_t v,
  ObjectContextRef head,
  ObjectContextRef obc,
  RecoveryHandle *_h)
{
  ECRecoveryHandle *h = static_cast<ECRecoveryHandle*>(_h);
  h->ops.push_back(RecoveryOp());
  h->ops.back().v = v;
  h->ops.back().hoid = hoid;
  h->ops.back().obc = obc;
  h->ops.back().recovery_info.soid = hoid;
  h->ops.back().recovery_info.version = v;
  if (obc) {
    h->ops.back().recovery_info.size = obc->obs.oi.size;
    h->ops.back().recovery_info.oi = obc->obs.oi;
  }
  h->ops.back().recovery_progress.omap_complete = true;
}

bool ECBackend::handle_message(
  OpRequestRef _op)
{
  dout(10) << __func__ << ": " << _op << dendl;
  switch (_op->get_req()->get_type()) {
  case MSG_OSD_EC_WRITE: {
    MOSDECSubOpWrite *op = static_cast<MOSDECSubOpWrite*>(_op->get_req());
    pg_shard_t from(op->get_source().num(), op->pgid.shard);
    handle_sub_write(from, _op, op->op);
    return true;
  }
  case MSG_OSD_EC_WRITE_REPLY: {
    MOSDECSubOpWriteReply *op = static_cast<MOSDECSubOpWriteReply*>(
      _op->get_req());
    pg_shard_t from(op->get_source().num(), op->pgid.shard);
    handle_sub_write_reply(from, op->op);
    return true;
  }
  case MSG_OSD_EC_READ: {
    MOSDECSubOpRead *op = static_cast<MOSDECSubOpRead*>(_op->get_req());
    pg_shard_t from(op->get_source().num(), op->pgid.shard);
    MOSDECSubOpReadReply *reply = new MOSDECSubOpReadReply;
    reply->pgid = get_parent()->primary_spg_t();
    reply->map_epoch = get_parent()->get_epoch();
    handle_sub_read(from, op->op, &(reply->op));
    get_parent()->send_message_osd_cluster(
      from.osd, reply, get_parent()->get_epoch());
    return true;
  }
  case MSG_OSD_EC_READ_REPLY: {
    MOSDECSubOpReadReply *op = static_cast<MOSDECSubOpReadReply*>(
      _op->get_req());
    pg_shard_t from(op->get_source().num(), op->pgid.shard);
    handle_sub_read_reply(from, op->op);
    return true;
  }
  default:
    return false;
  }
  return false;
}

struct SubWriteCommitted : public Context {
  ECBackend *pg;
  OpRequestRef msg;
  tid_t tid;
  eversion_t version;
  SubWriteCommitted(
    ECBackend *pg,
    OpRequestRef msg,
    tid_t tid,
    eversion_t version)
    : pg(pg), msg(msg), tid(tid), version(version) {}
  void finish(int) {
    msg->mark_event("sub_op_committed");
    pg->sub_write_committed(tid, version);
  }
};
void ECBackend::sub_write_committed(
  tid_t tid, eversion_t version) {
  parent->op_applied(version);
  if (get_parent()->pgb_is_primary()) {
    ECSubWriteReply reply;
    reply.tid = tid;
    reply.committed = true;
    handle_sub_write_reply(
      get_parent()->whoami_shard(),
      reply);
  } else {
    MOSDECSubOpWriteReply *r = new MOSDECSubOpWriteReply;
    r->pgid = get_parent()->primary_spg_t();
    r->map_epoch = get_parent()->get_epoch();
    r->op.tid = tid;
    r->op.committed = true;
    get_parent()->send_message_osd_cluster(
      get_parent()->primary_osd(), r, get_parent()->get_epoch());
  }
}

struct SubWriteApplied : public Context {
  ECBackend *pg;
  OpRequestRef msg;
  tid_t tid;
  eversion_t version;
  SubWriteApplied(
    ECBackend *pg,
    OpRequestRef msg,
    tid_t tid,
    eversion_t version)
    : pg(pg), msg(msg), tid(tid), version(version) {}
  void finish(int) {
    msg->mark_event("sub_op_applied");
    pg->sub_write_applied(tid, version);
  }
};
void ECBackend::sub_write_applied(
  tid_t tid, eversion_t version) {
  parent->op_applied(version);
  if (get_parent()->pgb_is_primary()) {
    ECSubWriteReply reply;
    reply.tid = tid;
    reply.applied = true;
    handle_sub_write_reply(
      get_parent()->whoami_shard(),
      reply);
  } else {
    MOSDECSubOpWriteReply *r = new MOSDECSubOpWriteReply;
    r->pgid = get_parent()->primary_spg_t();
    r->map_epoch = get_parent()->get_epoch();
    r->op.tid = tid;
    r->op.applied = true;
    get_parent()->send_message_osd_cluster(
      get_parent()->primary_osd(), r, get_parent()->get_epoch());
  }
}

void ECBackend::handle_sub_write(
  pg_shard_t from,
  OpRequestRef msg,
  ECSubWrite &op)
{
  msg->mark_started();
  assert(!get_parent()->get_log().get_missing().is_missing(op.soid));
  get_parent()->update_stats(op.stats);
  ObjectStore::Transaction *localt = new ObjectStore::Transaction;
  get_parent()->log_operation(
    op.log_entries,
    op.trim_to,
    !(op.t.empty()),
    localt);
  localt->append(op.t);
  localt->register_on_commit(
    get_parent()->bless_context(
      new SubWriteCommitted(this, msg, op.tid, op.at_version)));
  localt->register_on_commit(
    get_parent()->bless_context(
      new SubWriteApplied(this, msg, op.tid, op.at_version)));
  get_parent()->queue_transaction(localt, msg);
}

void ECBackend::handle_sub_read(
  pg_shard_t from,
  ECSubRead &op,
  ECSubReadReply *reply)
{
  for(list<pair<hobject_t, pair<uint64_t, uint64_t> > >::iterator i =
	op.to_read.begin();
	i != op.to_read.end();
	++i) {
    bufferlist bl;
    store->read(
      i->first.is_temp() ? temp_coll : coll,
      i->first,
      i->second.first,
      i->second.second,
      bl,
      false);
    reply->buffers_read.push_back(
      make_pair(
	i->first,
	make_pair(
	  i->second.second,
	  bl)
	)
      );
  }
  for (set<hobject_t>::iterator i = op.attrs_to_read.begin();
       i != op.attrs_to_read.end();
       ++i) {
    store->getattrs(
      i->is_temp() ? temp_coll : coll,
      *i,
      reply->attrs_read[*i]);
  }
}

void ECBackend::handle_sub_write_reply(
  pg_shard_t from,
  ECSubWriteReply &op)
{
  map<tid_t, Op>::iterator i = tid_to_op_map.find(op.tid);
  assert(i != tid_to_op_map.end());
  if (op.committed) {
    i->second.pending_commit.erase(from);
  }
  if (op.applied) {
    i->second.pending_apply.erase(from);
  }
  check_pending_ops();
}

void ECBackend::handle_sub_read_reply(
  pg_shard_t from,
  ECSubReadReply &op)
{
  map<tid_t, ReadOp>::iterator iter = tid_to_read_map.find(op.tid);
  assert(iter != tid_to_read_map.end());
  assert(iter->second.in_progress.count(from));
  iter->second.complete[from].swap(op.buffers_read);
  iter->second.in_progress.erase(from);

  for (map<hobject_t, map<string, bufferlist> >::iterator i =
	 op.attrs_read.begin();
       i != op.attrs_read.end();
       ++i) {
      map<hobject_t, map<string, bufferlist>*>::iterator j =
	iter->second.attrs_to_read.find(i->first);
      assert(j != iter->second.attrs_to_read.end());
      *(j->second) = i->second;
  }

  if (!iter->second.in_progress.empty())
    return;
  // done
  ReadOp &readop = iter->second;
  map<pg_shard_t,
      list<pair<hobject_t, pair<uint64_t, bufferlist> > >::iterator
      > res_iters;
  list<
    pair<
      hobject_t,
      boost::tuple<uint64_t, uint64_t, bufferlist*>
      >
    >::iterator out_iter;

  for (map<pg_shard_t,
	   list<pair<hobject_t, pair<uint64_t, bufferlist> > >
	 >::iterator i = readop.complete.begin();
       i != readop.complete.end();
       ++i) {
    assert(i->second.size() == readop.to_read.size());
    res_iters.insert(make_pair(i->first, i->second.begin()));
  }
  out_iter = readop.to_read.begin();

  while (true) {
    if (res_iters.begin()->second == readop.complete.begin()->second.end())
      break;
    uint64_t off(res_iters.begin()->second->second.first);
    hobject_t hoid(res_iters.begin()->second->first);
    map<int, bufferlist> chunks;
    for (map<pg_shard_t,
	   list<pair<hobject_t, pair<uint64_t, bufferlist> > >::iterator
	   >::iterator i = res_iters.begin();
	 i != res_iters.end();
	 ++i) {
      assert(i->second->first == hoid);
      assert(i->second->second.first == off);
      chunks[i->first.shard].claim(i->second->second.second);
      ++(i->second);
    }
    bufferlist decoded;
    int r = ECUtil::decode(
      stripe_size, stripe_width, ec_impl, chunks,
      &decoded);
    assert(r == 0);
    out_iter->second.get<2>()->substr_of(
      decoded,
      out_iter->second.get<0>() - ECUtil::logical_to_prev_stripe_bound_obj(
	stripe_size, stripe_width, out_iter->second.get<0>()),
      out_iter->second.get<1>());
  }
  readop.on_complete->complete(0);
  tid_to_read_map.erase(iter);
}

void ECBackend::check_recovery_sources(const OSDMapRef osdmap)
{
}

void ECBackend::_on_change(ObjectStore::Transaction *t)
{
  clear_state();
}

void ECBackend::clear_state()
{
  waiting.clear();
  reading.clear();
  writing.clear();
  tid_to_op_map.clear();
  tid_to_read_map.clear();
}

void ECBackend::on_flushed()
{
}


void ECBackend::dump_recovery_info(Formatter *f) const
{
}

PGBackend::PGTransaction *ECBackend::get_transaction()
{
  return new ECTransaction;
}

void ECBackend::submit_transaction(
  const hobject_t &hoid,
  const eversion_t &at_version,
  PGTransaction *_t,
  const eversion_t &trim_to,
  vector<pg_log_entry_t> &log_entries,
  Context *on_local_applied_sync,
  Context *on_all_applied,
  Context *on_all_commit,
  tid_t tid,
  osd_reqid_t reqid,
  OpRequestRef client_op
  )
{
  assert(!tid_to_op_map.count(tid));
  Op *op = &(tid_to_op_map[tid]);
  op->hoid = hoid;
  op->version = at_version;
  op->trim_to = trim_to;
  op->log_entries.swap(log_entries);
  op->on_local_applied_sync = on_local_applied_sync;
  op->on_all_applied = on_all_applied;
  op->on_all_commit = on_all_commit;
  op->tid = tid;
  op->reqid = reqid;
  op->client_op = client_op;

  op->t = static_cast<ECTransaction*>(_t);
  op->t->populate_deps(
    stripe_width,
    &(op->must_read),
    &(op->writes));
  waiting.push_back(op);
  check_pending_ops();
}

void ECBackend::start_read_op(
  tid_t tid,
  const list<
    pair<
      hobject_t,
      boost::tuple<uint64_t, uint64_t, bufferlist*>
      >
    > &to_read,
  const map<hobject_t, map<string, bufferlist> *> &attrs_to_read,
  Context *onfinish)
{
  assert(!tid_to_read_map.count(tid));
  ReadOp &op(tid_to_read_map[tid]);
  op.to_read = to_read;
  op.in_progress = min_to_read;
  op.on_complete = onfinish;
  op.attrs_to_read = attrs_to_read;

  ECSubRead readmsg;
  readmsg.tid = tid;
  for (list<
	 pair<
	   hobject_t,
	   boost::tuple<uint64_t, uint64_t, bufferlist*>
	   >
	 >::const_iterator i = to_read.begin();
       i != to_read.end();
       ++i) {
    uint64_t obj_offset =
      ECUtil::logical_to_prev_stripe_bound_obj(
	stripe_size, stripe_width,
	i->second.get<0>());
    uint64_t obj_end =
      ECUtil::logical_to_next_stripe_bound_obj(
	stripe_size, stripe_width,
	i->second.get<0>());
    uint64_t obj_len = obj_end - obj_offset;
    readmsg.to_read.push_back(
      make_pair(
	i->first,
	make_pair(obj_offset, obj_len)));
  }
  bool requested_attrs = false;
  for (set<pg_shard_t>::iterator i = op.in_progress.begin();
       i != op.in_progress.end();
       ++i) {
    MOSDECSubOpRead *msg = new MOSDECSubOpRead;
    msg->pgid = get_parent()->whoami_spg_t();
    msg->map_epoch = get_parent()->get_epoch();
    msg->op = readmsg;
    if (!requested_attrs) {
      set<hobject_t> attrs;
      for (map<hobject_t, map<string, bufferlist>*>::const_iterator i =
	     attrs_to_read.begin();
	   i != attrs_to_read.end();
	   ++i) {
	attrs.insert(i->first);
      }
      msg->op.attrs_to_read = attrs;
      requested_attrs = true;
    }
    get_parent()->send_message_osd_cluster(
      i->osd,
      msg,
      get_parent()->get_epoch());
  }
}

void ECBackend::call_commit_apply_cbs()
{
  bool found_not_applied = false;
  bool found_not_committed = false;
  for (list<Op*>::iterator i = writing.begin();
       i != writing.end() && !(found_not_applied && found_not_committed);
       ++i) {
    if (!found_not_committed && (*i)->pending_commit.empty()) {
      if ((*i)->on_all_commit) {
	(*i)->on_all_commit->complete(0);
	(*i)->on_all_commit = 0;
      }
    } else {
      found_not_committed = true;
    }
    if (!found_not_applied && (*i)->pending_apply.empty()) {
      if ((*i)->on_all_applied) {
	(*i)->on_all_applied->complete(0);
	(*i)->on_all_applied = 0;
      }
    } else {
      found_not_applied = true;
    }
  }
}

bool ECBackend::can_read(Op *op) {
  for (set<hobject_t>::iterator i = op->writes.begin();
       i != op->writes.end();
       ++i) {
    if (unstable.count(*i))
      return false;
  }
  return true;
}

struct ReadCB : public Context {
  ECBackend *pg;
  ECBackend::Op *op;

  ReadCB(ECBackend *pg, ECBackend::Op *op) : pg(pg), op(op) {}
  void finish(int r) {
    assert(r == 0);
    op->must_read.clear();
    pg->check_pending_ops();
  }
};

void ECBackend::start_read(Op *op) {
  unstable.insert(op->writes.begin(), op->writes.end());
  if (op->must_read.empty())
    return;
  list<
    pair<
      hobject_t,
      boost::tuple<uint64_t, uint64_t, bufferlist*>
      >
    > to_read;
  for (map<hobject_t, uint64_t>::iterator i = op->must_read.begin();
       i != op->must_read.end();
       ++i) {
    map<hobject_t, pair<uint64_t, bufferlist> >::iterator iter =
      op->reads_completed.insert(
	make_pair(
	  op->hoid,
	  make_pair(
	    i->second,
	    bufferlist()))).first;
    to_read.push_back(
      make_pair(
	i->first,
	boost::make_tuple(
	  i->second,
	  stripe_width,
	  &(iter->second.second))));
  }

  start_read_op(
    op->tid,
    to_read,
    map<hobject_t, map<string, bufferlist>*>(),
    new ReadCB(this, op));
}

void ECBackend::start_write(Op *op) {
  map<shard_id_t, ObjectStore::Transaction> trans;
  for (set<pg_shard_t>::iterator i = actingbackfill.begin();
       i != actingbackfill.end();
       ++i) {
    if (get_parent()->should_send_op(i->shard, op->hoid))
      trans[i->shard];
  }
  op->t->generate_transactions(
    ec_impl,
    coll,
    temp_coll,
    stripe_width,
    stripe_size,
    op->reads_completed,
    &trans,
    &(op->temp_added),
    &(op->temp_cleared));

  for (set<pg_shard_t>::iterator i = actingbackfill.begin();
       i != actingbackfill.end();
       ++i) {
    map<shard_id_t, ObjectStore::Transaction>::iterator iter =
      trans.find(i->shard);
    assert(iter != trans.end());
    bool should_send = get_parent()->should_send_op(*i, op->hoid);
    pg_stat_t stats =
      should_send ?
      get_info().stats :
      parent->get_shard_info().find(*i)->second.stats;
	
    ECSubWrite sop(
      op->tid,
      op->reqid,
      op->hoid,
      stats,
      should_send ? iter->second : ObjectStore::Transaction(),
      op->version,
      op->trim_to,
      op->log_entries,
      op->temp_added,
      op->temp_cleared);
    if (get_parent()->pgb_is_primary()) {
      handle_sub_write(
	get_parent()->whoami_shard(),
	op->client_op,
	sop);
    } else {
      MOSDECSubOpWrite *r = new MOSDECSubOpWrite(sop);
      r->pgid = get_parent()->primary_spg_t();
      r->map_epoch = get_parent()->get_epoch();
      get_parent()->send_message_osd_cluster(
	i->osd, r, get_parent()->get_epoch());
    }
    op->on_local_applied_sync = 0;
  }
}

void ECBackend::check_pending_ops()
{
  call_commit_apply_cbs();
  while (!writing.empty()) {
    Op *op = writing.front();
    if (op->pending_commit.size() || op->pending_apply.size())
      break;
    for (set<hobject_t>::iterator i = op->writes.begin();
	 i != op->writes.end();
	 ++i) {
      assert(unstable.count(*i));
      unstable.erase(*i);
    }
    op->writes.clear();
    writing.pop_front();
  }

  while (!waiting.empty()) {
    Op *op = waiting.front();
    if (can_read(op)) {
      start_read(op);
      waiting.pop_front();
      reading.push_back(op);
    } else {
      break;
    }
  }

  while (!reading.empty()) {
    Op *op = reading.front();
    if (op->must_read.empty()) {
      start_write(op);
      reading.pop_front();
      writing.push_back(op);
    } else {
      break;
    }
  }
}

int ECBackend::objects_read_sync(
  const hobject_t &hoid,
  uint64_t off,
  uint64_t len,
  bufferlist *bl)
{
  return -EOPNOTSUPP;
}

struct CallClientContexts : public Context {
  list<pair<pair<uint64_t, uint64_t>,
	    pair<bufferlist*, Context*> > > to_read;
  Context *c;
  CallClientContexts(
    const list<pair<pair<uint64_t, uint64_t>,
		    pair<bufferlist*, Context*> > > &to_read,
    Context *c)
    : to_read(to_read), c(c) {}
  void finish(int r) {
    for (list<pair<pair<uint64_t, uint64_t>,
		   pair<bufferlist*, Context*> > >::iterator i = to_read.begin();
	 i != to_read.end();
	 to_read.erase(i++)) {
      if (i->second.second) {
	if (r == 0) {
	  i->second.second->complete(i->second.first->length());
	} else {
	  i->second.second->complete(r);
	}
      }
    }
    c->complete(r);
    c = NULL;
  }
  ~CallClientContexts() {
    for (list<pair<pair<uint64_t, uint64_t>,
		   pair<bufferlist*, Context*> > >::iterator i = to_read.begin();
	 i != to_read.end();
	 to_read.erase(i++)) {
      delete i->second.second;
    }
    delete c;
  }
};

void ECBackend::objects_read_async(
  const hobject_t &hoid,
  const list<pair<pair<uint64_t, uint64_t>,
		  pair<bufferlist*, Context*> > > &to_read,
  Context *on_complete)
{
  list<
    pair<
      hobject_t,
      boost::tuple<uint64_t, uint64_t, bufferlist*>
      >
    > for_read_op;
  for (list<pair<pair<uint64_t, uint64_t>,
		 pair<bufferlist*, Context*> > >::const_iterator i =
	 to_read.begin();
       i != to_read.end();
       ++i) {
    for_read_op.push_back(
      make_pair(
	hoid,
	boost::make_tuple(i->first.first, i->first.second, i->second.first)));
  }
  start_read_op(
    get_parent()->get_tid(),
    for_read_op,
    map<hobject_t, map<string, bufferlist>*>(),
    new CallClientContexts(to_read, on_complete));
  return;
}
