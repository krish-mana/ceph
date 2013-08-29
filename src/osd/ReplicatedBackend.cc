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
#include "ReplicatedBackend.h"
#include "messages/MOSDSubOp.h"
#include "messages/MOSDSubOpReply.h"
#include "messages/MOSDPGPush.h"
#include "messages/MOSDPGPull.h"
#include "messages/MOSDPGPushReply.h"

#define dout_subsys ceph_subsys_osd
#define DOUT_PREFIX_ARGS this
#undef dout_prefix
#define dout_prefix _prefix(_dout, this)
static ostream& _prefix(std::ostream *_dout, ReplicatedBackend *pgb) {
  return *_dout << pgb->get_parent()->gen_dbg_prefix();
}

void ReplicatedBackend::recover_object(
  const hobject_t &hoid,
  const ObjectRecoveryInfo &recovery_info,
  RecoveryHandle *h
  )
{
  dout(10) << __func__ << dendl;
}

bool ReplicatedBackend::handle_message(
  OpRequestRef op
  )
{
  dout(10) << __func__ << ": " << op << dendl;
  switch (op->request->get_type()) {
  case MSG_OSD_PG_PUSH:
    // TODOXXX: needs to be active possibly
    do_push(op);
    return true;

  case MSG_OSD_PG_PULL:
    do_pull(op);
    return true;

  case MSG_OSD_PG_PUSH_REPLY:
    do_push_reply(op);
    return true;

  case MSG_OSD_SUBOP: {
    MOSDSubOp *m = static_cast<MOSDSubOp*>(op->request);
    if (m->ops.size() >= 1) {
      OSDOp *first = &m->ops[0];
      switch (first->op.op) {
      case CEPH_OSD_OP_PULL:
	sub_op_pull(op);
	return true;
      case CEPH_OSD_OP_PUSH:
        // TODOXXX: needs to be active possibly
	sub_op_push(op);
	return true;
      }
    }
    break;
  }

  case MSG_OSD_SUBOPREPLY:
    MOSDSubOpReply *r = static_cast<MOSDSubOpReply*>(op->request);
    if (r->ops.size() >= 1) {
      OSDOp &first = r->ops[0];
      switch (first.op.op) {
      case CEPH_OSD_OP_PUSH:
	// continue peer recovery
	sub_op_push_reply(op);
	return true;
      }
    }
    break;
  }
  return false;
}

void ReplicatedBackend::clear_state()
{
  // clear pushing/pulling maps
  pushing.clear();
  pulling.clear();
  pull_from_peer.clear();
}

void ReplicatedBackend::on_change(ObjectStore::Transaction *t)
{
  dout(10) << __func__ << dendl;
  // clear temp
  for (set<hobject_t>::iterator i = temp_contents.begin();
       i != temp_contents.end();
       ++i) {
    dout(10) << __func__ << ": Removing oid "
	     << *i << " from the temp collection" << dendl;
    t->remove(get_temp_coll(t), *i);
  }
  temp_contents.clear();
  clear_state();
}

coll_t ReplicatedBackend::get_temp_coll(ObjectStore::Transaction *t)
{
  if (temp_created)
    return temp_coll;
  if (!osd->store->collection_exists(temp_coll))
      t->create_collection(temp_coll);
  temp_created = true;
  return temp_coll;
}

void ReplicatedBackend::on_flushed()
{
  if (have_temp_coll() &&
      !osd->store->collection_empty(get_temp_coll())) {
    vector<hobject_t> objects;
    osd->store->collection_list(get_temp_coll(), objects);
    derr << __func__ << ": found objects in the temp collection: "
	 << objects << ", crashing now"
	 << dendl;
    assert(0 == "found garbage in the temp collection");
  }
}

/**
 * trim received data to remove what we don't want
 *
 * @param copy_subset intervals we want
 * @param data_included intervals we got
 * @param data_recieved data we got
 * @param intervals_usable intervals we want to keep
 * @param data_usable matching data we want to keep
 */
void ReplicatedBackend::trim_pushed_data(
  const interval_set<uint64_t> &copy_subset,
  const interval_set<uint64_t> &intervals_received,
  bufferlist data_received,
  interval_set<uint64_t> *intervals_usable,
  bufferlist *data_usable)
{
  if (intervals_received.subset_of(copy_subset)) {
    *intervals_usable = intervals_received;
    *data_usable = data_received;
    return;
  }

  intervals_usable->intersection_of(copy_subset,
				    intervals_received);

  uint64_t off = 0;
  for (interval_set<uint64_t>::const_iterator p = intervals_received.begin();
       p != intervals_received.end();
       ++p) {
    interval_set<uint64_t> x;
    x.insert(p.get_start(), p.get_len());
    x.intersection_of(copy_subset);
    for (interval_set<uint64_t>::const_iterator q = x.begin();
	 q != x.end();
	 ++q) {
      bufferlist sub;
      uint64_t data_off = off + (q.get_start() - p.get_start());
      sub.substr_of(data_received, data_off, q.get_len());
      data_usable->claim_append(sub);
    }
    off += p.get_len();
  }
}

/** op_push
 * NOTE: called from opqueue.
 */
void ReplicatedBackend::sub_op_push(OpRequestRef op)
{
  op->mark_started();
  MOSDSubOp *m = static_cast<MOSDSubOp *>(op->request);

  PushOp pop;
  pop.soid = m->recovery_info.soid;
  pop.version = m->version;
  m->claim_data(pop.data);
  pop.data_included.swap(m->data_included);
  pop.omap_header.swap(m->omap_header);
  pop.omap_entries.swap(m->omap_entries);
  pop.attrset.swap(m->attrset);
  pop.recovery_info = m->recovery_info;
  pop.before_progress = m->current_progress;
  pop.after_progress = m->recovery_progress;
  ObjectStore::Transaction *t = new ObjectStore::Transaction;

  if (is_primary()) {
    PullOp resp;
    bool more = handle_pull_response(m->get_source().num(), pop, &resp, t);
    if (more) {
      send_pull_legacy(
	m->get_priority(),
	m->get_source().num(),
	resp.recovery_info,
	resp.recovery_progress);
    }
  } else {
    PushReplyOp resp;
    MOSDSubOpReply *reply = new MOSDSubOpReply(
      m, 0, get_osdmap()->get_epoch(), CEPH_OSD_FLAG_ACK);
    reply->set_priority(m->get_priority());
    assert(entity_name_t::TYPE_OSD == m->get_connection()->peer_type);
    handle_push(m->get_source().num(), pop, &resp, t);
    t->register_on_complete(new C_OSD_SendMessageOnConn(
			     osd, reply, m->get_connection()));
  }
  t->register_on_commit(new C_OnPushCommit(this, op));
  osd->store->queue_transaction(osr.get(), t);
  return;
}

void ReplicatedBackend::_failed_push(int from, const hobject_t &soid)
{
  map<hobject_t,set<int> >::iterator p = missing_loc.find(soid);
  if (p != missing_loc.end()) {
    dout(0) << "_failed_push " << soid << " from osd." << from
	    << ", reps on " << p->second << dendl;

    p->second.erase(from);          // forget about this (bad) peer replica
    if (p->second.empty())
      missing_loc.erase(p);
  } else {
    dout(0) << "_failed_push " << soid << " from osd." << from
	    << " but not in missing_loc ???" << dendl;
  }

  finish_recovery_op(soid);  // close out this attempt,
  pull_from_peer[from].erase(soid);
  pulling.erase(soid);
}

void ReplicatedBackend::_do_push(OpRequestRef op)
{
  MOSDPGPush *m = static_cast<MOSDPGPush *>(op->request);
  assert(m->get_header().type == MSG_OSD_PG_PUSH);
  int from = m->get_source().num();

  vector<PushReplyOp> replies;
  ObjectStore::Transaction *t = new ObjectStore::Transaction;
  for (vector<PushOp>::iterator i = m->pushes.begin();
       i != m->pushes.end();
       ++i) {
    replies.push_back(PushReplyOp());
    handle_push(from, *i, &(replies.back()), t);
  }

  MOSDPGPushReply *reply = new MOSDPGPushReply;
  reply->set_priority(m->get_priority());
  reply->pgid = info.pgid;
  reply->map_epoch = m->map_epoch;
  reply->replies.swap(replies);
  reply->compute_cost(g_ceph_context);

  t->register_on_complete(new C_OSD_SendMessageOnConn(
			    osd, reply, m->get_connection()));

  osd->store->queue_transaction(osr.get(), t);
}

void ReplicatedBackend::_do_pull_response(OpRequestRef op)
{
  MOSDPGPush *m = static_cast<MOSDPGPush *>(op->request);
  assert(m->get_header().type == MSG_OSD_PG_PUSH);
  int from = m->get_source().num();

  vector<PullOp> replies(1);
  ObjectStore::Transaction *t = new ObjectStore::Transaction;
  for (vector<PushOp>::iterator i = m->pushes.begin();
       i != m->pushes.end();
       ++i) {
    bool more = handle_pull_response(from, *i, &(replies.back()), t);
    if (more)
      replies.push_back(PullOp());
  }
  replies.erase(replies.end() - 1);

  if (replies.size()) {
    MOSDPGPull *reply = new MOSDPGPull;
    reply->set_priority(m->get_priority());
    reply->pgid = info.pgid;
    reply->map_epoch = m->map_epoch;
    reply->pulls.swap(replies);
    reply->compute_cost(g_ceph_context);

    t->register_on_complete(new C_OSD_SendMessageOnConn(
			      osd, reply, m->get_connection()));
  }

  osd->store->queue_transaction(osr.get(), t);
}

void ReplicatedBackend::do_pull(OpRequestRef op)
{
  MOSDPGPull *m = static_cast<MOSDPGPull *>(op->request);
  assert(m->get_header().type == MSG_OSD_PG_PULL);
  int from = m->get_source().num();

  map<int, vector<PushOp> > replies;
  for (vector<PullOp>::iterator i = m->pulls.begin();
       i != m->pulls.end();
       ++i) {
    replies[from].push_back(PushOp());
    handle_pull(from, *i, &(replies[from].back()));
  }
  send_pushes(m->get_priority(), replies);
}

void ReplicatedBackend::do_push_reply(OpRequestRef op)
{
  MOSDPGPushReply *m = static_cast<MOSDPGPushReply *>(op->request);
  assert(m->get_header().type == MSG_OSD_PG_PUSH_REPLY);
  int from = m->get_source().num();

  vector<PushOp> replies(1);
  for (vector<PushReplyOp>::iterator i = m->replies.begin();
       i != m->replies.end();
       ++i) {
    bool more = handle_push_reply(from, *i, &(replies.back()));
    if (more)
      replies.push_back(PushOp());
  }
  replies.erase(replies.end() - 1);

  map<int, vector<PushOp> > _replies;
  _replies[from].swap(replies);
  send_pushes(m->get_priority(), _replies);
}

void ReplicatedBackend::handle_push(
  int from, PushOp &pop, PushReplyOp *response,
  ObjectStore::Transaction *t)
{
  dout(10) << "handle_push "
	   << pop.recovery_info
	   << pop.after_progress
	   << dendl;
  bufferlist data;
  data.claim(pop.data);
  bool first = pop.before_progress.first;
  bool complete = pop.after_progress.data_complete &&
    pop.after_progress.omap_complete;

  response->soid = pop.recovery_info.soid;
  submit_push_data(pop.recovery_info,
		   first,
		   complete,
		   pop.data_included,
		   data,
		   pop.omap_header,
		   pop.attrset,
		   pop.omap_entries,
		   t);

  if (complete)
    on_local_recover(
      pop.recovery_info.soid,
      object_stat_sum_t(),
      pop.recovery_info,
      t);
}

void ReplicatedBackend::send_pushes(int prio, map<int, vector<PushOp> > &pushes)
{
  for (map<int, vector<PushOp> >::iterator i = pushes.begin();
       i != pushes.end();
       ++i) {
    ConnectionRef con = osd->get_con_osd_cluster(
      i->first,
      get_osdmap()->get_epoch());
    if (!con)
      continue;
    if (!(con->get_features() & CEPH_FEATURE_OSD_PACKED_RECOVERY)) {
      for (vector<PushOp>::iterator j = i->second.begin();
	   j != i->second.end();
	   ++j) {
	dout(20) << __func__ << ": sending push (legacy) " << *j
		 << " to osd." << i->first << dendl;
	send_push_op_legacy(prio, i->first, *j);
      }
    } else {
      vector<PushOp>::iterator j = i->second.begin();
      while (j != i->second.end()) {
	uint64_t cost = 0;
	uint64_t pushes = 0;
	MOSDPGPush *msg = new MOSDPGPush();
	msg->pgid = info.pgid;
	msg->map_epoch = get_osdmap()->get_epoch();
	msg->set_priority(prio);
	for (;
	     (j != i->second.end() &&
	      cost < g_conf->osd_max_push_cost &&
	      pushes < g_conf->osd_max_push_objects) ;
	     ++j) {
	  dout(20) << __func__ << ": sending push " << *j
		   << " to osd." << i->first << dendl;
	  cost += j->cost(g_ceph_context);
	  pushes += 1;
	  msg->pushes.push_back(*j);
	}
	msg->compute_cost(g_ceph_context);
	osd->send_message_osd_cluster(msg, con);
      }
    }
  }
}

void ReplicatedBackend::send_pulls(int prio, map<int, vector<PullOp> > &pulls)
{
  for (map<int, vector<PullOp> >::iterator i = pulls.begin();
       i != pulls.end();
       ++i) {
    ConnectionRef con = osd->get_con_osd_cluster(
      i->first,
      get_osdmap()->get_epoch());
    if (!con)
      continue;
    if (!(con->get_features() & CEPH_FEATURE_OSD_PACKED_RECOVERY)) {
      for (vector<PullOp>::iterator j = i->second.begin();
	   j != i->second.end();
	   ++j) {
	dout(20) << __func__ << ": sending pull (legacy) " << *j
		 << " to osd." << i->first << dendl;
	send_pull_legacy(
	  prio,
	  i->first,
	  j->recovery_info,
	  j->recovery_progress);
      }
    } else {
      dout(20) << __func__ << ": sending pulls " << i->second
	       << " to osd." << i->first << dendl;
      MOSDPGPull *msg = new MOSDPGPull();
      msg->set_priority(prio);
      msg->pgid = info.pgid;
      msg->map_epoch = get_osdmap()->get_epoch();
      msg->pulls.swap(i->second);
      msg->compute_cost(g_ceph_context);
      osd->send_message_osd_cluster(msg, con);
    }
  }
}

int ReplicatedBackend::send_push(int prio, int peer,
			    const ObjectRecoveryInfo &recovery_info,
			    const ObjectRecoveryProgress &progress,
			    ObjectRecoveryProgress *out_progress)
{
  PushOp op;
  int r = build_push_op(recovery_info, progress, out_progress, &op);
  if (r < 0)
    return r;
  return send_push_op_legacy(prio, peer, op);
}

int ReplicatedBackend::build_push_op(const ObjectRecoveryInfo &recovery_info,
				const ObjectRecoveryProgress &progress,
				ObjectRecoveryProgress *out_progress,
				PushOp *out_op)
{
  ObjectRecoveryProgress _new_progress;
  if (!out_progress)
    out_progress = &_new_progress;
  ObjectRecoveryProgress &new_progress = *out_progress;
  new_progress = progress;

  dout(7) << "send_push_op " << recovery_info.soid
	  << " v " << recovery_info.version
	  << " size " << recovery_info.size
	  << " recovery_info: " << recovery_info
          << dendl;

  if (progress.first) {
    osd->store->omap_get_header(coll, recovery_info.soid, &out_op->omap_header);
    osd->store->getattrs(coll, recovery_info.soid, out_op->attrset);

    // Debug
    bufferlist bv;
    bv.push_back(out_op->attrset[OI_ATTR]);
    object_info_t oi(bv);

    if (oi.version != recovery_info.version) {
      osd->clog.error() << info.pgid << " push "
			<< recovery_info.soid << " v "
			<< " failed because local copy is "
			<< oi.version << "\n";
      return -EINVAL;
    }

    new_progress.first = false;
  }

  uint64_t available = g_conf->osd_recovery_max_chunk;
  if (!progress.omap_complete) {
    ObjectMap::ObjectMapIterator iter =
      osd->store->get_omap_iterator(coll,
				    recovery_info.soid);
    for (iter->lower_bound(progress.omap_recovered_to);
	 iter->valid();
	 iter->next()) {
      if (!out_op->omap_entries.empty() &&
	  available <= (iter->key().size() + iter->value().length()))
	break;
      out_op->omap_entries.insert(make_pair(iter->key(), iter->value()));

      if ((iter->key().size() + iter->value().length()) <= available)
	available -= (iter->key().size() + iter->value().length());
      else
	available = 0;
    }
    if (!iter->valid())
      new_progress.omap_complete = true;
    else
      new_progress.omap_recovered_to = iter->key();
  }

  if (available > 0) {
    out_op->data_included.span_of(recovery_info.copy_subset,
				 progress.data_recovered_to,
				 available);
  } else {
    out_op->data_included.clear();
  }

  for (interval_set<uint64_t>::iterator p = out_op->data_included.begin();
       p != out_op->data_included.end();
       ++p) {
    bufferlist bit;
    osd->store->read(coll, recovery_info.soid,
		     p.get_start(), p.get_len(), bit);
    if (p.get_len() != bit.length()) {
      dout(10) << " extent " << p.get_start() << "~" << p.get_len()
	       << " is actually " << p.get_start() << "~" << bit.length()
	       << dendl;
      p.set_len(bit.length());
      new_progress.data_complete = true;
    }
    out_op->data.claim_append(bit);
  }

  if (!out_op->data_included.empty())
    new_progress.data_recovered_to = out_op->data_included.range_end();

  if (new_progress.is_complete(recovery_info)) {
    new_progress.data_complete = true;
    info.stats.stats.sum.num_objects_recovered++;
  }

  info.stats.stats.sum.num_keys_recovered += out_op->omap_entries.size();
  info.stats.stats.sum.num_bytes_recovered += out_op->data.length();

  osd->logger->inc(l_osd_push);
  osd->logger->inc(l_osd_push_outb, out_op->data.length());
  
  // send
  out_op->version = recovery_info.version;
  out_op->soid = recovery_info.soid;
  out_op->recovery_info = recovery_info;
  out_op->after_progress = new_progress;
  out_op->before_progress = progress;
  return 0;
}

int ReplicatedBackend::send_push_op_legacy(int prio, int peer, PushOp &pop)
{
  tid_t tid = osd->get_tid();
  osd_reqid_t rid(osd->get_cluster_msgr_name(), 0, tid);
  MOSDSubOp *subop = new MOSDSubOp(rid, info.pgid, pop.soid,
				   false, 0, get_osdmap()->get_epoch(),
				   tid, pop.recovery_info.version);
  subop->ops = vector<OSDOp>(1);
  subop->ops[0].op.op = CEPH_OSD_OP_PUSH;

  subop->set_priority(prio);
  subop->version = pop.version;
  subop->ops[0].indata.claim(pop.data);
  subop->data_included.swap(pop.data_included);
  subop->omap_header.claim(pop.omap_header);
  subop->omap_entries.swap(pop.omap_entries);
  subop->attrset.swap(pop.attrset);
  subop->recovery_info = pop.recovery_info;
  subop->current_progress = pop.before_progress;
  subop->recovery_progress = pop.after_progress;

  osd->send_message_osd_cluster(peer, subop, get_osdmap()->get_epoch());
  return 0;
}

void ReplicatedBackend::prep_push_op_blank(const hobject_t& soid, PushOp *op)
{
  op->recovery_info.version = eversion_t();
  op->version = eversion_t();
  op->soid = soid;
}

void ReplicatedBackend::sub_op_push_reply(OpRequestRef op)
{
  MOSDSubOpReply *reply = static_cast<MOSDSubOpReply*>(op->request);
  const hobject_t& soid = reply->get_poid();
  assert(reply->get_header().type == MSG_OSD_SUBOPREPLY);
  dout(10) << "sub_op_push_reply from " << reply->get_source() << " " << *reply << dendl;
  int peer = reply->get_source().num();

  op->mark_started();
  
  PushReplyOp rop;
  rop.soid = soid;
  PushOp pop;
  bool more = handle_push_reply(peer, rop, &pop);
  if (more)
    send_push_op_legacy(pushing[soid][peer].priority, peer, pop);
}

bool ReplicatedBackend::handle_push_reply(int peer, PushReplyOp &op, PushOp *reply)
{
  const hobject_t &soid = op.soid;
  if (pushing.count(soid) == 0) {
    dout(10) << "huh, i wasn't pushing " << soid << " to osd." << peer
	     << ", or anybody else"
	     << dendl;
    return false;
  } else if (pushing[soid].count(peer) == 0) {
    dout(10) << "huh, i wasn't pushing " << soid << " to osd." << peer
	     << dendl;
    return false;
  } else {
    PushInfo *pi = &pushing[soid][peer];

    if (!pi->recovery_progress.data_complete) {
      dout(10) << " pushing more from, "
	       << pi->recovery_progress.data_recovered_to
	       << " of " << pi->recovery_info.copy_subset << dendl;
      ObjectRecoveryProgress new_progress;
      build_push_op(
	pi->recovery_info,
	pi->recovery_progress, &new_progress, reply);
      pi->recovery_progress = new_progress;
      return true;
    } else {
      on_peer_recover(peer, soid, pi->recovery_info);
      pushing[soid].erase(peer);
      pi = NULL;
      
      if (pushing[soid].empty()) {
	on_global_recover(soid);
      } else {
	dout(10) << "pushed " << soid << ", still waiting for push ack from " 
		 << pushing[soid].size() << " others" << dendl;
      }
      return false;
    }
  }
}

int ReplicatedBackend::send_pull_legacy(int prio, int peer,
			    const ObjectRecoveryInfo &recovery_info,
			    ObjectRecoveryProgress progress)
{
  // send op
  tid_t tid = osd->get_tid();
  osd_reqid_t rid(osd->get_cluster_msgr_name(), 0, tid);

  dout(10) << "send_pull_op " << recovery_info.soid << " "
	   << recovery_info.version
	   << " first=" << progress.first
	   << " data " << recovery_info.copy_subset
	   << " from osd." << peer
	   << " tid " << tid << dendl;

  MOSDSubOp *subop = new MOSDSubOp(rid, info.pgid, recovery_info.soid,
				   false, CEPH_OSD_FLAG_ACK,
				   get_osdmap()->get_epoch(), tid,
				   recovery_info.version);
  subop->set_priority(prio);
  subop->ops = vector<OSDOp>(1);
  subop->ops[0].op.op = CEPH_OSD_OP_PULL;
  subop->ops[0].op.extent.length = g_conf->osd_recovery_max_chunk;
  subop->recovery_info = recovery_info;
  subop->recovery_progress = progress;

  osd->send_message_osd_cluster(peer, subop, get_osdmap()->get_epoch());

  osd->logger->inc(l_osd_pull);
  return 0;
}

void ReplicatedBackend::submit_push_data(
  ObjectRecoveryInfo &recovery_info,
  bool first,
  bool complete,
  const interval_set<uint64_t> &intervals_included,
  bufferlist data_included,
  bufferlist omap_header,
  map<string, bufferptr> &attrs,
  map<string, bufferlist> &omap_entries,
  ObjectStore::Transaction *t)
{
  coll_t target_coll;
  if (first && complete) {
    target_coll = coll;
  } else {
    dout(10) << __func__ << ": Creating oid "
	     << recovery_info.soid << " in the temp collection" << dendl;
    temp_contents.insert(recovery_info.soid);
    target_coll = get_temp_coll(t);
  }

  if (first) {
    on_local_recover_start(recovery_info.soid, t);
    t->remove(get_temp_coll(t), recovery_info.soid);
    t->touch(target_coll, recovery_info.soid);
    t->omap_setheader(target_coll, recovery_info.soid, omap_header);
  }
  uint64_t off = 0;
  for (interval_set<uint64_t>::const_iterator p = intervals_included.begin();
       p != intervals_included.end();
       ++p) {
    bufferlist bit;
    bit.substr_of(data_included, off, p.get_len());
    t->write(target_coll, recovery_info.soid,
	     p.get_start(), p.get_len(), bit);
    off += p.get_len();
  }

  t->omap_setkeys(target_coll, recovery_info.soid,
		  omap_entries);
  t->setattrs(target_coll, recovery_info.soid,
	      attrs);

  if (complete) {
    if (!first) {
      assert(temp_contents.count(recovery_info.soid));
      dout(10) << __func__ << ": Removing oid "
	       << recovery_info.soid << " from the temp collection" << dendl;
      temp_contents.erase(recovery_info.soid);
      t->collection_move(coll, target_coll, recovery_info.soid);
    }

    submit_push_complete(recovery_info, t);
  }
}

void ReplicatedBackend::submit_push_complete(ObjectRecoveryInfo &recovery_info,
					ObjectStore::Transaction *t)
{
  for (map<hobject_t, interval_set<uint64_t> >::const_iterator p =
	 recovery_info.clone_subset.begin();
       p != recovery_info.clone_subset.end();
       ++p) {
    for (interval_set<uint64_t>::const_iterator q = p->second.begin();
	 q != p->second.end();
	 ++q) {
      dout(15) << " clone_range " << p->first << " "
	       << q.get_start() << "~" << q.get_len() << dendl;
      t->clone_range(coll, p->first, recovery_info.soid,
		     q.get_start(), q.get_len(), q.get_start());
    }
  }
}

ObjectRecoveryInfo ReplicatedBackend::recalc_subsets(const ObjectRecoveryInfo& recovery_info)
{
  if (!recovery_info.soid.snap || recovery_info.soid.snap >= CEPH_NOSNAP)
    return recovery_info;

  SnapSetContext *ssc = get_snapset_context(recovery_info.soid.oid,
					    recovery_info.soid.get_key(),
					    recovery_info.soid.hash,
					    false,
					    recovery_info.soid.get_namespace());
  assert(ssc);
  ObjectRecoveryInfo new_info = recovery_info;
  new_info.copy_subset.clear();
  new_info.clone_subset.clear();
  assert(ssc);
  calc_clone_subsets(ssc->snapset, new_info.soid, pg_log.get_missing(), info.last_backfill,
		     new_info.copy_subset, new_info.clone_subset);
  put_snapset_context(ssc);
  return new_info;
}

bool ReplicatedBackend::handle_pull_response(
  int from, PushOp &pop, PullOp *response,
  ObjectStore::Transaction *t)
{
  interval_set<uint64_t> data_included = pop.data_included;
  bufferlist data;
  data.claim(pop.data);
  dout(10) << "handle_pull_response "
	   << pop.recovery_info
	   << pop.after_progress
	   << " data.size() is " << data.length()
	   << " data_included: " << data_included
	   << dendl;
  if (pop.version == eversion_t()) {
    // replica doesn't have it!
    _failed_push(from, pop.soid);
    return false;
  }

  hobject_t &hoid = pop.soid;
  assert((data_included.empty() && data.length() == 0) ||
	 (!data_included.empty() && data.length() > 0));

  if (!pulling.count(hoid)) {
    return false;
  }

  PullInfo &pi = pulling[hoid];
  if (pi.recovery_info.size == (uint64_t(-1))) {
    pi.recovery_info.size = pop.recovery_info.size;
    pi.recovery_info.copy_subset.intersection_of(
      pop.recovery_info.copy_subset);
  }

  pi.recovery_info = recalc_subsets(pi.recovery_info);

  interval_set<uint64_t> usable_intervals;
  bufferlist usable_data;
  trim_pushed_data(pi.recovery_info.copy_subset,
		   data_included,
		   data,
		   &usable_intervals,
		   &usable_data);
  data_included = usable_intervals;
  data.claim(usable_data);

  info.stats.stats.sum.num_bytes_recovered += data.length();

  bool first = pi.recovery_progress.first;
  pi.recovery_progress = pop.after_progress;

  dout(10) << "new recovery_info " << pi.recovery_info
	   << ", new progress " << pi.recovery_progress
	   << dendl;

  if (first) {
    bufferlist oibl;
    if (pop.attrset.count(OI_ATTR)) {
      oibl.push_back(pop.attrset[OI_ATTR]);
      ::decode(pi.recovery_info.oi, oibl);
    } else {
      assert(0);
    }
    bufferlist ssbl;
    if (pop.attrset.count(SS_ATTR)) {
      ssbl.push_back(pop.attrset[SS_ATTR]);
      ::decode(pi.recovery_info.ss, ssbl);
    } else {
      assert(pi.recovery_info.soid.snap != CEPH_NOSNAP &&
	     pi.recovery_info.soid.snap != CEPH_SNAPDIR);
    }
  }

  bool complete = pi.is_complete();

  submit_push_data(pi.recovery_info, first,
		   complete,
		   data_included, data,
		   pop.omap_header,
		   pop.attrset,
		   pop.omap_entries,
		   t);

  info.stats.stats.sum.num_keys_recovered += pop.omap_entries.size();

  if (complete) {
    pulling.erase(hoid);
    pull_from_peer[from].erase(hoid);
    info.stats.stats.sum.num_objects_recovered++;
    on_local_recover(hoid, object_stat_sum_t(), pi.recovery_info, t);
    return false;
  } else {
    response->soid = pop.soid;
    response->recovery_info = pi.recovery_info;
    response->recovery_progress = pi.recovery_progress;
    return true;
  }
}

/** op_pull
 * process request to pull an entire object.
 * NOTE: called from opqueue.
 */
void ReplicatedBackend::sub_op_pull(OpRequestRef op)
{
  MOSDSubOp *m = static_cast<MOSDSubOp*>(op->request);
  assert(m->get_header().type == MSG_OSD_SUBOP);

  op->mark_started();

  const hobject_t soid = m->poid;

  dout(7) << "pull" << soid << " v " << m->version
          << " from " << m->get_source()
          << dendl;

  assert(!is_primary());  // we should be a replica or stray.

  PullOp pop;
  pop.soid = soid;
  pop.recovery_info = m->recovery_info;
  pop.recovery_progress = m->recovery_progress;

  PushOp reply;
  handle_pull(m->get_source().num(), pop, &reply);
  send_push_op_legacy(
    m->get_priority(),
    m->get_source().num(),
    reply);

  log_subop_stats(op, 0, l_osd_sop_pull_lat);
}

void ReplicatedBackend::handle_pull(int peer, PullOp &op, PushOp *reply)
{
  const hobject_t &soid = op.soid;
  struct stat st;
  int r = osd->store->stat(coll, soid, &st);
  if (r != 0) {
    osd->clog.error() << info.pgid << " " << peer << " tried to pull " << soid
		      << " but got " << cpp_strerror(-r) << "\n";
    prep_push_op_blank(soid, reply);
  } else {
    ObjectRecoveryInfo &recovery_info = op.recovery_info;
    ObjectRecoveryProgress &progress = op.recovery_progress;
    if (progress.first && recovery_info.size == ((uint64_t)-1)) {
      // Adjust size and copy_subset
      recovery_info.size = st.st_size;
      recovery_info.copy_subset.clear();
      if (st.st_size)
	recovery_info.copy_subset.insert(0, st.st_size);
      assert(recovery_info.clone_subset.empty());
    }

    r = build_push_op(recovery_info, progress, 0, reply);
    if (r < 0)
      prep_push_op_blank(soid, reply);
  }
}

void ReplicatedBackend::prep_push_op_blank(const hobject_t& soid, PushOp *op)
{
  op->recovery_info.version = eversion_t();
  op->version = eversion_t();
  op->soid = soid;
}

