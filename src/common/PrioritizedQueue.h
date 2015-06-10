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

#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include "common/Formatter.h"
#include "common/RefCountedObj.h"
#include "include/xlist.h"

#include <map>
#include <utility>
#include <list>
#include <algorithm>
#include <boost/intrusive_ptr.hpp>

/**
 * Manages queue for normal and strict priority items
 *
 * On dequeue, the queue will select the lowest priority queue
 * such that the q has bucket > cost of front queue item.
 *
 * If there is no such queue, we choose the next queue item for
 * the highest priority queue.
 *
 * Before returning a dequeued item, we place into each bucket
 * cost * (priority/total_priority) tokens.
 *
 * enqueue_strict and enqueue_strict_front queue items into queues
 * which are serviced in strict priority order before items queued
 * with enqueue and enqueue_front
 *
 * Within a priority class, we schedule round robin based on the class
 * of type K used to enqueue items.  e.g. you could use entity_inst_t
 * to provide fairness for different clients.
 */
template <typename T, typename K>
class PrioritizedQueue {
  uint64_t total_priority;
  uint64_t max_tokens_per_subqueue;
  uint64_t min_cost;

public:
  class QItem;
  typedef boost::intrusive_ptr<QItem> QItemRef;
private:

  typedef xlist<QItem*> ElemList;

  template <class F>
  static unsigned filter_list_pairs(
    ElemList *l, F f,
    std::list<T> *out) {
    unsigned ret = 0;
    if (out) {
      for (typename ElemList::reverse_iterator i = l->rbegin();
	   i != l->rend();
	   ++i) {
	if (f((*i)->item)) {
	  out->push_front((*i)->item);
	}
      }
    }
    for (typename ElemList::iterator i = l->begin();
	 i != l->end();
      ) {
      if (f((*i)->item)) {
	QItemRef to_release(*i, false /* take ref ownership */);
	l->remove(*i++);
	to_release->dequeued();
	++ret;
      } else {
	++i;
      }
    }
    return ret;
  }

  struct SubQueue {
  private:
    typedef std::map<K, ElemList> Classes;
    Classes q;
    uint64_t tokens, max_tokens;
    uint64_t size;
    typename Classes::iterator cur;
  public:
    SubQueue(const SubQueue &other)
      : q(other.q),
	tokens(other.tokens),
	max_tokens(other.max_tokens),
	size(other.size),
	cur(q.begin()) {}
    SubQueue()
      : tokens(0),
	max_tokens(0),
	size(0), cur(q.begin()) {}
    void set_max_tokens(unsigned mt) {
      max_tokens = mt;
    }
    unsigned get_max_tokens() const {
      return max_tokens;
    }
    unsigned num_tokens() const {
      return tokens;
    }
    void put_tokens(uint64_t t) {
      tokens += t;
      if (tokens > max_tokens)
	tokens = max_tokens;
    }
    void take_tokens(uint64_t t) {
      if (tokens > t)
	tokens -= t;
      else
	tokens = 0;
    }
    void enqueue(QItem *item);
    void enqueue_front(QItem *item);

    QItemRef front() const {
      assert(!(q.empty()));
      assert(cur != q.end());
      return cur->second.front();
    }

    QItemRef pop_front() {
      assert(!(q.empty()));
      assert(cur != q.end());
      QItemRef qitem(cur->second.front(), false /* take reference */);
      cur->second.pop_front();
      if (cur->second.empty())
	q.erase(cur++);
      else
	++cur;
      if (cur == q.end())
	cur = q.begin();
      assert(size > 0);
      size--;
      qitem->dequeued();
      return qitem;
    }

    unsigned length() const {
      return size;
    }
    bool empty() const {
      return q.empty();
    }
    template <class F>
    void remove_by_filter(F f, std::list<T> *out) {
      for (typename Classes::iterator i = q.begin();
	   i != q.end();
	   ) {
	unsigned rsize = filter_list_pairs(&(i->second), f, out);
	assert(size >= rsize);
	size -= rsize;
	if (i->second.empty()) {
	  if (cur == i)
	    ++cur;
	  q.erase(i++);
	} else {
	  ++i;
	}
      }
      if (cur == q.end())
	cur = q.begin();
    }
    void remove_by_class(K k, std::list<T> *out) {
      typename Classes::iterator i = q.find(k);
      if (i == q.end())
	return;
      assert(size >= i->second.size());
      size -= i->second.size();
      if (i == cur)
	++cur;
      for (typename ElemList::reverse_iterator j =
	     i->second.rbegin();
	     j != i->second.rend();
	   ) {
	if (out) {
	  out->push_front((*j)->item);
	}
	i->second.remove(*j++);
	(*j)->dequeued();
	(*j)->put();
      }
      q.erase(i);
      if (cur == q.end())
	cur = q.begin();
    }

    void dump(Formatter *f) const {
      f->dump_int("tokens", tokens);
      f->dump_int("max_tokens", max_tokens);
      f->dump_int("size", size);
      f->dump_int("num_keys", q.size());
      if (!empty())
	f->dump_int("first_item_cost", front()->cost);
    }
  };

  typedef std::map<unsigned, SubQueue> SubQueues;

public:
  class QItem : private xlist<QItem*>::item, public RefCountedObject {
    friend class PrioritizedQueue;
    T item;
    unsigned priority;
    uint64_t cost;
    K klass;

    QItem(
      T item,
      unsigned priority,
      uint64_t cost,
      K k)
      : xlist<QItem*>::item(this),
	item(item),
	priority(priority),
	cost(cost),
	klass(k) {}
  public:
    bool is_queued() const { return xlist<QItem*>::get_list(); }
    void dequeued() {}
  };

private:
  SubQueues high_queue;
  SubQueues queue;

  SubQueue *create_queue(unsigned priority) {
    typename SubQueues::iterator p = queue.find(priority);
    if (p != queue.end())
      return &p->second;
    total_priority += priority;
    SubQueue *sq = &queue[priority];
    sq->set_max_tokens(max_tokens_per_subqueue);
    return sq;
  }

  void remove_queue(unsigned priority) {
    assert(queue.count(priority));
    queue.erase(priority);
    assert(total_priority >= priority);
    total_priority -= priority;
  }

  void distribute_tokens(uint64_t cost) {
    if (total_priority == 0)
      return;
    for (typename SubQueues::iterator i = queue.begin();
	 i != queue.end();
	 ++i) {
      i->second.put_tokens(((i->first * cost) / total_priority) + 1);
    }
  }

public:
  PrioritizedQueue(uint64_t max_per, uint64_t min_c)
    : total_priority(0),
      max_tokens_per_subqueue(max_per),
      min_cost(min_c)
  {}

  unsigned length() const {
    unsigned total = 0;
    for (typename SubQueues::const_iterator i = queue.begin();
	 i != queue.end();
	 ++i) {
      assert(i->second.length());
      total += i->second.length();
    }
    for (typename SubQueues::const_iterator i = high_queue.begin();
	 i != high_queue.end();
	 ++i) {
      assert(i->second.length());
      total += i->second.length();
    }
    return total;
  }

  template <class F>
  void remove_by_filter(F f, std::list<T> *removed = 0) {
    for (typename SubQueues::iterator i = queue.begin();
	 i != queue.end();
	 ) {
      unsigned priority = i->first;
      
      i->second.remove_by_filter(f, removed);
      if (i->second.empty()) {
	++i;
	remove_queue(priority);
      } else {
	++i;
      }
    }
    for (typename SubQueues::iterator i = high_queue.begin();
	 i != high_queue.end();
	 ) {
      i->second.remove_by_filter(f, removed);
      if (i->second.empty()) {
	high_queue.erase(i++);
      } else {
	++i;
      }
    }
  }

  void remove_by_class(K k, std::list<T> *out = 0) {
    for (typename SubQueues::iterator i = queue.begin();
	 i != queue.end();
	 ) {
      i->second.remove_by_class(k, out);
      if (i->second.empty()) {
	unsigned priority = i->first;
	++i;
	remove_queue(priority);
      } else {
	++i;
      }
    }
    for (typename SubQueues::iterator i = high_queue.begin();
	 i != high_queue.end();
	 ) {
      i->second.remove_by_class(k, out);
      if (i->second.empty()) {
	high_queue.erase(i++);
      } else {
	++i;
      }
    }
  }

  QItemRef enqueue_strict(K cl, unsigned priority, T item) {
    QItem *it = new QItem(item, priority, 0, cl);
    high_queue[priority].enqueue(it);
    return QItemRef(it);
  }

  QItemRef enqueue_strict_front(K cl, unsigned priority, T item) {
    QItem *it = new QItem(item, priority, 0, cl);
    high_queue[priority].enqueue_front(it);
    return QItemRef(it);
  }

  QItemRef enqueue(K cl, unsigned priority, uint64_t cost, T item) {
    QItem *it = new QItem(item, priority, cost, cl);
    if (cost < min_cost)
      cost = min_cost;
    if (cost > max_tokens_per_subqueue)
      cost = max_tokens_per_subqueue;
    create_queue(priority)->enqueue(it);
    return QItemRef(it);
  }

  QItemRef enqueue_front(K cl, unsigned priority, uint64_t cost, T item) {
    QItem *it = new QItem(item, priority, cost, cl);
    if (cost < min_cost)
      cost = min_cost;
    if (cost > max_tokens_per_subqueue)
      cost = max_tokens_per_subqueue;
    create_queue(priority)->enqueue_front(it);
    return QItemRef(it);
  }

  bool empty() const {
    assert((total_priority == 0) || !(queue.empty()));
    return queue.empty() && high_queue.empty();
  }

  T dequeue() {
    assert(!empty());

    if (!(high_queue.empty())) {
      QItemRef ret = high_queue.rbegin()->second.pop_front();
      if (high_queue.rbegin()->second.empty())
	high_queue.erase(high_queue.rbegin()->first);
      return ret->item;
    }

    // if there are multiple buckets/subqueues with sufficient tokens,
    // we behave like a strict priority queue among all subqueues that
    // are eligible to run.
    for (typename SubQueues::iterator i = queue.begin();
	 i != queue.end();
	 ++i) {
      assert(!(i->second.empty()));
      if (i->second.front()->cost < i->second.num_tokens()) {
	QItemRef ret = i->second.pop_front();
	uint64_t cost = ret->cost;
	i->second.take_tokens(cost);
	i->second.pop_front();
	if (i->second.empty())
	  remove_queue(i->first);
	distribute_tokens(cost);
	return ret->item;
      }
    }

    // if no subqueues have sufficient tokens, we behave like a strict
    // priority queue.
    QItemRef ret = queue.rbegin()->second.pop_front();
    uint64_t cost = ret->cost;
    queue.rbegin()->second.pop_front();
    if (queue.rbegin()->second.empty())
      remove_queue(queue.rbegin()->first);
    distribute_tokens(cost);
    return ret->item;
  }

  void dump(Formatter *f) const {
    f->dump_int("total_priority", total_priority);
    f->dump_int("max_tokens_per_subqueue", max_tokens_per_subqueue);
    f->dump_int("min_cost", min_cost);
    f->open_array_section("high_queues");
    for (typename SubQueues::const_iterator p = high_queue.begin();
	 p != high_queue.end();
	 ++p) {
      f->open_object_section("subqueue");
      f->dump_int("priority", p->first);
      p->second.dump(f);
      f->close_section();
    }
    f->close_section();
    f->open_array_section("queues");
    for (typename SubQueues::const_iterator p = queue.begin();
	 p != queue.end();
	 ++p) {
      f->open_object_section("subqueue");
      f->dump_int("priority", p->first);
      p->second.dump(f);
      f->close_section();
    }
    f->close_section();
  }
};

template<typename T, typename K>
void PrioritizedQueue<T, K>::SubQueue::enqueue(QItem *item)
{
  q[item->klass].push_back(item);
  if (cur == q.end())
    cur = q.begin();
  size++;
}

template<typename T, typename K>
void PrioritizedQueue<T, K>::SubQueue::enqueue_front(QItem *item)
{
  q[item->klass].push_front(item);
  if (cur == q.end())
    cur = q.begin();
  size++;
}

#endif
