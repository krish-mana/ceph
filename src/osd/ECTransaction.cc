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
#include <vector>
#include <sstream>

#include "ECBackend.h"
#include "ECUtil.h"
#include "os/ObjectStore.h"

struct DepPopulator : public boost::static_visitor<void> {
  typedef void result_type;

  uint64_t cur_id;
  const uint64_t stripe_width;
  stringstream *out;
  DepPopulator(
    const uint64_t stripe_width,
    stringstream *out)
    : cur_id(0), stripe_width(stripe_width), out(out) {}

  set<hobject_t> writes;
  map<uint64_t, boost::optional<uint64_t> > id_to_read_offset;
  map<hobject_t, uint64_t> cur_obj_to_id;
  map<uint64_t, hobject_t> id_to_orig_obj;

  map<hobject_t, uint64_t>::iterator get_cur_obj_to_id(const hobject_t &oid) {
    map<hobject_t, uint64_t>::iterator obj_iter = cur_obj_to_id.find(oid);
    if (obj_iter == cur_obj_to_id.end()) {
      uint64_t new_id = ++cur_id;
      id_to_orig_obj.insert(make_pair(new_id, oid));
      obj_iter = cur_obj_to_id.insert(make_pair(oid, new_id)).first;
      if (out)
	*out << "added to cur_obj_to_id: " << *obj_iter << std::endl;
    } else {
      if (out)
	*out << "found obj_iter: " << *obj_iter << std::endl;
    }
    return obj_iter;
  }

  void operator()(const ECTransaction::AppendOp &op) {
    writes.insert(op.oid);

    map<hobject_t, uint64_t>::iterator obj_iter =
      get_cur_obj_to_id(op.oid);

    map<uint64_t, boost::optional<uint64_t> >::iterator rd_iter =
      id_to_read_offset.find(obj_iter->second);
    if (rd_iter == id_to_read_offset.end()) {
      if (op.off % stripe_width != 0) {
	id_to_read_offset[obj_iter->second] = op.off - (op.off % stripe_width);
      } else {
	id_to_read_offset[obj_iter->second];
      }
    }
  }
  void operator()(const ECTransaction::CloneOp &op) {
    writes.insert(op.target);
    map<hobject_t, uint64_t>::iterator obj_iter =
      get_cur_obj_to_id(op.source);

    bool inserted = cur_obj_to_id.insert(
      make_pair(
	op.target,
	obj_iter->second)).second;
    assert(inserted);
  }
  void operator()(const ECTransaction::RenameOp &op) {
    writes.insert(op.source);
    writes.insert(op.destination);

    map<hobject_t, uint64_t>::iterator obj_iter =
      get_cur_obj_to_id(op.source);

    bool inserted = cur_obj_to_id.insert(
      make_pair(
	op.destination,
	obj_iter->second)).second;
    assert(inserted);

    cur_obj_to_id.erase(obj_iter);
  }
  void remove(const hobject_t &oid) {
    writes.insert(oid);
    map<hobject_t, uint64_t>::iterator obj_iter =
      get_cur_obj_to_id(oid);

    cur_obj_to_id.erase(obj_iter);

    obj_iter =
      get_cur_obj_to_id(oid);
    id_to_read_offset[obj_iter->second];
  }
  void operator()(const ECTransaction::StashOp &op) {
    remove(op.oid);
  }
  void operator()(const ECTransaction::RemoveOp &op) {
    remove(op.oid);
  }
  void operator()(const ECTransaction::SetAttrsOp &op) {
    writes.insert(op.oid);
  }
  void operator()(const ECTransaction::RmAttrOp &op) {
    writes.insert(op.oid);
  }
  void operator()(const ECTransaction::NoOp &op) {}

  void get_results(
    map<hobject_t, uint64_t> *must_read,
    set<hobject_t> *_writes) {
    _writes->swap(writes);
    for(map<uint64_t, boost::optional<uint64_t> >::iterator i =
	  id_to_read_offset.begin();
	i != id_to_read_offset.end();
	++i) {
      if (i->second) {
	map<uint64_t, hobject_t>::iterator iter =
	  id_to_orig_obj.find(i->first);
	assert(iter != id_to_orig_obj.end());
	must_read->insert(
	  make_pair(iter->second, *(i->second)));
      }
    }
  }
};

void ECTransaction::populate_deps(
  uint64_t stripe_width,
  map<hobject_t, uint64_t> *must_read,
  set<hobject_t> *writes,
  stringstream *dbg) const
{
  DepPopulator dep(stripe_width, dbg);
  visit(dep);
  dep.get_results(must_read, writes);
}

struct TransGenerator : public boost::static_visitor<void> {
  typedef void result_type;

  ErasureCodeInterfaceRef &ecimpl;
  const coll_t coll;
  const coll_t temp_coll;
  const uint64_t stripe_width;
  const uint64_t stripe_size;
  map<hobject_t, pair<uint64_t, bufferlist> > partial_stripes;
  map<shard_id_t, ObjectStore::Transaction> *trans;
  set<int> want;
  set<hobject_t> *temp_added;
  set<hobject_t> *temp_removed;
  stringstream *out;
  TransGenerator(
    ErasureCodeInterfaceRef &ecimpl,
    coll_t coll,
    coll_t temp_coll,
    const uint64_t stripe_width,
    const uint64_t stripe_size,
    const map<hobject_t, pair<uint64_t, bufferlist> > &partial_stripes,
    map<shard_id_t, ObjectStore::Transaction> *trans,
    set<hobject_t> *temp_added,
    set<hobject_t> *temp_removed,
    stringstream *out)
    : ecimpl(ecimpl), coll(coll), temp_coll(temp_coll),
      stripe_width(stripe_width), stripe_size(stripe_size),
      partial_stripes(partial_stripes),
      trans(trans),
      temp_added(temp_added), temp_removed(temp_removed),
      out(out) {
    for (map<shard_id_t, ObjectStore::Transaction>::iterator i = trans->begin();
	 i != trans->end();
	 ++i) {
      want.insert(i->first);
    }
  }

  const coll_t &get_coll_ct(const hobject_t &hoid) {
    if (hoid.is_temp()) {
      temp_removed->erase(hoid);
      temp_added->insert(hoid);
    }
    return get_coll(hoid);
  }
  const coll_t &get_coll_rm(const hobject_t &hoid) {
    if (hoid.is_temp()) {
      temp_added->erase(hoid);
      temp_removed->insert(hoid);
    }
    return get_coll(hoid);
  }
  const coll_t &get_coll(const hobject_t &hoid) {
    if (hoid.is_temp())
      return temp_coll;
    else
      return coll;
  }

  void operator()(const ECTransaction::AppendOp &op) {
    coll_t cid(get_coll_ct(op.oid));
    uint64_t offset = op.off;
    bufferlist bl(op.bl);
    if (offset % stripe_width != 0) {
      map<hobject_t, pair<uint64_t, bufferlist> >::iterator iter =
	partial_stripes.find(op.oid);
      assert(iter != partial_stripes.end());
      assert(iter->second.first == offset - (offset % stripe_width));
      bufferlist bl2;
      bl2.substr_of(iter->second.second, 0, offset % stripe_width);
      bl2.claim_append(bl);
      bl.swap(bl2);
      offset = iter->second.first;
    }
    map<int, bufferlist> buffers;

    // align
    bl.append_zero(stripe_width - ((offset + bl.length()) % stripe_width));
    int r = ECUtil::encode(
      stripe_size, stripe_width, ecimpl, bl, want, &buffers);
    assert(r == 0);
    for (map<shard_id_t, ObjectStore::Transaction>::iterator i = trans->begin();
	 i != trans->end();
	 ++i) {
      assert(buffers.count(i->first));
      bufferlist &enc_bl = buffers[i->first];
      i->second.write(
	cid,
	ghobject_t(op.oid, ghobject_t::NO_GEN, i->first),
	(offset / stripe_width) * stripe_size,
	enc_bl.length(),
	enc_bl);
    }

    uint64_t end(bl.length());
    if (end % stripe_width != 0) {
      bufferlist bl3;
      bl3.substr_of(bl, end - (end % stripe_width), end);
      partial_stripes.insert(
	make_pair(
	  op.oid,
	  make_pair(
	    end - (end % stripe_width),
	    bl3)));
    } else {
      // subsequent append must be aligned
      partial_stripes.erase(op.oid);
    }
  }
  void operator()(const ECTransaction::CloneOp &op) {
    coll_t cid(get_coll_ct(op.target));
    map<hobject_t, pair<uint64_t, bufferlist> >::iterator siter =
      partial_stripes.find(op.source);
    assert(siter != partial_stripes.end());
    bool inserted = partial_stripes.insert(
      make_pair(op.target, siter->second)).second;
    assert(inserted);

    for (map<shard_id_t, ObjectStore::Transaction>::iterator i = trans->begin();
	 i != trans->end();
	 ++i) {
      i->second.clone(
	cid,
	ghobject_t(op.source, ghobject_t::NO_GEN, i->first),
	ghobject_t(op.target, ghobject_t::NO_GEN, i->first));
    }
  }
  void operator()(const ECTransaction::RenameOp &op) {
    coll_t to_cid(get_coll_ct(op.destination));
    coll_t from_cid(get_coll_rm(op.source));
    map<hobject_t, pair<uint64_t, bufferlist> >::iterator siter =
      partial_stripes.find(op.source);
    assert(siter != partial_stripes.end());
    bool inserted = partial_stripes.insert(
      make_pair(op.destination, siter->second)).second;
    assert(inserted);
    partial_stripes.erase(siter);

    for (map<shard_id_t, ObjectStore::Transaction>::iterator i = trans->begin();
	 i != trans->end();
	 ++i) {
      i->second.collection_move_rename(
	from_cid,
	ghobject_t(op.source, ghobject_t::NO_GEN, i->first),
	to_cid,
	ghobject_t(op.destination, ghobject_t::NO_GEN, i->first));
    }
  }
  void operator()(const ECTransaction::StashOp &op) {
    partial_stripes.erase(op.oid);
    coll_t cid(get_coll_rm(op.oid));
    for (map<shard_id_t, ObjectStore::Transaction>::iterator i = trans->begin();
	 i != trans->end();
	 ++i) {
      i->second.collection_move_rename(
	cid,
	ghobject_t(op.oid, ghobject_t::NO_GEN, i->first),
	cid,
	ghobject_t(op.oid, op.version, i->first));
    }
  }
  void operator()(const ECTransaction::RemoveOp &op) {
    partial_stripes.erase(op.oid);
    coll_t cid(get_coll_rm(op.oid));
    for (map<shard_id_t, ObjectStore::Transaction>::iterator i = trans->begin();
	 i != trans->end();
	 ++i) {
      i->second.remove(
	cid,
	ghobject_t(op.oid, ghobject_t::NO_GEN, i->first));
    }
  }
  void operator()(const ECTransaction::SetAttrsOp &op) {
    coll_t cid(get_coll_ct(op.oid));
    map<string, bufferlist> attrs(op.attrs);
    for (map<shard_id_t, ObjectStore::Transaction>::iterator i = trans->begin();
	 i != trans->end();
	 ++i) {
      i->second.setattrs(
	cid,
	ghobject_t(op.oid, ghobject_t::NO_GEN, i->first),
	attrs);
    }
  }
  void operator()(const ECTransaction::RmAttrOp &op) {
    coll_t cid(get_coll_ct(op.oid));
    for (map<shard_id_t, ObjectStore::Transaction>::iterator i = trans->begin();
	 i != trans->end();
	 ++i) {
      i->second.rmattr(
	cid,
	ghobject_t(op.oid, ghobject_t::NO_GEN, i->first),
	op.key);
    }
  }
  void operator()(const ECTransaction::NoOp &op) {}
};


void ECTransaction::generate_transactions(
  ErasureCodeInterfaceRef &ecimpl,
  coll_t coll,
  coll_t temp_coll,
  uint64_t stripe_width,
  uint64_t stripe_size,
  const map<hobject_t, pair<uint64_t, bufferlist> > &reads,
  map<shard_id_t, ObjectStore::Transaction> *transactions,
  set<hobject_t> *temp_added,
  set<hobject_t> *temp_removed,
  stringstream *out) const
{
  TransGenerator gen(
    ecimpl,
    coll, temp_coll,
    stripe_width,
    stripe_size,
    reads,
    transactions,
    temp_added,
    temp_removed,
    out);
  visit(gen);
}
