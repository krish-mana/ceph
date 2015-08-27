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

#ifndef CEPH_OS_MPSC_QUEUE_H
#define CEPH_OS_MPSC_QUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
#include <boost/optional.hpp>

/**
 * Multi Producer Single Consumer Queue
 *
 * Placeholder implementation, make faster later.  Intentionally unbounded,
 * user should make sure usage is throttled.
 */
template<typename T>
class MPSCQueue {
  std::mutex lock;
  typedef std::unique_lock<std::mutex> unique_lock;
  std::condition_variable cv;
  std::deque<T> q;
public:
  void push(T &&t) {
    {
      unique_lock g(lock);
      q.push_back(t);
    }
    cv.notify_one();
  }
  void push(const T &t) {
    {
      unique_lock g(lock);
      q.push_back(t);
      if (q.size() != 1)
	return;
    }
    cv.notify_one();
  }
  T pop() {
    unique_lock g(lock);
    cv.wait(lock, [this](){ return !q.empty(); });
    return q.pop_front();
  }
  boost::optional<T> try_pop() {
    unique_lock g(lock);
    return q.empty() ? boost::optional<T>() : q.pop_front();
  }
};

#endif
