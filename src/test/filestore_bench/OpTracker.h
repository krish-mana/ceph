// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include <boost/scoped_ptr>
#include <list>
#include <tr1/memory>

class StatTracker {
  void record(const std::list<string> &category,
	      uint64_t value) = 0;
};

class OnApplied : public Context {
  OpRef ref;
  OnApplied(OpRef ref) : ref(ref) {};
  void finish(int r) { ref->on_applied() }
};

class OnCommit : public Context {
  OpRef ref;
  OnApplied(OpRef ref) : ref(ref) {};
  void finish(int r) { ref->on_commit() }
};
    

class Op {
  void begin() = 0;
  void on_commit() = 0;
  void on_applied() = 0;
  bool complete() = 0;
};
typedef std::tr1::shared_ptr<Op> OpRef;

class OpTracker {
  boost::scoped_ptr<ObjectStore> store;
public:
  void submit_op(OpRef);
}
  
