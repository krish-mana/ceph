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

#include <iostream>
#include <sstream>
#include <errno.h>
#include <signal.h>
#include "osd/ECBackend.h"
#include "gtest/gtest.h"

class ECTransactionDepsTest : public ::testing::Test {
protected:
  ECTransaction t;
  const hobject_t hoid;
  const hobject_t hoid2;
  map<hobject_t, uint64_t> must_read;
  set<hobject_t> writes;
  ECTransactionDepsTest()
  : hoid(sobject_t("as", 1)), hoid2(sobject_t("asdf", 0)) {}
};

void test_deps(
  uint64_t stripe_size,
  const ECTransaction &t,
  const map<hobject_t, uint64_t> &must_read,
  const set<hobject_t> &writes)
{
  map<hobject_t, uint64_t> cand_must_read;
  set<hobject_t> cand_writes;
  stringstream ss;
  t.populate_deps(stripe_size, &cand_must_read, &cand_writes, &ss);
  std::cout << ss.str() << std::endl;
  ASSERT_EQ(must_read, cand_must_read);
  ASSERT_EQ(writes, cand_writes);
}

bufferlist get_bl(uint64_t size) {
  bufferlist bl;
  for (uint64_t i = 0; i < size; ++i) {
    bl.append('a');
  }
  return bl;
}

static const uint64_t STRIPE_SIZE = 50;

TEST_F(ECTransactionDepsTest, Deps1) {
  bufferlist bl = get_bl(2*STRIPE_SIZE);
  t.append(hoid, 0, bl.length(), bl);

  writes.insert(hoid);

  test_deps(STRIPE_SIZE, t, must_read, writes);
}
TEST_F(ECTransactionDepsTest, Deps2) {
  bufferlist bl = get_bl(2*STRIPE_SIZE);
  t.append(hoid, 1, bl.length(), bl);

  must_read[hoid] = 0;

  writes.insert(hoid);

  test_deps(STRIPE_SIZE, t, must_read, writes);
}
TEST_F(ECTransactionDepsTest, Deps3) {
  bufferlist bl = get_bl(STRIPE_SIZE * 2);
  t.append(hoid, 1, bl.length(), bl);
  t.append(hoid, (2*STRIPE_SIZE) + 1, bl.length(), bl);

  must_read[hoid] = 0;

  writes.insert(hoid);

  test_deps(STRIPE_SIZE, t, must_read, writes);
}
TEST_F(ECTransactionDepsTest, Deps4) {
  bufferlist bl = get_bl(STRIPE_SIZE);
  t.append(hoid, 1, bl.length(), bl);
  t.append(hoid, (2*STRIPE_SIZE) + 1, bl.length(), bl);
  t.append(hoid2, (2*STRIPE_SIZE) + 1, bl.length(), bl);

  must_read[hoid] = 0;
  must_read[hoid2] = 2*STRIPE_SIZE;

  writes.insert(hoid);
  writes.insert(hoid2);

  test_deps(STRIPE_SIZE, t, must_read, writes);
}
TEST_F(ECTransactionDepsTest, Deps5) {
  bufferlist bl = get_bl(STRIPE_SIZE);;
  t.append(hoid, 1, bl.length(), bl);
  t.clone(hoid, hoid2);
  t.append(hoid2, (2*STRIPE_SIZE) + 1, bl.length(), bl);
  t.append(hoid, 1 + STRIPE_SIZE, bl.length(), bl);

  must_read[hoid] = 0;

  writes.insert(hoid);
  writes.insert(hoid2);

  test_deps(STRIPE_SIZE, t, must_read, writes);
}
TEST_F(ECTransactionDepsTest, Deps6) {
  bufferlist bl = get_bl(STRIPE_SIZE);;
  t.append(hoid, 1, bl.length(), bl);
  t.rename(hoid, hoid2);
  t.append(hoid2, (2*STRIPE_SIZE) + 1, bl.length(), bl);
  t.append(hoid, 0, bl.length(), bl);

  must_read[hoid] = 0;

  writes.insert(hoid);
  writes.insert(hoid2);

  test_deps(STRIPE_SIZE, t, must_read, writes);
}
TEST_F(ECTransactionDepsTest, Deps7) {
  bufferlist bl = get_bl(STRIPE_SIZE);;
  t.append(hoid, 1, bl.length(), bl);
  t.rename(hoid, hoid2);
  t.append(hoid2, (2*STRIPE_SIZE) + 1, bl.length(), bl);
  t.append(hoid, 0, bl.length(), bl);

  must_read[hoid] = 0;

  writes.insert(hoid);
  writes.insert(hoid2);

  test_deps(STRIPE_SIZE, t, must_read, writes);
}
