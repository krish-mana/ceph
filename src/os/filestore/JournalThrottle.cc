// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "JournalThrottle.h"
#include "include/assert.h"

bool JournalThrottle::set_params(
  double _low_threshhold,
  double _high_threshhold,
  double _expected_delay,
  double _max_delay,
  uint64_t _throttle_max,
  std::ostream *errstream)
{
  bool valid = true;
  if (_low_threshhold > _high_threshhold) {
    valid = false;
    if (errstream) {
      *errstream << "low_threshhold (" << _low_threshhold
		 << ") > high_threshhold (" << _high_threshhold
		 << ")" << std::endl;
    }
  }

  if (_max_delay < _expected_delay) {
    valid = false;
    if (errstream) {
      *errstream << "expected_delay (" << _expected_delay
		 << ") > max_delay (" << _max_delay
		 << ")" << std::endl;
    }
  }

  if (_low_threshhold > 1 || _low_threshhold < 0) {
    valid = false;
    if (errstream) {
      *errstream << "invalid low_threshhold (" << _low_threshhold << ")"
		 << std::endl;
    }
  }

  if (_high_threshhold > 1 || _high_threshhold < 0) {
    valid = false;
    if (errstream) {
      *errstream << "invalid high_threshhold (" << _high_threshhold << ")"
		 << std::endl;
    }
  }

  if (_max_delay < 0) {
    valid = false;
    if (errstream) {
      *errstream << "invalid max_delay (" << _max_delay << ")"
		 << std::endl;
    }
  }

  if (_expected_delay < 0) {
    valid = false;
    if (errstream) {
      *errstream << "invalid expected_delay (" << _expected_delay << ")"
		 << std::endl;
    }
  }

  if (!valid)
    return false;

  locker l(lock);
  low_threshhold = _low_threshhold;
  high_threshhold = _high_threshhold;
  expected_delay = _expected_delay * _throttle_max;
  max_delay = _max_delay * _throttle_max;
  max = _throttle_max;

  if (high_threshhold - low_threshhold > 0) {
    s0 = expected_delay / (2 * (high_threshhold - low_threshhold));
  } else {
    low_threshhold = high_threshhold;
    s0 = 0;
  }

  if (1 - high_threshhold > 0) {
    s1 = (max_delay - (2 * expected_delay))/(1 - high_threshhold);
  } else {
    high_threshhold = 1;
    s1 = 0;
  }

  _kick_waiters();
  return true;
}

std::chrono::duration<double> JournalThrottle::_get_delay()
{
  if (max == 0)
    return std::chrono::duration<double>(0);

  double r = current / max;
  if (r < low_threshhold) {
    return std::chrono::duration<double>(0);
  } else if (r < high_threshhold) {
    return std::chrono::duration<double>(
      (r - low_threshhold) * s0);
  } else {
    return std::chrono::duration<double>(
      (2 * expected_delay) + ((r - high_threshhold) * s1));
  }
}

std::chrono::duration<double> JournalThrottle::get(uint64_t c)
{
  locker l(lock);
  auto start = std::chrono::system_clock::now();
  auto delay = _get_delay();

  // fast path
  if (delay == std::chrono::duration<double>(0) &&
      waiters.empty() &&
      ((max == 0) || (current == 0) || ((current + c) <= max))) {
    current += c;
    return std::chrono::duration<double>(0);
  }

  auto ticket = _push_waiter();
  while ((waiters.begin() != ticket) ||
	 ((start + delay) > std::chrono::system_clock::now()) ||
	 !((max == 0) || (current == 0) || ((current + c) <= max))) {
    (*ticket)->wait_until(l, start + delay);
    delay = _get_delay();
  }
  waiters.pop_front();
  _kick_waiters();

  current += c;
  return std::chrono::system_clock::now() - start;
}

void JournalThrottle::take(uint64_t c)
{
  locker l(lock);
  current += c;
}

void JournalThrottle::register_throttle_seq(uint64_t seq, uint64_t c)
{
  locker l(lock);
  journaled_ops.push_back(std::make_pair(seq, c));
}

void JournalThrottle::flush(uint64_t mono_id)
{
  locker l(lock);
  while (!journaled_ops.empty() &&
	 journaled_ops.front().first <= mono_id) {

    assert(current >= journaled_ops.front().second);
    current -= journaled_ops.front().second;
    journaled_ops.pop_front();
  }
  assert(!((current == 0) ^ journaled_ops.empty()));
  _kick_waiters();
}

uint64_t JournalThrottle::get_current()
{
  locker l(lock);
  return current;
}

uint64_t JournalThrottle::get_max()
{
  locker l(lock);
  return max;
}
