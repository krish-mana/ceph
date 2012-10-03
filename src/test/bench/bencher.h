// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-

#ifndef BENCHERH
#define BENCHERH

#include <utility>
#include "distribution.h"
#include "stat_collector.h"
#include "backend.h"
#include <boost/scoped_ptr.hpp>
#include "common/Mutex.h"
#include "common/Cond.h"

class OnWriteApplied;
class OnWriteCommit;
class OnReadComplete;
class Clenaup;

class Bencher {
public:
  enum OpType {
    WRITE,
    READ
  };

private:
  boost::scoped_ptr<
    Distribution<boost::tuple<std::string,uint64_t,uint64_t, OpType> > > op_dist;
  boost::scoped_ptr<StatCollector> stat_collector;
  boost::scoped_ptr<Backend> backend;
  const uint64_t max_in_flight;
  const uint64_t max_duration;
  const uint64_t max_ops;

  Mutex lock;
  Cond open_ops_cond;
  uint64_t open_ops;
  void start_op();
  void drain_ops();
  void complete_op();
public:
  Bencher(
    Distribution<boost::tuple<std::string, uint64_t, uint64_t, OpType> > *op_gen,
    StatCollector *stat_collector,
    Backend *backend,
    uint64_t max_in_flight,
    uint64_t max_duration,
    uint64_t max_ops) :
    op_dist(op_gen),
    stat_collector(stat_collector),
    backend(backend),
    max_in_flight(max_in_flight),
    max_duration(max_duration),
    max_ops(max_ops),
    lock("Bencher::lock"),
    open_ops(0)
  {}
  Bencher(
    Distribution<std::string> *object_gen,
    Distribution<uint64_t> *offset_gen,
    Distribution<uint64_t> *length_gen,
    Distribution<OpType> *op_type_gen,
    StatCollector *stat_collector,
    Backend *backend,
    uint64_t max_in_flight,
    uint64_t max_duration,
    uint64_t max_ops) :
    op_dist(
      new FourTupleDist<std::string, uint64_t, uint64_t, OpType>(
	object_gen, offset_gen, length_gen, op_type_gen)),
    stat_collector(stat_collector),
    backend(backend),
    max_in_flight(max_in_flight),
    max_duration(max_duration),
    max_ops(max_ops),
    lock("Bencher::lock"),
    open_ops(0)
  {}

  void init(
    const set<std::string> &objects,
    uint64_t size,
    std::ostream *out
    );

  void run_bench();
  friend class OnWriteApplied;
  friend class OnWriteCommit;
  friend class OnReadComplete;
  friend class Cleanup;
};

#endif
