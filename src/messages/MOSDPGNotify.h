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

#ifndef CEPH_MOSDPGPEERNOTIFY_H
#define CEPH_MOSDPGPEERNOTIFY_H

#include "msg/Message.h"

#include "osd/osd_types.h"

/*
 * PGNotify - notify primary of my PGs and versions.
 */

class MOSDPGNotify : public Message {

  static const int HEAD_VERSION = 3;
  static const int COMPAT_VERSION = 1;

  epoch_t epoch;
  /// query_epoch is the epoch of the query being responded to, or
  /// the current epoch if this is not being sent in response to a
  /// query. This allows the recipient to disregard responses to old
  /// queries.
  vector<pg_notify_t> pg_list;   // pgid -> version

 public:
  version_t get_epoch() { return epoch; }
  vector<pg_notify_t>& get_pg_list() { return pg_list; }

  MOSDPGNotify()
    : Message(MSG_OSD_PG_NOTIFY, HEAD_VERSION, COMPAT_VERSION) { }
  MOSDPGNotify(epoch_t e, vector<pg_notify_t>& l)
    : Message(MSG_OSD_PG_NOTIFY, HEAD_VERSION, COMPAT_VERSION),
      epoch(e) {
    pg_list.swap(l);
  }
private:
  ~MOSDPGNotify() {}

public:  
  const char *get_type_name() const { return "PGnot"; }

  void encode_payload(uint64_t features) {
    ::encode(epoch, payload);
    ::encode(pg_list, payload);
  }
  void decode_payload() {
    vector<pg_info_t> infos;
    epoch_t query_epoch;
    bufferlist::iterator p = payload.begin();
    ::decode(epoch, p);
    if (header.version < 3) {
      ::decode(infos, p);
      if (header.version >= 2) {
	::decode(query_epoch, p);
      } else {
	query_epoch = epoch;
      }
      for (vector<pg_info_t>::iterator i = infos.begin();
	   i != infos.end();
	   ++i) {
	pg_list.push_back(pg_notify_t(query_epoch, epoch, *i));
      }
    } else {
      ::decode(pg_list, p);
    }
  }
  void print(ostream& out) const {
    out << "pg_notify(";
    for (vector<pg_notify_t>::const_iterator i = pg_list.begin();
         i != pg_list.end();
         ++i) {
      if (i != pg_list.begin())
	out << ",";
      out << i->info.pgid;
    }
    out << " epoch " << epoch
	<< ")";
  }
};

#endif
