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

#ifndef ECUTIL_H
#define ECUTIL_H

#include <map>
#include <set>

#include "ErasureCodeInterface.h"
#include "include/buffer.h"
#include "include/assert.h"

namespace ECUtil {

inline int decode_helper(
  ErasureCodeInterfaceRef &ecimpl,
  const map<int, bufferlist> &chunks,
  bufferlist *decoded) {
  set<int> want_to_read;
  for (unsigned int i = 0;
       i < 1/*get_data_chunk_count()*/;
       i++) {
    want_to_read.insert(i);
  }
  map<int, bufferlist> decoded_map;
  int r = ecimpl->decode(want_to_read, chunks, &decoded_map);
  if (r)
    return r;
  for (unsigned int i = 0;
       i < 1/*get_data_chunk_count()*/;
       i++) {
    decoded->claim_append(decoded_map[i]);
  }
  return 0;
}

inline int decode(
  uint64_t stripe_size,
  uint64_t stripe_width,
  ErasureCodeInterfaceRef &ec_impl,
  map<int, bufferlist> &to_decode,
  bufferlist *out) {
  assert(to_decode.size());
  uint64_t obj_size = to_decode.begin()->second.length();
  assert(obj_size % stripe_size == 0);
  assert(obj_size > 0);
  for (map<int, bufferlist>::iterator i = to_decode.begin();
       i != to_decode.end();
       ++i) {
    assert(i->second.length() == obj_size);
  }
  for (uint64_t i = 0; i < obj_size; i += stripe_size) {
    map<int, bufferlist> chunks;
    for (map<int, bufferlist>::iterator j = to_decode.begin();
	 j != to_decode.end();
	 ++j) {
      chunks[j->first].substr_of(j->second, i, i+stripe_size);
    }
    bufferlist bl;
    int r = decode_helper(ec_impl, chunks, &bl);
    assert(bl.length() == stripe_width);
    assert(r == 0);
    out->claim_append(bl);
  }
  return 0;
}

inline int encode(
  uint64_t stripe_size,
  uint64_t stripe_width,
  ErasureCodeInterfaceRef &ec_impl,
  bufferlist &in,
  const set<int> &want,
  map<int, bufferlist> *out) {
  uint64_t logical_size = in.length();
  assert(logical_size % stripe_width == 0);
  assert(logical_size > 0);
  for (uint64_t i = 0; i < logical_size; i += stripe_width) {
    map<int, bufferlist> encoded;
    int r = ec_impl->encode(want, in.substr(i, i + stripe_width), &encoded);
    assert(r == 0);
    for (map<int, bufferlist>::iterator i = encoded.begin();
	 i != encoded.end();
	 ++i) {
      (*out)[i->first].claim_append(i->second);
    }
  }
  return 0;
}

inline uint64_t logical_to_prev_stripe_bound_obj(
  uint64_t stripe_size,
  uint64_t stripe_width,
  uint64_t logical_offset) {
  return (logical_offset / stripe_width) * stripe_size;
}

inline uint64_t logical_to_next_stripe_bound_obj(
  uint64_t stripe_size,
  uint64_t stripe_width,
  uint64_t logical_offset) {
  return ((logical_offset + stripe_width - 1)/ stripe_width) * stripe_size;
}


inline uint64_t obj_bound_to_logical_offset(
  uint64_t stripe_size,
  uint64_t stripe_width,
  uint64_t obj_offset) {
  assert(obj_offset % stripe_size == 0);
  return (obj_offset / stripe_size) * stripe_width;
}

};
#endif
