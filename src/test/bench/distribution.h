// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-

#ifndef DISTIRITBIONHPP
#define DISTIRITBIONHPP

#include <map>
#include <set>
#include <utility>
#include <vector>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/uniform_real.hpp>

typedef boost::mt11213b rngen_t;

template <typename T>
class Distribution {
public:
  virtual T operator()() = 0;
  virtual ~Distribution() {}
};

template <typename T>
class RandomDist : public Distribution<T> {
  rngen_t rng;
  std::map<uint64_t, T> contents;
public:
  RandomDist(rngen_t rng, std::set<T> &initial) : rng(rng) {
    uint64_t count = 0;
    for (typename std::set<T>::iterator i = initial.begin();
	 i != initial.end();
	 ++i, ++count) {
      contents.insert(make_pair(count, *i));
    }
  }
  virtual T operator()() {
    assert(contents.size());
    boost::uniform_int<> value(0, contents.size() - 1);
    return contents.find(value(rng))->second;
  }
};

template <typename T>
class WeightedDist : public Distribution<T> {
  rngen_t rng;
  double total;
  std::map<double, T> contents;
public:
  WeightedDist(rngen_t rng, const std::set<std::pair<double, T> > &initial)
    : rng(rng), total(0) {
    for (typename std::set<std::pair<double, T> >::const_iterator i =
	   initial.begin();
	 i != initial.end();
	 ++i) {
      total += i->first;
      contents.insert(make_pair(total, i->second));
    }
  }
  virtual T operator()() {
    return contents.lower_bound(
      boost::uniform_real<>(0, total)(rng))->second;
  }
};

template <typename T, typename U>
class SequentialDist : public Distribution<T> {
  rngen_t rng;
  std::vector<T> contents;
  typename std::vector<T>::iterator cur;
public:
  SequentialDist(rngen_t rng, U &initial) : rng(rng) {
    contents.insert(initial.begin(), initial.end());
    cur = contents.begin();
  }
  virtual T operator()() {
    assert(contents.size());
    if (cur == contents.end())
      cur = contents.begin();
    return *(cur++);
  }
};

class UniformRandom : public Distribution<uint64_t> {
  rngen_t rng;
  uint64_t min;
  uint64_t max;
public:
  UniformRandom(rngen_t rng, uint64_t min, uint64_t max) :
    rng(rng), min(min), max(max) {}
  virtual uint64_t operator()() {
    return boost::uniform_int<>(min, max)(rng);
  }
};

class Uniform : public Distribution<uint64_t> {
  uint64_t val;
public:
  Uniform(uint64_t val) : val(val) {}
  virtual uint64_t operator()() {
    return val;
  }
};

#endif
