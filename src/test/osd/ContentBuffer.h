// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#ifndef CONTENTBUFFER_H
#define CONTENTBUFFER_H
#include <tr1/memory>
#include "include/interval_set.h"
#include "include/buffer.h"
class ContentBuffer {
  typedef std::tr1::shared_ptr<ContentBuffer> ContentBufferRef;

  virtual uint64_t get_length() = 0;

  virtual void get_ranges(interval_set<uint64_t> &ranges) = 0;

  virtual iterator_impl *get_iterator_impl() = 0;

  virtual iterator_impl *dup_iterator_impl(const iterator_impl *in) = 0;

  virtual void put_iterator_impl(iterator_impl *in) = 0;

  virtual ~ContentsGenerator() {};

  iterator begin() {
    return iterator(this, get_iterator_impl(in));
  }

  class iterator_impl {
  public:
    virtual char operator*() = 0;
    virtual iterator_impl &operator++() = 0;
    virtual void seek(uint64_t pos) = 0;
    virtual bool end() = 0;
    virtual uint64_t get_pos() const = 0;
    virtual ~iterator_impl() {};
  };

  class iterator {
  public:
    ContentBufferRef parent;
    iterator_impl *impl;
    char operator *() { return **impl; }
    iterator &operator++() { ++(*impl); return *this; };
    void seek(uint64_t pos) { impl->seek(pos); }
    bool end() { return impl->end(); }
    ~iterator() { parent->put_iterator_impl(impl); }
    iterator &operator=(const iterator &rhs) {
      iterator new_iter(rhs);
      swap(new_iter);
      return *this;
    }
    void swap(iterator &other) {
      ContentsGenerator *otherparent = other.parent;
      other.parent = parent;
      parent = otherparent;

      iterator_impl *otherimpl = other.impl;
      other.impl = impl;
      impl = otherimpl;
    }
    iterator(const iterator &rhs) : parent(rhs.parent) {
      impl = parent->dup_iterator_impl(rhs.impl);
    }
    iterator(ContentBufferRef parent, iterator_impl *impl) :
      parent(parent), impl(impl) {}
  };
};

#endif
