// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "OSDReactor.h"
#include "OSD.h"
#include <sstream>

// TODO: make a config option
const unsigned MAX_INFLIGHT_OPS = 16;

OSDReactor::OSDReactor(
  const string &thread_name,
  OSD *osd,
  CephContext *cct) :
  thread_name(thread_name),
  osd(osd),
  cct(cct),
  stop(false),
  running(false),
  in_queue(
    cct->_conf->osd_op_pq_max_tokens_per_priority,
    cct->_conf->osd_op_pq_min_cost)
{
}

bool OSDReactor::empty()
{
  unique_lock l(lock);
  return in_queue.empty();
}

void OSDReactor::start()
{
  unique_lock l(lock);
  running = true;
  t = std::thread([this](){ _run(); });
}

void OSDReactor::join()
{
  {
    unique_lock l(lock);
    assert(running);
    assert(!stop);
    stop = true;
    while (running)
      stop_cv.wait(l);
  }
  t.join();
}

void OSDReactor::drain()
{
  unique_lock l(lock);
  while (!in_queue.empty())
    drain_cv.wait(l);
}

void OSDReactor::_run()
{
  unique_lock l(lock);
  heartbeat_handle_d *hb = cct->get_heartbeat_map()->add_worker(thread_name);
  HBHandle tp_handle(
    cct->get_heartbeat_map(),
    hb,
    cct->_conf->osd_op_thread_timeout,
    cct->_conf->osd_op_thread_suicide_timeout);

  while (!stop) {
    while (in_progress.empty() && in_queue.empty()) {
      tp_handle.suspend_tp_timeout();
      queue_cv.wait(l);
      tp_handle.reset_tp_timeout();
    }
    l.unlock();

    bool did_work = false;
    for (auto i = in_progress.begin();
	 i != in_progress.end();
	 ++i) {
      i->first->lock_suspend_timeout(tp_handle);
      while (!i->second.blocked() && !i->second.ready()) {
	i->second.run_until_blocked_or_ready();
	did_work = true;
      }
      i->first->unlock();
      if (i->second.ready()) {
#if 0
// need to figure out how to make this work
	{
#ifdef WITH_LTTNG
	  osd_reqid_t reqid;
	  if (boost::optional<OpRequestRef> _op = op->maybe_get_op()) {
	    reqid = (*_op)->get_reqid();
	  }
#endif
	  tracepoint(osd, opwq_process_finish, reqid.name._type,
		     reqid.name._num, reqid.tid, reqid.inc);
	}
#endif
	did_work = true;
	i->second.get();
	in_progress.erase(i++);
      } else {
	++i;
      }
    }

    tp_handle.suspend_tp_timeout();
    l.lock();
    tp_handle.reset_tp_timeout();

    if (did_work || in_progress.size() > MAX_INFLIGHT_OPS) {
      continue;
    }

    pair<PGRef, PGQueueable> to_run = in_queue.dequeue();
    l.unlock();
#if 0
    {
#ifdef WITH_LTTNG
      osd_reqid_t reqid;
      if (boost::optional<OpRequestRef> _op = op->maybe_get_op()) {
	reqid = (*_op)->get_reqid();
      }
#endif
      tracepoint(osd, opwq_process_start, reqid.name._type,
		 reqid.name._num, reqid.tid, reqid.inc);
    }
#endif

    Ceph::Future<> next;
    to_run.first->lock_suspend_timeout(tp_handle);
    to_run.second.run(osd, to_run.first, tp_handle, &next);
    to_run.first->unlock();

    if (next.ready())
      next.get();

    if (next.valid()) {
      in_progress.emplace_back(
	to_run.first,
	std::move(next));
    }

    // do stuff
    tp_handle.suspend_tp_timeout();
    l.lock();
    tp_handle.reset_tp_timeout();
  }
  running = false;
  stop_cv.notify_one();
  return;
}

void OSDReactor::queue(
  std::pair<PGRef, PGQueueable> &&item)
{
  unique_lock l(lock);
  unsigned priority = item.second.get_priority();
  unsigned cost = item.second.get_cost();

  if (priority >= CEPH_MSG_PRIO_LOW)
    in_queue.enqueue_strict(
      item.second.get_owner(), priority, item);
  else
    in_queue.enqueue(
      item.second.get_owner(),
      priority, cost, item);
  queue_cv.notify_one();
}

void OSDReactor::queue_front(
  std::pair<PGRef, PGQueueable> &&item)
{
  unique_lock l(lock);
  unsigned priority = item.second.get_priority();
  unsigned cost = item.second.get_cost();

  if (priority >= CEPH_MSG_PRIO_LOW)
    in_queue.enqueue_strict_front(
      item.second.get_owner(), priority, item);
  else
    in_queue.enqueue_front(
      item.second.get_owner(),
      priority, cost, item);
  queue_cv.notify_one();
}

struct Pred {
  PG *pg;
  list<OpRequestRef> *out_ops;
  uint64_t reserved_pushes_to_free;
  Pred(PG *pg, list<OpRequestRef> *out_ops = 0)
    : pg(pg), out_ops(out_ops), reserved_pushes_to_free(0) {}
  void accumulate(PGQueueable &op) {
    reserved_pushes_to_free += op.get_reserved_pushes();
    if (out_ops) {
      boost::optional<OpRequestRef> mop = op.maybe_get_op();
      if (mop)
	out_ops->push_back(*mop);
    }
  }
  bool operator()(pair<PGRef, PGQueueable> &op) {
    if (op.first == pg) {
      accumulate(op.second);
      return true;
    } else {
      return false;
    }
  }
  uint64_t get_reserved_pushes_to_free() const {
    return reserved_pushes_to_free;
  }
};

void OSDReactor::dequeue(PG *pg)
{
  unique_lock l(lock);
  assert(pg);

  Pred f(pg);
  in_queue.remove_by_filter(f);
  osd->service.release_reserved_pushes(f.get_reserved_pushes_to_free());
}

void OSDReactor::dequeue_and_get_ops(PG *pg, std::list<OpRequestRef> *dequeued)
{
  unique_lock l(lock);
  assert(pg);

  Pred f(pg, dequeued);
  in_queue.remove_by_filter(f);
  osd->service.release_reserved_pushes(f.get_reserved_pushes_to_free());
}

void OSDReactorPool::start()
{
  num_shards = cct->_conf->osd_op_num_shards;
  shards.reserve(num_shards);
  for (unsigned i = 0; i < num_shards; ++i) {
    std::stringstream name;
    name << "OSDReactorThread<" << i << ">";
    shards.emplace_back(
      new OSDReactor(
	name.str(),
	osd,
	cct));
    shards.back()->start();
  }
}

void OSDReactorPool::stop()
{
  for (auto &&i: shards) {
    i->drain();
    i->join();
  }
  shards.clear();
}

void OSDReactorPool::queue(
  std::pair<PGRef, PGQueueable> &&to_queue)
{
  get_reactor(&*(to_queue.first)).queue(std::move(to_queue));
}

void OSDReactorPool::queue_front(
  std::pair<PGRef, PGQueueable> &&to_queue)
{
  get_reactor(&*(to_queue.first)).queue_front(std::move(to_queue));
}

void OSDReactorPool::dequeue(PG *pg)
{
  get_reactor(pg).dequeue(pg);
}

void OSDReactorPool::dequeue_and_get_ops(
  PG *pg, std::list<OpRequestRef> *dequeued)
{
  get_reactor(pg).dequeue_and_get_ops(pg, dequeued);
}
