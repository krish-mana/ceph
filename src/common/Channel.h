// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-

#ifndef CHANNEL_H
#define CHANNEL_H

#include <tr1/memory>
#include <utility>
#include <atomic>

#include "include/atomic.h"
#include "common/Mutex.h"
#include "common/Cond.h"

template <typename T>
class Channel {
  const static unsigned CHUNK_SIZE = 1000;
  class TQueue {
    struct TQueueCell {
      T contents[CHUNK_SIZE];
      ceph::atomic_t first, last;
      std::atomic<TQueueCell*> next;
      TQueueCell() : first(0), last(0), next(0) {}
      bool empty() {
	return first.read() == CHUNK_SIZE;
      }
      bool full() {
	return last.read() == CHUNK_SIZE;
      }
      void push_back(T t) {
	contents[last.read()] = t;
	last.inc();
      }
      T pop_front() {
	T ret = contents[first.read()];
	first.inc();
	contents[first.read()-1] = T();
	return ret;
      }
    };
  public:
    TQueueCell *front, *back;
    TQueue() : front(new TQueueCell), back(front) {}
    T pop_front() {
      assert(!(front->empty()));
      T ret = front->pop_front();
      TQueueCell *next = 0;
      if (front->empty() && (next = front->next.load())) {
	TQueueCell *cur_front = front;
	front = next;
	delete cur_front;
      }
      return ret;
    }
    void push_back(T t) {
      TQueueCell *cur_back = back;
      assert(!(cur_back->full()));
      cur_back->push_back(t);
      if (cur_back->full()) {
	TQueueCell *next = new TQueueCell;
	cur_back->next.store(next);
	back = next;
      }
    }
  };
  TQueue queue;

  Mutex lock;
  Cond cond;
  ceph::atomic_t size;

  Channel() : lock("Channel::lock") {}

  class _In {
    friend class Channel;
    std::tr1::shared_ptr<Channel<T> > parent;
    _In(std::tr1::shared_ptr<Channel<T> > parent) : parent(parent) {}
  public:
    void send(T t) {
      Mutex::Locker l(parent->lock);
      parent->queue.push_back(t);
      parent->cond.Signal();
      parent->size.inc();
    }
  };
  class _Out {
    friend class Channel;
    std::tr1::shared_ptr<Channel<T> > parent;
    unsigned left;
    _Out(std::tr1::shared_ptr<Channel<T> > parent) : parent(parent), left(0) {}
  public:
    T receive() {
      if (!left) {
	left = parent->size.read();
	if (!left) {
	  Mutex::Locker l(parent->lock);
	  while (!(left = parent->size.read()))
	    parent->cond.Wait(parent->lock);
	}
	parent->size.sub(left);
      }
      T ret = parent->queue.pop_front();
      --left;
      return ret;
    }
  };
  friend class _In;
  friend class _Out;

public:
  typedef std::tr1::shared_ptr<_In> In;
  typedef std::tr1::shared_ptr<_Out> Out;

  static std::pair<In, Out> make_channel() {
    std::tr1::shared_ptr<Channel<T> > chan(new Channel());
    return make_pair(In(new _In(chan)), Out(new _Out(chan)));
  }
};

#endif
