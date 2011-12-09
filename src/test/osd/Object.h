// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "include/interval_set.h"
#include "include/buffer.h"
#include <list>
#include <map>
#include <set>

#ifndef OBJECT_H
#define OBJECT_H

class LayeredBuffer : public ContentBuffer {
public:
  typedef std::tr1::shared_ptr<LayeredBuffer> LayeredBufferRef;

  LayeredBuffer() : 
    layers() {}

  class iterator_impl : public ContentBuffer::iterator_impl {
  public:
    uint64_t pos;
    LayeredBufferRef obj;
    list<uint64_t> stack;
    uint64_t limit;
    list<ContentBufferRef>::iterator cur_cont;
    
    iterator(Object &obj) : 
      pos(0), obj(obj) {}

    iterator &advance(bool init);
    iterator &operator++() {
      return advance(false);
    }

    char operator*() {
      if (cur_cont == obj.layers.end()) {
	return '\0';
      } else {
	map<ContDesc,ContentsGenerator::iterator>::iterator j = cont_iters.find(*cur_cont);
	assert(j != cont_iters.end());
	return *(j->second);
      }
    }

    bool end() {
      return pos == cont_gen->get_length(*obj.layers.begin());
    }

    void seek(uint64_t _pos) {
      if (_pos < pos) {
	assert(0);
      }
      while (pos < _pos) {
	++(*this);
      }
    }
  };

  iterator begin() {
    return iterator(*this, this->cont_gen);
  }

  bool deleted() {
    return !layers.size(); // No layers indicates missing object
  }

  void update(const ContDesc &next);
  bool check(bufferlist &to_check);
  const ContDesc &most_recent();
private:
  list<ContDesc> layers;
  ContentsGenerator *cont_gen;
  Object();
};

#endif
