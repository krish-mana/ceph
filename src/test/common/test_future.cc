// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 Red Hat <contact@redhat.com>
 *
 * LGPL2.1 (see COPYING-LGPL2.1) or later
 */

#include "common/Future.h"

#include "gtest/gtest.h"

using namespace Ceph;

TEST(Future, basic)
{
  {
    Future<int> fut;
    Promise<int> p(fut.get_promise());
    ASSERT_FALSE(fut.ready());
    p.fulfill(1);
    ASSERT_TRUE(fut.ready());
    ASSERT_TRUE(fut.valid());
    ASSERT_FALSE(p.valid());
    ASSERT_EQ(fut.get(), 1);
    ASSERT_FALSE(fut.valid());
    ASSERT_FALSE(p.valid());
  }

  {
    Future<int> fut;
    Promise<int> p(fut.get_promise());
    Future<int> fut2 = fut.then([](int in) { return in + 1; });
    ASSERT_FALSE(fut.valid());
    ASSERT_TRUE(fut2.valid());
    ASSERT_TRUE(p.valid());
    ASSERT_TRUE(fut2.blocked());
    p.fulfill(1);
    ASSERT_FALSE(fut2.blocked());
    ASSERT_FALSE(fut2.ready());
    fut2.run_until_blocked_or_ready();
    ASSERT_FALSE(fut2.blocked());
    ASSERT_TRUE(fut2.ready());
    ASSERT_FALSE(p.valid());
    ASSERT_EQ(fut2.get(), 2);
    ASSERT_FALSE(fut2.valid());
  }

  {
    Future<int> fut;
    Promise<int> p(fut.get_promise());
    Future<int> fut2 = fut.then([](int in) { return in + 10; });
    ASSERT_FALSE(fut.valid());
    ASSERT_TRUE(fut2.valid());

    Promise<int> p2;
    Promise<int> *_p2 = &p2;
    Future<int> fut3 = fut2.then([=](int in) mutable {
	Future<int> ret;
	*_p2 = std::move(ret.get_promise());
	Future<int> ret2(ret.then([=](int in2) { return in + in2; }));
	return ret2;
      });
    ASSERT_FALSE(fut2.valid());
    ASSERT_TRUE(fut3.valid());

    ASSERT_TRUE(p.valid());
    ASSERT_FALSE(p2.valid());

    ASSERT_TRUE(fut3.blocked());
    p.fulfill(1);
    ASSERT_FALSE(fut3.blocked());
    ASSERT_FALSE(fut3.ready());
    fut3.run_until_blocked_or_ready();
    ASSERT_TRUE(fut3.blocked());
    ASSERT_FALSE(fut3.ready());
    ASSERT_FALSE(p.valid());
    ASSERT_TRUE(p2.valid());
    p2.fulfill(100);
    ASSERT_FALSE(p2.valid());
    ASSERT_FALSE(fut3.blocked());

    fut3.run_until_blocked_or_ready();
    ASSERT_FALSE(fut3.blocked());
    ASSERT_TRUE(fut3.ready());

    ASSERT_EQ(fut3.get(), 111);
    ASSERT_FALSE(fut3.valid());
  }
}
