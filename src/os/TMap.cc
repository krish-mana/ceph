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
  bufferlist bl;
  map<string, bufferlist> attrs;
  int r = read_tmap(path->path(), &bl, &attrs);
  if (r == -EINVAL) {
    bl.clear();
    attrs.clear();
  } else if (r < 0 && r != -ENOENT) {
    return r;
  }
  for (map<string, bufferptr>::const_iterator i = set.begin();
       i != set.end();
       ++i) {
    bufferlist value;
    value.append(i->second);
    attrs.insert(pair<string, bufferlist>(i->first, value));
  }
  return write_tmap(path->path(), bl, attrs);
}

int TMap::set_header(const hobject_t &hoid,
		     CollectionIndex::IndexedPath path,
		     const bufferlist &header)
{
  bufferlist bl;
  map<string, bufferlist> attrs;
  int r = read_tmap(path->path(), &bl, &attrs);
  if (r < 0 && r != -ENOENT)
    return r;
  return write_tmap(path->path(), header, attrs);
}

int TMap::get_header(const hobject_t &hoid,
		     CollectionIndex::IndexedPath path,
		     bufferlist *bl)
{
  map<string, bufferlist> attrs;
  return read_tmap(path->path(), bl, &attrs);
}

int TMap::clear(const hobject_t &hoid,
		CollectionIndex::IndexedPath path)
{
  int fd = ::open(path->path(), O_TRUNC|O_WRONLY, 0);
  if (fd < 0)
    return fd;
  ::close(fd);
  return 0;
}

int TMap::rm_keys(const hobject_t &hoid,
		  CollectionIndex::IndexedPath path,
		  const set<string> &keys)
{
  bufferlist bl;
  map<string, bufferlist> attrs;
  int r = read_tmap(path->path(), &bl, &attrs);
  if (r < 0 && r != -ENOENT)
    return r;
  for (set<string>::const_iterator i = keys.begin();
       i != keys.end();
       ++i) {
    if (attrs.count(*i))
      attrs.erase(*i);
  }
  return write_tmap(path->path(), bl, attrs);
}

int TMap::get(const hobject_t &hoid,
	      CollectionIndex::IndexedPath path,
	      bufferlist *header,
	      map<string, bufferlist> *out)
{
  return read_tmap(path->path(), header, out);
}

int TMap::get_keys(const hobject_t &hoid,
		   CollectionIndex::IndexedPath path,
		   set<string> *keys)
{
  bufferlist bl;
  map<string, bufferlist> attrs;
  int r = read_tmap(path->path(), &bl, &attrs);
  if (r < 0)
    return r;
  for (map<string, bufferlist>::iterator i = attrs.begin();
       i != attrs.end();
       ++i)
    keys->insert(i->first);
  return 0;
}

int TMap::get_values(const hobject_t &hoid,
		     CollectionIndex::IndexedPath path,
		     const set<string> &keys,
		     map<string, bufferlist> *out)
{
  bufferlist bl;
  int r = read_tmap(path->path(), &bl, out);
  if (r < 0)
    return r;
  for (map<string, bufferlist>::iterator i = out->begin();
       i != out->end();
       ) {
    if (!keys.count(i->first))
      out->erase(i++);
    else
      ++i;
  }
  return 0;
}

int TMap::check_keys(const hobject_t &hoid,
		     CollectionIndex::IndexedPath path,
		     const set<string> &keys,
		     set<string> *out)
{
  bufferlist bl;
  map<string, bufferlist> attrs;
  int r = read_tmap(path->path(), &bl, &attrs);
  if (r < 0)
    return r;
  for (set<string>::const_iterator i = keys.begin();
       i != keys.end();
       ++i) {
    if (attrs.count(*i))
      out->insert(*i);
  }
  return 0;
}


