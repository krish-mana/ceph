// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 Red Hat
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef OSD_REACTOR_H
#define OSD_REACTOR_H

#include <functional>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

#include "common/PrioritizedQueue.h"
#include "common/Future.h"
#include "PG.h"
#include "os/ObjectStore.h"

class OSD;

/**
 * OSDReactor
 *
 * Handles multiplexing multiple concurrent PG tasks with (possibly) async
 * io operations with new queued operations
 */
class OSDReactor {
  const string thread_name;
  OSD *osd;
  CephContext *cct;

  std::mutex lock;
  std::condition_variable queue_cv;
  std::condition_variable drain_cv;
  std::condition_variable stop_cv;
  bool stop;
  bool running;
  typedef std::unique_lock<std::mutex> unique_lock;

  PrioritizedQueue<pair<PGRef, PGQueueable>, entity_inst_t> in_queue;

  std::list<pair<PGRef, Ceph::Future<>>> in_progress;

  std::thread t;

  void _run();
public:
  OSDReactor(
    const string &thread_name,
    OSD *osd,
    CephContext *cct);

  void drain();
  void start();
  void join();
  bool empty();
  void queue(std::pair<PGRef, PGQueueable> &&to_queue);
  void queue_front(std::pair<PGRef, PGQueueable> &&to_queue);
  void dequeue(PG *pg);
  void dequeue_and_get_ops(PG *pg, std::list<OpRequestRef> *out);
  const char *get_name() const {
    return thread_name.c_str();
  }
  void dump(Formatter *f) {
    unique_lock l(lock);
    in_queue.dump(f);
  }
};

class OSDReactorPool {
  unsigned num_shards;
  std::vector<std::unique_ptr<OSDReactor>> shards;
  OSD *osd;
  CephContext *cct;

  OSDReactor &get_reactor(PG *pg) {
    assert(num_shards);
    return *(shards[pg->get_pgid().ps() % num_shards]);
  }
public:
  OSDReactorPool(OSD *osd, CephContext *cct) :
    num_shards(0), osd(osd), cct(cct) {}
  void queue(std::pair<PGRef, PGQueueable> &&to_queue);
  void queue_front(std::pair<PGRef, PGQueueable> &&to_queue);
  void dequeue(PG *pg);
  void dequeue_and_get_ops(PG *pg, std::list<OpRequestRef> *out);
  void start();
  void stop();
  void dump(Formatter *f) {
    for (auto &&i: shards) {
      f->open_object_section(i->get_name());
      i->dump(f);
      f->close_section();
    }
  }
};


#endif
