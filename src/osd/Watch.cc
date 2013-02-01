// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#include "PG.h"

#include "include/types.h"
#include "messages/MWatchNotify.h"

#include <map>

#include "OSD.h"
#include "ReplicatedPG.h"
#include "Watch.h"

#include "common/config.h"

#define dout_subsys ceph_subsys_osd
#undef dout_prefix
#define dout_prefix _prefix(_dout, this)

static ostream& _prefix(
  std::ostream* _dout,
  Notify *notify) {
  return *_dout << notify->gen_dbg_prefix();
}

Notify::Notify(
  ConnectionRef client,
  unsigned num_watchers,
  bufferlist &payload,
  uint32_t timeout,
  uint64_t cookie,
  uint64_t notify_id,
  uint64_t version,
  OSDService *osd)
  : client(client),
    in_progress_watchers(num_watchers),
    complete(false),
    discarded(false),
    payload(payload),
    timeout(timeout),
    cookie(cookie),
    notify_id(notify_id),
    version(version),
    osd(osd),
    cb(0),
    lock("Notify::lock") {}

NotifyRef Notify::makeNotifyRef(
  ConnectionRef client,
  unsigned num_watchers,
  bufferlist &payload,
  uint32_t timeout,
  uint64_t cookie,
  uint64_t notify_id,
  uint64_t version,
  OSDService *osd) {
  NotifyRef ret(
    new Notify(
      client, num_watchers,
      payload, timeout,
      cookie, notify_id,
      version, osd));
  ret->set_self(ret);
  return ret;
}

class NotifyTimeoutCB : public Context {
  NotifyRef notif;
public:
  NotifyTimeoutCB(NotifyRef notif) : notif(notif) {}
  void finish(int) {
    notif->do_timeout();
  }
};

void Notify::do_timeout()
{
  Mutex::Locker l(lock);
  dout(10) << "timeout" << dendl;
  cb = 0;
  if (discarded || complete)
    return;
  discarded = true;

  in_progress_watchers = 0; // we give up TODO: we should return an error code
  maybe_complete_notify();
}

void Notify::complete_watcher()
{
  Mutex::Locker l(lock);
  dout(10) << "complete_watcher" << dendl;
  if (_should_discard())
    return;
  assert(in_progress_watchers > 0);
  --in_progress_watchers;
  maybe_complete_notify();
}

void Notify::maybe_complete_notify()
{
  dout(10) << "maybe_complete_notify -- "
	   << in_progress_watchers
	   << " in progress watchers " << dendl;
  if (!in_progress_watchers) {
    MWatchNotify *reply(new MWatchNotify(cookie, version, notify_id,
					 WATCH_NOTIFY, payload));
    osd->send_message_osd_client(reply, client.get());
    complete = true;
  }
}

bool Notify::_should_discard()
{
  return discarded || complete;
}

void Notify::discard()
{
  Mutex::Locker l(lock);
  discarded = true;
}

void Notify::init()
{
  Mutex::Locker l(lock);
  maybe_complete_notify();
}

#define dout_subsys ceph_subsys_osd
#undef dout_prefix
#define dout_prefix _prefix(_dout, this)

static ostream& _prefix(
  std::ostream* _dout,
  Watch *watch) {
  return *_dout << watch->gen_dbg_prefix();
}

string Watch::gen_dbg_prefix() {
  stringstream ss;
  ss << pg->gen_prefix() << " -- Watch(" 
     << make_pair(cookie, entity)
     << ") ";
  return ss.str();
}

Watch::Watch(
  ReplicatedPG *pg,
  OSDService *osd,
  ObjectContext *obc,
  uint32_t timeout,
  uint64_t cookie,
  entity_name_t entity)
  : cb(0),
    osd(osd),
    pg(pg),
    obc(obc),
    canceled(false),
    timeout(timeout),
    cookie(cookie),
    entity(entity),
    discarded(false) {
  obc->get();
}

Watch::~Watch() {
  assert(pg->is_locked());
  pg->put_object_context(obc);
}

bool Watch::connected() { return conn; }

class HandleWatchTimeout : public Context {
  WatchRef watch;
public:
  bool canceled;
  HandleWatchTimeout(WatchRef watch) : watch(watch), canceled(true) {}
  void finish(int) { /* not used */ }
  void complete(int) {
    watch->osd->watch_lock.Unlock();
    watch->pg->lock();
    bool done = true;
    if (!watch->isdiscarded() && !canceled)
      done = watch->pg->handle_watch_timeout(watch);
    watch->cb = 0;
    watch->pg->unlock();
    watch->osd->watch_lock.Lock();
    if (done)
      delete this;
  }
};

Context *Watch::get_cb()
{
  return cb;
}

ObjectContext *Watch::get_obc()
{
  obc->get();
  return obc;
}

void Watch::register_cb()
{
  Mutex::Locker l(osd->watch_lock);
  dout(15) << "registering callback" << dendl;
  cb = new HandleWatchTimeout(self.lock());
  osd->watch_timer.add_event_after(
    timeout,
    cb);
}

void Watch::unregister_cb()
{
  dout(15) << "unregister_cb" << dendl;
  if (!cb)
    return;
  dout(15) << "actually registered, cancelling" << dendl;
  cb->canceled = true;
  {
    Mutex::Locker l(osd->watch_lock);
    osd->watch_timer.cancel_event(cb);
  }
  cb = 0;
}

void Watch::connect(ConnectionRef con)
{
  dout(10) << "connecting" << dendl;
  conn = con;
  static_cast<OSD::Session*>(con->get_priv())->wstate.addWatch(self.lock());
  for (map<uint64_t, NotifyRef>::iterator i = in_progress_notifies.begin();
       i != in_progress_notifies.end();
       ++i) {
    send_notify(i->second);
  }
  unregister_cb();
}

void Watch::disconnect()
{
  dout(10) << "disconnect" << dendl;
  conn = ConnectionRef();
  register_cb();
}

void Watch::clear_discarded_notifies()
{
  for (map<uint64_t, NotifyRef>::iterator i = in_progress_notifies.begin();
       i != in_progress_notifies.end();
       ) {
    if (i->second->should_discard())
      in_progress_notifies.erase(i++);
    else
      ++i;
  }
}

void Watch::discard() {
  dout(10) << "discard" << dendl;
  for (map<uint64_t, NotifyRef>::iterator i = in_progress_notifies.begin();
       i != in_progress_notifies.end();
       ) {
    i->second->discard();
  }
  in_progress_notifies.clear();
  unregister_cb();
  discarded = true;
}

bool Watch::isdiscarded()
{
  return discarded;
}

void Watch::remove()
{
  dout(10) << "remove" << dendl;
  for (map<uint64_t, NotifyRef>::iterator i = in_progress_notifies.begin();
       i != in_progress_notifies.end();
       ++i) {
    i->second->complete_watcher();
  }
  in_progress_notifies.clear();
  unregister_cb();
  discarded = true;
  if (conn) {
    static_cast<OSD::Session*>(
      conn->get_priv())->wstate.removeWatch(self.lock());
    conn = ConnectionRef();
  }
}

void Watch::start_notify(NotifyRef notif)
{
  dout(10) << "start_notify " << notif->notify_id << dendl;
  assert(in_progress_notifies.find(notif->notify_id) ==
	 in_progress_notifies.end());
  in_progress_notifies[notif->notify_id] = notif;
  if (connected())
    send_notify(notif);
  clear_discarded_notifies();
}
void Watch::send_notify(NotifyRef notif)
{
  dout(10) << "send_notify" << dendl;
  MWatchNotify *notify_msg = new MWatchNotify(
    cookie, notif->version, notif->notify_id,
    WATCH_NOTIFY, notif->payload);
  osd->send_message_osd_client(notify_msg, conn.get());
}

void Watch::notify_ack(uint64_t notify_id)
{
  dout(10) << "notify_ack" << dendl;
  map<uint64_t, NotifyRef>::iterator i = in_progress_notifies.find(notify_id);
  if (i != in_progress_notifies.end()) {
    i->second->complete_watcher();
    in_progress_notifies.erase(i);
  }
  clear_discarded_notifies();
}

WatchRef Watch::makeWatchRef(
  ReplicatedPG *pg, OSDService *osd,
  ObjectContext *obc, uint32_t timeout, uint64_t cookie, entity_name_t entity)
{
  WatchRef ret(new Watch(pg, osd, obc, timeout, cookie, entity));
  ret->set_self(ret);
  return ret;
}

void WatchConState::addWatch(WatchRef watch)
{
  Mutex::Locker l(lock);
  watches.insert(watch);
}

void WatchConState::removeWatch(WatchRef watch)
{
  Mutex::Locker l(lock);
  watches.erase(watch);
}

void WatchConState::reset()
{
  set<WatchRef> _watches;
  {
    Mutex::Locker l(lock);
    for (set<WWatchRef>::iterator i = watches.begin();
	 i != watches.end();
	 ++i) {
      WatchRef watch(i->lock());
      if (watch)
	_watches.insert(watch);
    }
    watches.clear();
  }
  for (set<WatchRef>::iterator i = _watches.begin();
       i != _watches.end();
       ) {
    boost::intrusive_ptr<ReplicatedPG> pg((*i)->get_pg());
    pg->lock();
    if (!(*i)->isdiscarded()) {
      (*i)->disconnect();
    }
    _watches.erase(i++); // drop reference while holding the pg lock
    pg->unlock();
  }
}
