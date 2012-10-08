// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#ifndef CEPH_HASHWQ_H
#define CEPH_HASHWQ_H

#include "Mutex.h"
#include "Cond.h"
#include "Thread.h"
#include "common/config_obs.h"

class CephContext;

/**
 * Efficiently manages N queues of type T partitioned by
 * T t -> C c; c(t) % N
 */
template <typename T, typename C, class K>
class HashWQ {
  struct Worker : public Thread {
    CephContext *cct;
    Mutex lock;
    Cond cond;
    bool stopping;
    bool stopped;
    unsigned tid;
    list<T> queue;
    const unsigned timeout_interval;
    const unsigned suicide_interval;
    K k;
    Worker(HashWQ *pool, unsigned tid) :
      cct(pool->cct),
      lock("Worker"),
      stopping(0), stopped(0), tid(tid),
      timeout_interval(pool->timeout_interval),
      suicide_interval(pool->suicide_interval),
      k(pool->k) {}
    void *entry() {
#if 0
      std::stringstream ss;
      ss << pool->name << " thread " << (void*)pthread_self();
#endif
      while (1) {
	T next;
	{
	  Mutex::Locker l(lock);
	  while (queue.empty()) {
	    if (stopping) {
	      stopped = true;
	      cond.Signal();
	      return 0;
	    }
	    cond.Wait(lock);
	  }
	  next = queue.front();
	  queue.pop_front();
	}
	k(next);
      }
      return 0;
    }
    void stop() {
      Mutex::Locker l(lock);
      stopping = true;
      cond.Signal();
      while (!stopped)
	cond.Wait(lock);
      join();
    }
  };
  friend class Worker;

  CephContext *cct;
  bool running;
  string name;
  vector<Worker*> workers;
  const unsigned timeout_interval;
  const unsigned suicide_interval;
  C c;
  K k;
    
public:
  HashWQ(
    CephContext *cct, string name, unsigned num_threads,
    unsigned timeout_interval, unsigned suicide_interval,
    K k) :
    cct(cct),
    running(0),
    name(name),
    timeout_interval(timeout_interval),
    suicide_interval(suicide_interval),
    k(k) {
    for (unsigned i = 0; i < num_threads; ++i) {
      workers.push_back(new Worker(this, i));
    }
  }
  ~HashWQ() {
    for (typename vector<Worker*>::iterator i = workers.begin();
	 i != workers.end();
	 ++i) {
      delete *i;
    }
    workers.clear();
  }
  void queue(T t) {
    unsigned n = c(t) % workers.size();
    {
      Mutex::Locker l(workers[n]->lock);
      workers[n]->queue.push_back(t);
      workers[n]->cond.Signal();
    }
  }
  void start() {
    for (typename vector<Worker*>::iterator i = workers.begin();
	 i != workers.end();
	 ++i) {
      (*i)->create();
    }
  }
  void stop() {
    for (typename vector<Worker*>::iterator i = workers.begin();
	 i != workers.end();
	 ++i) {
      (*i)->stop();
    }
  }
};

#endif
