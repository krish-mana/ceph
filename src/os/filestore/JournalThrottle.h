// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_JOURNAL_THROTTLE_H
#define CEPH_JOURNAL_THROTTLE_H

#include <list>
#include <deque>
#include <condition_variable>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>

/**
 * JournalThrottle
 *
 * Throttle designed to implement dynamic throttling as the journal fills
 * up.  The goal is to not delay ops at all when the journal is relatively
 * empty, delay ops somewhat as the journal begins to fill (with the delay
 * getting linearly longer as the journal fills up to a high water mark),
 * and to delay much more aggressively (though still linearly with usage)
 * until we hit the max value.
 *
 * Let the current throttle ratio (current/max) be r, low_threshhold be l,
 * high_threshhold be h, expected_delay be e, and max_delay be m.
 *
 * delay_ratio = 0, r \in [0, l)
 * delay_ratio = (r - l) * (e / (2 * (h - l))), r \in [l, h)
 * delay_ratio = 2e + (r - h)((m - 2e)/(1 - h))
 *
 * The actual delay will then be delay_ratio * current.
 *
 * The usage pattern is as follows:
 * 1) Call get(seq, bytes) before taking the op_queue_throttle
 * 2) Once the journal is flushed, flush(max_op_id_flushed)
 */
class JournalThrottle {
  std::mutex lock;

  /// deque<id, count>
  std::deque<std::pair<uint64_t, uint64_t> > journaled_ops;
  using locker = std::unique_lock<std::mutex>;

  unsigned next_cond = 0;

  /// allocated once to avoid constantly allocating new ones
  std::vector<std::condition_variable> conds;

  /// pointers into conds
  std::list<std::condition_variable*> waiters;

  std::list<std::condition_variable*>::iterator _push_waiter() {
    unsigned next = next_cond;
    if (next_cond == conds.size())
      next_cond = 0;
    return waiters.insert(waiters.end(), &(conds[next]));
  }

  void _kick_waiters() {
    if (!waiters.empty())
      waiters.front()->notify_all();
  }

  /// see above, values are in [0, 1].
  double low_threshhold = 0;
  double high_threshhold = 1;

  /// see above, values are in seconds
  double expected_delay = 0;
  double max_delay = 0;

  /// Filled in in set_params
  double s0 = 0; ///< e/2(h - l), h != l, 0 otherwise
  double s1 = 0; ///< (m - 2e)/(1 - h), 1 != h, 0 otherwise

  /// max
  uint64_t max = 0;
  uint64_t current = 0;

  std::chrono::duration<double> _get_delay();

public:
  /**
   * set_params
   *
   * Sets params.  If the params are invalid, returns false
   * and populates errstream (if non-null) with a user compreshensible
   * explanation.
   */
  bool set_params(
    double low_threshhold,
    double high_threshhold,
    double expected_delay,
    double max_delay,
    uint64_t throttle_max,
    std::ostream *errstream);

  /**
   * gets specified throttle for id mono_id, waiting as necessary
   *
   * @param c [in] amount to take
   * @return duration waited
   */
  std::chrono::duration<double> get(uint64_t c);

  /**
   * take
   *
   * Takes specified throttle without waiting
   */
  void take(uint64_t c);

  /**
   * register_throttle_seq
   *
   * Registers a sequence number with an amount of throttle to
   * release upon flush()
   *
   * @param seq [in] seq
   */
  void register_throttle_seq(uint64_t seq, uint64_t c);


  /**
   * Releases throttle held by ids <= mono_id
   *
   * @param mono_id [in] id up to which to flush
   */
  void flush(uint64_t mono_id);

  uint64_t get_current();
  uint64_t get_max();

  JournalThrottle(
    unsigned expected_concurrency ///< [in] determines size of conds
    ) : conds(expected_concurrency) {}
};

#endif
