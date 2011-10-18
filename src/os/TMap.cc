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

int TMap::set_keys(const hobject_t &hoid,
		   CollectionIndex::IndexedPath path,
		   const map<string, bufferptr> &set)
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


