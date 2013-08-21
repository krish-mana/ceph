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
  dout(10) << __func__ << dendl;
  return false;
}

void ReplicatedBackend::on_change(ObjectStore::Transaction *t)
{
  dout(10) << __func__ << dendl;
}
