// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "include/interval_set.h"
#include "include/buffer.h"
#include <list>
#include <map>
#include <set>

#ifndef RANDOM_BUFFER_H
#define RANDOM_BUFFER_H

ostream &operator<<(ostream &out, const ContDesc &rhs);
class ContDesc {
public:
  int objnum;
  int cursnap;
  unsigned seqnum;
  string prefix;
  string oid;

  ContDesc() :
    objnum(0), cursnap(0),
    seqnum(0), prefix("") {}

  ContDesc(int objnum,
	   int cursnap,
	   unsigned seqnum,
	   const string &prefix) :
    objnum(objnum), cursnap(cursnap),
    seqnum(seqnum), prefix(prefix) {}

  bool operator==(const ContDesc &rhs) {
    return (rhs.objnum == objnum &&
	    rhs.cursnap == cursnap &&
	    rhs.seqnum == seqnum &&
	    rhs.prefix == prefix &&
	    rhs.oid == oid);
  }

  bool operator<(const ContDesc &rhs) const {
    return seqnum < rhs.seqnum;
  }

  bool operator!=(const ContDesc &rhs) {
    return !((*this) == rhs);
  }
};

class VarLenGenerator : public ContentsGenerator {
public:
  class RandWrap {
  public:
    unsigned int state;
    RandWrap(unsigned int seed)
    {
      state = seed;
    }

    int operator()()
    {
      return rand_r(&state);
    }
  };

  class iterator_impl : public ContentsGenerator::iterator_impl {
  public:
    uint64_t pos;
    ContDesc cont;
    RandWrap rand;
    bufferlist header;
    bufferlist::iterator header_pos;
    VarLenGenerator *cont_gen;
    char current;
    iterator_impl(const ContDesc &cont, VarLenGenerator *cont_gen) : 
      pos(0), cont(cont), rand(cont.seqnum), cont_gen(cont_gen) {
      cont_gen->write_header(cont, header);
      header_pos = header.begin();
      current = *header_pos;
      ++header_pos;
    }

    virtual ContDesc get_cont() const { return cont; }
    virtual uint64_t get_pos() const { return pos; }

    iterator_impl &operator++() {
      assert(!end());
      pos++;
      if (end()) {
	return *this;
      }
      if (header_pos.end()) {
	current = rand();
      } else {
	current = *header_pos;
	++header_pos;
      }
      return *this;
    }

    char operator*() {
      return current;
    }

    void seek(uint64_t _pos) {
      if (_pos < pos) {
	iterator_impl begin = iterator_impl(cont, cont_gen);
	begin.seek(_pos);
	*this = begin;
      }
      while (pos < _pos) {
	++(*this);
      }
    }

    bool end() {
      return pos >= cont_gen->get_length(cont);
    }
  };

  void get_ranges(const ContDesc &cont, interval_set<uint64_t> &out);

  ContentsGenerator::iterator_impl *get_iterator_impl(const ContDesc &in) {
    VarLenGenerator::iterator_impl *i = new iterator_impl(in, this);
    return i;
  }

  void put_iterator_impl(ContentsGenerator::iterator_impl *in) {
    delete in;
  }

  ContentsGenerator::iterator_impl *dup_iterator_impl(const ContentsGenerator::iterator_impl *in) {
    ContentsGenerator::iterator_impl *retval = get_iterator_impl(in->get_cont());
    retval->seek(in->get_pos());
    return retval;
  }

  int get_header_length(const ContDesc &in) {
    return 7*sizeof(int) + in.prefix.size();
  }

  uint64_t get_length(const ContDesc &in) {
    RandWrap rand(in.seqnum);
    return (rand() % length) + get_header_length(in);
  }

  void write_header(const ContDesc &in, bufferlist &output);

  bool read_header(bufferlist::iterator &p, ContDesc &out);
  uint64_t length;
  uint64_t min_stride_size;
  uint64_t max_stride_size;
  VarLenGenerator(int length) : 
    length(length), min_stride_size(length/10), max_stride_size(length/5) {}
};


#endif

