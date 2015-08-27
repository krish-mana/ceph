// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 Red Hat
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 * *Very* heavily inspired by
 * http://docs.seastar-project.org/master/future_8hh_source.html
 */

#ifndef CEPH_FUTURE_H
#define CEPH_FUTURE_H

#include <memory>
#include <future>
#include <type_traits>
#include <memory>
#include <list>

#include <boost/optional.hpp>
#include <boost/noncopyable.hpp>

namespace Ceph {

namespace FutureDetail {

template <typename T>
class _Inbox;

template <typename T, typename ErrT>
class Promise;

template <typename T, typename ErrT>
class Result;

template <typename T>
struct _Outbox : private boost::noncopyable {
  _Inbox<T> *snk;

  _Outbox(_Outbox &&) = delete;
  _Outbox(_Inbox<T> *in) : snk(in) {}

  void fill(T &&t);
public:
  ~_Outbox() { assert(!snk); }
};

template <typename T>
struct _Inbox : private boost::noncopyable {
  _Inbox(_Inbox &&) = delete;
  boost::optional<T> box;
  _Outbox<T> *src = nullptr;
public:
  _Inbox(T &&t) : box(std::move(t)) {}
  
  _Inbox() {}

  bool ready() const { return box; }
  _Outbox<T> *get_outbox() {
    assert(!src);
    return (src = new _Outbox<T>(this));
  }
  T get() {
    assert(box);
    assert(!src);
    return std::move(*box);
  }
  void forward(_Outbox<T> &&ob) {
    assert(ob.snk);
    if (src) {
      src->snk = ob.snk;
      ob.snk = nullptr;
      src->snk->src = src;
      src = nullptr;
    } else if (box) {
      ob.fill(get());
    } else {
      ob.snk->src = nullptr;
      ob.snk = nullptr;
    }
  };
public:
  ~_Inbox() { assert(!src); }
};

template <typename T>
void _Outbox<T>::fill(T &&t) {
  assert(snk);
  snk->box = t;
  snk->src = nullptr;
  snk = nullptr;
}

template <typename T, typename ErrT>
class Future;

template <typename T, typename ErrT = int>
class Promise {
  friend class Future<T, ErrT>;
  friend class Result<T, ErrT>;
  std::unique_ptr<_Outbox<T>> ob;
  Promise(_Outbox<T> *ob) : ob(ob) {}
public:
  Promise() = default;
  bool valid() const { return static_cast<bool>(ob); }
  void fulfill(T &&t) {
    ob->fill(std::move(t));
    ob.reset();
  }
  void error(ErrT &&err) { assert(0 == "unimplemented"); }
};

template <typename T, typename ErrT>
class Result {
  friend class Promise<T, ErrT>;
  friend class Future<T, ErrT>;
  std::unique_ptr<_Inbox<T>> ib;
public:
  Result() : ib(new _Inbox<T>()) {}
  Result(T &&t) : ib(new _Inbox<T>(std::move(t))) {}
  bool valid() const { return static_cast<bool>(ib); }
  bool ready() const {
    assert(ib);
    return ib->ready();
  }
  Promise<T, ErrT> get_promise() {
    return Promise<T, ErrT>(ib->get_outbox());
  }
  void forward(Promise<T> &&p) {
    assert(ib);
    assert(p.ob);
    ib->forward(std::move(*(p.ob)));
    ib.reset();
    p.ob.reset();
  }
  T get() {
    assert(ready());
    T ret(ib->get());
    ib.reset();
    return ret;
  }
};

class Transformer {
public:
  using Ref = typename std::unique_ptr<Transformer>;

  virtual bool ready() = 0;

  // invalidates *this
  virtual std::list<Ref> run() && = 0;

  virtual ~Transformer() {}
};

template <typename U, typename Func, typename ErrT>
class TransformerImpl : boost::noncopyable, public Transformer {
public:
  Func f;
  Result<U, ErrT> src;
  TransformerImpl(Func &&f, Result<U, ErrT> &&_src) :
    f(std::move(f)), src(std::move(_src)) {
    assert(src.valid());
  }

  bool ready() {
    return src.ready();
  }

  std::list<Ref> run() && {
    return f(src.get());
  }
};

template <typename U, typename Func, typename ErrT>
Transformer *make_transformer(Func &&f, Result<U, ErrT> &&u) {
  return new TransformerImpl<U, Func, ErrT>(std::move(f), std::move(u));
}

template <typename T, typename ErrT>
struct Futurize {
  using FutureType = Future<T, ErrT>;
  using WrappedType = T;
  using ErrorType = ErrT;
  static FutureType make_future(T &&x) { return FutureType(std::move(x)); }
};

template <typename T, typename ErrT>
struct Futurize<Future<T, ErrT>, ErrT> {
  using FutureType = Future<T, ErrT>;
  using WrappedType = T;
  using ErrorType = ErrT;
  static FutureType make_future(FutureType &&x) { return std::move(x); }
};

template <typename T, typename ErrT = int>
class Future : boost::noncopyable {
  template <typename U, typename ErrU>
  friend class Future;

  Result<T, ErrT> inbox;
  std::list<Transformer::Ref> blockers;
  
  Future(Result<T, ErrT> &&inbox, std::list<Transformer::Ref> &&blockers) :
    inbox(std::move(inbox)), blockers(std::move(blockers)) {}


public:
  Future() {}
  Future(T &&t) : inbox(std::move(t)) {}
  Future(Future &&f) :
    inbox(std::move(f.inbox)), blockers(std::move(f.blockers)) {}

  void set_error(ErrT t) {
    assert(0 == "not implemented");
  }

  bool valid() const {
    return inbox.valid();
  }

  bool blocked() const {
    return !((blockers.empty() && inbox.ready()) ||
	     blockers.front()->ready());
  }

  bool ready() const {
    return inbox.ready();
  }

  bool is_error() const {
    assert(0 == "not implemented");
  }

  Promise<T, ErrT> get_promise() {
    return inbox.get_promise();
  }

  T get() {
    assert(ready() && !is_error());
    return inbox.get();
  }

  ErrT get_error() {
    assert(0 == "not implemented");
    return 0;
  }

  template <typename Func>
  auto then(Func &&f) ->
    typename Futurize<typename std::result_of<Func(T)>::type, ErrT>::FutureType {
    using futurator = Futurize<typename std::result_of<Func(T)>::type, ErrT>;
    using futuretype = typename futurator::FutureType;
    using wrapped_type = typename futurator::WrappedType;
    using promise = Promise<wrapped_type, ErrT>;
    using result = Result<wrapped_type, ErrT>;

    assert(valid());

    result res;
    promise p(res.get_promise());
    blockers.push_back(
      Transformer::Ref(
	make_transformer(
	  [p = std::move(p),
	   f = std::move(f)](T &&t) mutable {
	    futuretype fut(futurator::make_future(f(std::move(t))));
	    if (fut.ready()) {
	      p.fulfill(std::move(fut.get()));
	      return std::list<Transformer::Ref>();
	    } else {
	      fut.inbox.forward(std::move(p));
	      return std::move(fut.blockers);
	    }
	  },
	  std::move(inbox))));

    futuretype ret(std::move(res), std::move(blockers));
    return ret;
  }

  template <typename Func, typename Errfunc>
  auto then(Func &&f, Errfunc &&errf) ->
    typename Futurize<typename std::result_of<Func(T)>::type, ErrT>::FutureType {
    assert(0 == "not implemented");
    return f(get());
  }

private:
  void run_one_step() {
    assert(!blocked() && !ready());
    assert(blockers.size());
    assert(blockers.front()->ready());
    std::list<Transformer::Ref> next_steps = std::move(*blockers.front()).run();
    blockers.pop_front();
    blockers.splice(blockers.begin(), std::move(next_steps));
  }

public:
  void wait() {
    assert(0 == "Not implemented");
  }
  void run_until_blocked_or_ready() {
    while (!blocked() && !ready())
      run_one_step();
  }
};
};

template <typename T>
using Promise = FutureDetail::Promise<T>;
template <typename T>
using Future = FutureDetail::Future<T>;

};

#endif
