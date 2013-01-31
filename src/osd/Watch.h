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
#ifndef CEPH_WATCH_H
#define CEPH_WATCH_H

#include <boost/intrusive_ptr.hpp>
#include <tr1/memory>
#include <set>

#include "msg/Messenger.h"
#include "include/Context.h"
#include "common/Mutex.h"

enum WatcherState {
  WATCHER_PENDING,
  WATCHER_NOTIFIED,
};

class OSDService;
class PG;
void intrusive_ptr_add_ref(PG *pg);
void intrusive_ptr_release(PG *pg);
class ObjectContext;
class MWatchNotify;

class Watch;
typedef std::tr1::shared_ptr<Watch> WatchRef;
typedef std::tr1::weak_ptr<Watch> WWatchRef;

class Notify;
typedef std::tr1::shared_ptr<Notify> NotifyRef;
typedef std::tr1::weak_ptr<Notify> WNotifyRef;

/**
 * Notify tracks the progress of a particular notify
 *
 * References are held by Watch and the timeout callback
 */
class NotifyTimeoutCB;
class Notify {
  friend class NotifyTimeoutCB;
  friend class Watch;
  WNotifyRef self;
  ConnectionRef client;
  unsigned in_progress_watchers;
  bool complete;
  bool discarded;

  bufferlist payload;
  uint32_t timeout;
  uint64_t cookie;
  uint64_t notify_id;
  uint64_t version;

  OSDService *osd;
  Context *cb;
  Mutex lock;
  void maybe_complete_notify();
  void do_timeout();
  Notify(
    ConnectionRef client,
    unsigned num_watchers,
    bufferlist &payload,
    uint32_t timeout,
    uint64_t cookie,
    uint64_t notify_id,
    uint64_t version,
    OSDService *osd);
public:
  void set_self(NotifyRef _self) {
    self = _self;
  }
  static NotifyRef makeNotifyRef(
    ConnectionRef client,
    unsigned num_watchers,
    bufferlist &payload,
    uint32_t timeout,
    uint64_t cookie,
    uint64_t notify_id,
    uint64_t version,
    OSDService *osd);

  void init();
  void complete_watcher();
  void discard();
  bool should_discard();
};
/**
 * Watch is a mapping between a Connection and an ObjectContext
 *
 * References are held by ObjectContext and the timeout callback
 */
class HandleWatchTimeout;
class Watch {
  WWatchRef self;
  friend class HandleWatchTimeout;
  ConnectionRef conn;
  HandleWatchTimeout *cb;

  OSDService *osd;
  boost::intrusive_ptr<PG> pg;
  ObjectContext *obc;

  std::map<uint64_t, NotifyRef> in_progress_notifies;

  bool canceled;
  uint32_t timeout;
  uint64_t cookie;
  entity_name_t entity;
  bool discarded;

  Watch(
    PG *pg, OSDService *osd,
    ObjectContext *obc, uint32_t timeout,
    uint64_t cookie, entity_name_t entity);
  void clear_discarded_notifies();
  void register_cb();
  void unregister_cb();
  void send_notify(NotifyRef notif);
public:
  static WatchRef makeWatchRef(
    PG *pg, OSDService *osd,
    ObjectContext *obc, uint32_t timeout, uint64_t cookie, entity_name_t entity);
  void set_self(WatchRef _self) {
    self = _self;
  }

  ObjectContext *get_obc();
  uint64_t get_cookie() const { return cookie; }
  entity_name_t get_entity() const { return entity; }
  Context *get_cb();

  void lock_pg();
  void unlock_pg();

  bool connected();
  void connect(ConnectionRef con);
  void disconnect();

  void discard();
  bool isdiscarded();
  void remove();

  void start_notify(uint64_t notify_id, NotifyRef notif);
  void notify_ack(uint64_t notify_id);
};

/**
 * Holds weak refs to Watch structures
 */
class WatchConState {
  Mutex lock;
  std::set<WWatchRef> watches;
public:
  WatchConState() : lock("WatchConState") {}
  void addWatch(WatchRef watch);
  void removeWatch(WatchRef watch);
  void reset();
};

#endif
