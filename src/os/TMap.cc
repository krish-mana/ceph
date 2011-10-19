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
#include "TMap.h"
#include "common/safe_io.h"
#include "common/errno.h"
#include "include/buffer.h"

static int write_tmap(const char *path,
		      const bufferlist header,
		      const map<string, bufferlist> &map) {
  int r = ::open(path, O_WRONLY|O_TRUNC|O_CREAT, 0);
  if (r < 0)
    return -errno;
  int fd = r;

  bufferlist bl;
  ::encode(header, bl);
  ::encode(map, bl);

  r = bl.write_fd(fd);
  if (r == 0)
    r = bl.length();
  ::close(fd);
  return r;
}

static int read_tmap(const char *path,
		     bufferlist *header,
		     map<string, bufferlist> *map) {
  int r = ::open(path, O_RDONLY, 0);
  if (r < 0)
    return -errno;
  int fd = r;

  struct stat st;
  memset(&st, 0, sizeof(st));
  ::fstat(fd, &st);
  size_t len = st.st_size;

  bufferptr bptr(len);
  int got = safe_pread(fd, bptr.c_str(), len, 0);
  if (got < 0) {
    TEMP_FAILURE_RETRY(::close(fd));
    return got;
  }

  bptr.set_length(got);
  bufferlist bl;
  bl.append(bptr);
  bufferlist::iterator iter = bl.begin();
  try {
    ::decode(*header, iter);
    ::decode(*map, iter);
  } catch (...) {
    return -EINVAL;
  }
  return 0;
}

int TMap::set_keys(const hobject_t &hoid,
		   CollectionIndex::IndexedPath path,
		   const map<string, bufferptr> &set)
{
  return 0;
}

int TMap::set_header(const hobject_t &hoid,
		     CollectionIndex::IndexedPath path,
		     const bufferlist &bl)
{
  return 0;
}

int TMap::get_header(const hobject_t &hoid,
		     CollectionIndex::IndexedPath path,
		     bufferlist *bl)
{
  return 0;
}

int TMap::clear(const hobject_t &hoid,
		CollectionIndex::IndexedPath path)
{
  return 0;
}

int TMap::rm_keys(const hobject_t &hoid,
		  CollectionIndex::IndexedPath path,
		  const set<string> &keys)
{
  return 0;
}

int TMap::get(const hobject_t &hoid,
	      CollectionIndex::IndexedPath path,
	      bufferlist *header,
	      map<string, bufferlist> *out)
{
  return 0;
}

int TMap::get_keys(const hobject_t &hoid,
		   CollectionIndex::IndexedPath path,
		   set<string> *keys)
{
  return 0;
}

int TMap::get_values(const hobject_t &hoid,
		     CollectionIndex::IndexedPath path,
		     const set<string> &keys,
		     map<string, bufferlist> *out)
{
  return 0;
}

int TMap::check_keys(const hobject_t &hoid,
		     CollectionIndex::IndexedPath path,
		     const set<string> &keys,
		     set<string> *out)
{
  return 0;
}


