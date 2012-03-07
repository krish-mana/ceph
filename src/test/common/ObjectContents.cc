// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#include "ObjectContents.h"
#include "include/buffer.h"
#include <map>

unsigned long ObjectContents::Iterator::get_state(uint64_t pos)
{
  if (parent->seeds.count(pos)) {
    return parent->seeds[pos];
  }
  seek_to(pos - 1);
  return current_state;
}

void ObjectContents::clone_range(ObjectContents &other,
				 interval_set<uint64_t> &intervals)
{
  interval_set<uint64_t> written_to_clone;
  written_to_clone.intersection_of(intervals, other.written);

  interval_set<uint64_t> zeroed = intervals;
  zeroed.subtract(written_to_clone);

  other.written.union_of(intervals);
  other.written.subtract(zeroed);

  for (interval_set<uint64_t>::iterator i = written_to_clone.begin();
       i != written_to_clone.end();
       ++i) {
    uint64_t start = i.get_start();
    uint64_t len = i.get_len();

    seeds[start+len] = get_iterator().get_state(start+len);
    seeds.erase(seeds.upper_bound(start), seeds.lower_bound(start+len));
    seeds[start] = other.get_iterator().get_state(start);
    seeds.insert(other.seeds.upper_bound(start),
		 --other.seeds.upper_bound(start+len));
  }

  if (intervals.range_end() > _size)
    _size = intervals.range_end();
  _exists = true;
  return;
}

void ObjectContents::write(unsigned long seed,
			   uint64_t start,
			   uint64_t len)
{
  _exists = true;
  seeds[start+len] = get_iterator().get_state(start+len);
  seeds.erase(seeds.lower_bound(start),
	      --seeds.upper_bound(start+len));
  seeds[start] = seed;

  interval_set<uint64_t> to_write;
  to_write.insert(start, len);
  written.union_of(to_write);

  if (start + len > _size)
    _size = start + len;
  return;
}
