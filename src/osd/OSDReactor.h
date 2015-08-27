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

#include "common/mpsc_queue.h"
#include "common/PrioritizedQueue.h"
#include "PG.h"
#include "os/ObjectStore.h"

class OSD;

/**
 * OSDReactor
 *
 * Handles multiplexing multiple concurrent PG tasks with (possibly) async
 * io operations with new queued operations
 */
class OSDReactor : ObjectStore::CompletionQPort {
  OSD *osd;

  std::mutex lock;
  std::condition_variable cv;
  typedef std::unique_lock<std::mutex> unique_lock;

  PrioritizedQueue< pair<PGRef, PGQueueable>, entity_inst_t> in_queue;

  std::deque< pair<std::function<void(int)> *, int> > runqueue;
  unsigned active;

  std::thread t;
public:
  OSDReactor(OSD *osd);

  void run();

  void start_operation(unsigned num) override final;
  void push(void *completion, int result) override final;
};

#endif
