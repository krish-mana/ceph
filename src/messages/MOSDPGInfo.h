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


#ifndef CEPH_MOSDPGINFO_H
#define CEPH_MOSDPGINFO_H

#include "msg/Message.h"
#include "osd/osd_types.h"

class MOSDPGInfo : public Message {
  epoch_t epoch;
  static const int HEAD_VERSION = 2;
  static const int COMPAT_VERSION = 1;

public:
  vector<pg_notify_t> pg_info;

  epoch_t get_epoch() { return epoch; }

  MOSDPGInfo() : Message(MSG_OSD_PG_INFO,
			 HEAD_VERSION,
			 COMPAT_VERSION) {}
  MOSDPGInfo(version_t mv) :
    Message(MSG_OSD_PG_INFO,
	    HEAD_VERSION,
	    COMPAT_VERSION),
    epoch(mv) { }
private:
  ~MOSDPGInfo() {}

public:
  const char *get_type_name() const { return "pg_info"; }
  void print(ostream& out) const {
    out << "pg_info(" << pg_info.size() << " pgs e" << epoch << ")";
  }

  void encode_payload(uint64_t features) {
    ::encode(epoch, payload);
    ::encode(pg_info, payload);
  }
  void decode_payload() {
    bufferlist::iterator p = payload.begin();
    ::decode(epoch, p);
    if (header.version < 2) {
      vector<pg_info_t> infos;
      ::decode(infos, p);
      for (vector<pg_info_t>::iterator i = infos.begin();
	   i != infos.end();
	   ++i) {
	pg_info.push_back(pg_notify_t(epoch, epoch, *i));
      }
    } else {
      ::decode(pg_info, p);
    }
  }
};

#endif
