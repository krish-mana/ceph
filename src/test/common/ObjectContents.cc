// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#include "ObjectContents.h"
#include "include/buffer.h"
#include <map>

bool test_object_contents()
{
  ObjectContents c, d;
  assert(!c.exists());
  c.write(10, 10, 20);
  assert(c.exists());
  assert(c.size() == 20);

  bufferlist bl;
  for (ObjectContents::Iterator iter = c.get_iterator();
       iter.valid();
       ++iter) {
    bl.append(*iter);
  }
  assert(bl.length() == 20);

  assert(bl[0] == '\0');
  assert(bl[9] == '\0');

  d.clone_range(c, 5, 15);
  assert(d.size() == 15);

  ObjectContents::Iterator iter2 = d.get_iterator();
  iter.seek_to(5);
  for (uint64_t i = 5; i < 15; ++i) {
    assert(*iter == bl[i]);
  }
  return true;
}
  

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
    seeds.erase(seeds.lower_bound(start), seeds.lower_bound(start+len));

    seeds[start] = other.get_iterator().get_state(start);
    seeds.insert(other.seeds.upper_bound(start),
		 other.seeds.lower_bound(start+len));
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
	      seeds.lower_bound(start+len));
  seeds[start] = seed;

  interval_set<uint64_t> to_write;
  to_write.insert(start, len);
  written.union_of(to_write);

  if (start + len > _size)
    _size = start + len;
  return;
}
