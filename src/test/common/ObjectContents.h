// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "include/interval_set.h"
#include "include/buffer.h"
#include <list>
#include <map>
#include <set>
#include <tr1/memory>
#include <boost/scoped_ptr.hpp>

#ifndef COMMON_OBJECT_H
#define COMMON_OBJECT_H

enum {
  RANDOMWRITEFULL,
  DELETED,
  CLONERANGE
};

class ObjectContents;
ObjectContents *object_decode(bufferlist::iterator &bp);

class ObjectContents {
public:
  class iterator_impl {
  public:
    virtual char get() = 0;
    virtual void next() = 0;
    virtual void seek_to_first() = 0;
    virtual bool valid() = 0;
    virtual uint64_t get_pos() = 0;

    virtual void seek_to(uint64_t to) {
      if (to < get_pos())
	seek_to_first();
      while (get_pos() < to) next();
    }
    virtual ~iterator_impl() {};
  };
  typedef std::tr1::shared_ptr<iterator_impl> iterator;

  virtual iterator get_iterator() = 0;
  virtual ObjectContents *duplicate() const = 0;
  virtual uint64_t size() const = 0;
  virtual bool exists() const { return true; }
  virtual void encode(bufferlist &bl) const = 0;
  virtual ~ObjectContents() {};
};

class RandomWriteFull : public ObjectContents {
  uint64_t _size;
  map<uint64_t, unsigned long> cached_seeds;
  class rand_iterator : public iterator_impl {
    RandomWriteFull *parent;
    unsigned int current_state;
    int current_val;
    uint64_t pos;
  public:
    rand_iterator(RandomWriteFull *parent) :
      parent(parent), current_state(0), current_val(0), pos(0) {
      seek_to_first();
    }
    char get() {
      return static_cast<char>(current_val % 256);
    }
    uint64_t get_pos() {
      return pos;
    }
    void seek_to(uint64_t _pos) {
      map<uint64_t, unsigned long>::iterator i =
	parent->cached_seeds.upper_bound(_pos);
      --i;
      if (pos < i->first) {
	pos = i->first - 1;
	current_state = i->second;
	next();
      }
      while (pos < _pos) next();
    }
      
    void seek_to_first() {
      seek_to(0);
    }
    void next() {
      pos++;
      if (!(pos % 1024) && pos > parent->cached_seeds.rbegin()->first) {
	parent->cached_seeds.insert(make_pair(pos, current_state));
      }
      current_val = rand_r(&current_state);
    }
    bool valid() {
      return pos <= parent->size();
    }
  };

public:
  RandomWriteFull(uint64_t seed, uint64_t size)
    : _size(size) {
    cached_seeds[0] = seed;
  }

  RandomWriteFull(bufferlist::iterator &bp) {
    __u8 code;
    ::decode(code, bp);
    assert(code == RANDOMWRITEFULL);
    ::decode(_size, bp);
    ::decode(cached_seeds, bp);
  }

  iterator get_iterator() {
    return iterator(new rand_iterator(this));
  }
  uint64_t size() const { return _size; }
  ObjectContents *duplicate() const {
    return new RandomWriteFull(*this);
  }
  void encode(bufferlist &bl) const {
    __u8 code = RANDOMWRITEFULL;
    ::encode(code, bl);
    ::encode(_size, bl);
    ::encode(cached_seeds, bl);
  }
};

class Deleted : public ObjectContents {
  class deleted_iterator : public iterator_impl {
    uint64_t pos;
  public:
    deleted_iterator() : pos(0) {}
    char get() {
      return '\0';
    }
    void seek_to_first() {
      pos = 0;
    }
    void seek_to(uint64_t _pos) {
      pos = _pos;
    }
    void next() {
      ++pos;
    }
    bool valid() {
      return false;
    }
    uint64_t get_pos() { return pos; }
  };
public:
  Deleted() {};
  Deleted(bufferlist::iterator &bp) {
    __u8 code;
    ::decode(code, bp);
    assert(code == DELETED);
  }
  iterator get_iterator() {
    return iterator(new deleted_iterator);
  }
  uint64_t size() const { return 0; }
  bool exists() const { return false; }
  ObjectContents *duplicate() const {
    return new Deleted();
  }
  void encode(bufferlist &bl) const {
    __u8 code = DELETED;
    ::encode(code, bl);
  }
};


class CloneRange : public ObjectContents {
  boost::scoped_ptr<ObjectContents> from;
  boost::scoped_ptr<ObjectContents> to;
  interval_set<uint64_t> cloned_intervals;
  uint64_t total_size;
  class clone_range_iterator : public iterator_impl {
    uint64_t pos;
    CloneRange *parent;
    ObjectContents::iterator from_iter;
    ObjectContents::iterator to_iter;
  public:
    clone_range_iterator(CloneRange *parent) :
      pos(0), parent(parent),
      from_iter(parent->from->get_iterator()),
      to_iter(parent->to->get_iterator()) {}
    uint64_t get_pos() { return pos; }
    char get() {
      return parent->cloned_intervals.contains(pos) ?
	from_iter->get() : to_iter->get();
    }
    void seek_to_first() {
      from_iter->seek_to_first();
      to_iter->seek_to_first();
    }
    void next() {
      pos++;
      if (parent->cloned_intervals.contains(pos))
	from_iter->seek_to(pos);
      else
	to_iter->seek_to(pos);
      return;
    }
    bool valid() {
      return pos < parent->size();
    }
  };
  friend class clone_range_iterator;
public:
  CloneRange(const ObjectContents &_from, const ObjectContents &_to,
	     const interval_set<uint64_t> &intervals)
    : from(_from.duplicate()), to(_to.duplicate()),
      cloned_intervals(intervals) {
    total_size = _size();
  }

  CloneRange(bufferlist::iterator &bp) {
    __u8 code;
    ::decode(code, bp);
    assert(code == CLONERANGE);
    from.reset(object_decode(bp));
    to.reset(object_decode(bp));
    ::decode(cloned_intervals, bp);
    total_size = _size();
  }

  iterator get_iterator() {
    return iterator(new clone_range_iterator(this));
  }
  uint64_t size() const { return total_size; }
  uint64_t _size() const {
    return from->size() < to->size() ? to->size() : 
      to->size() < cloned_intervals.range_end() ?
      cloned_intervals.range_end() : to->size();
  }
  ObjectContents *duplicate() const {
    return new CloneRange(*from, *to, cloned_intervals);
  }
  void encode(bufferlist &bl) const {
    __u8 code = CLONERANGE;
    ::encode(code, bl);
    from->encode(bl);
    to->encode(bl);
    ::encode(cloned_intervals, bl);
  }
};

#endif
