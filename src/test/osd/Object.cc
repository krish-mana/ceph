// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "include/interval_set.h"
#include "include/buffer.h"
#include <list>
#include <map>
#include <set>

#include "Object.h"

LayeredBuffer::iterator &LayeredBuffer::iterator::advance(bool init) {
  assert(pos < limit);
  assert(!end());
  if (!init) {
    pos++;
  }
  if (end()) {
    return *this;
  }
  while (pos == limit) {
    limit = *stack.begin();
    stack.pop_front();
    cur_cont--;
  }

  if (cur_cont == obj.layers.end()) {
    return *this;
  }

  interval_set<uint64_t> ranges;
  cont_gen->get_ranges(*cur_cont, ranges);
  while (!ranges.contains(pos)) {
    stack.push_front(limit);
    uint64_t next;
    if (pos >= ranges.range_end()) {
      next = limit;
    } else {
      next = ranges.start_after(pos);
    }
    if (next < limit) {
      limit = next;
    }
    cur_cont++;
    if (cur_cont == obj.layers.end()) {
      break;
    }

    ranges.clear();
    cont_gen->get_ranges(*cur_cont, ranges);
  }

  if (cur_cont == obj.layers.end()) {
    return *this;
  }

  if (!cont_iters.count(*cur_cont)) {
    cont_iters.insert(pair<ContentBuffer::iterator>(cont_gen->get_iterator(*cur_cont)));
  }
  map<ContDesc,ContentsGenerator::iterator>::iterator j = cont_iters.find(*cur_cont);
  assert(j != cont_iters.end());
  j->second.seek(pos);
  return *this;
}

const ContDesc &LayeredBuffer::most_recent() {
  return *layers.begin();
}

void LayeredBuffer::update(const ContDesc &next) {
  layers.push_front(next);
  return;
  interval_set<uint64_t> fall_through;
  fall_through.insert(0, cont_gen->get_length(next));
  for (list<ContDesc>::iterator i = layers.begin();
       i != layers.end();
       ) {
    interval_set<uint64_t> valid;
    cont_gen->get_ranges(*i, valid);
    valid.intersection_of(fall_through);
    if (valid.empty()) {
      layers.erase(i++);
      continue;
    }
    fall_through.subtract(valid);
    ++i;
  }
}

bool LayeredBuffer::check(bufferlist &to_check) {
  iterator i = begin();
  uint64_t pos = 0;
  for (bufferlist::iterator p = to_check.begin();
       !p.end();
       ++p, ++i, ++pos) {
    if (i.end()) {
      std::cout << "reached end of iterator first" << std::endl;
      return false;
    }
    if (*i != *p) {
      std::cout << "incorrect buffer at pos " << pos << std::endl;
      return false;
    }
  }
  return true;
}
