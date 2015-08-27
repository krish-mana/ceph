// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "OSDReactor.h"

// TODO: make a config option
const unsigned MAX_INFLIGHT_OPS = 16;

void OSDReactor::start_operation(unsigned num)
{
  unique_lock g(lock);
  active += num;
  cv.notify_one();
}

void OSDReactor::push(void *completion, int result)
{
  {
    unique_lock g(lock);
    assert(active >= 1);
    active--;
    runqueue.push_back(
      make_pair(
	static_cast<std::function<void(int)> *>(completion), result));
  }
  cv.notify_one();
}

OSDReactor::OSDReactor(OSD *osd) :
  osd(osd),
  in_queue(10, 10),
  active(0)
{
  t = std::thread([this](){ run(); });
}

void OSDReactor::run()
{
  bool stop = false;
  while (!stop) {
    unique_ptr<std::function<void(int)> > to_run;
    int arg = 0;
    {
      unique_lock g(lock);
      cv.wait(
	g, [this](){
	  return !(
	    runqueue.empty() && (
	      active >= MAX_INFLIGHT_OPS ||
	      in_queue.empty()));
	});
      if (!runqueue.empty()) {
	auto queued = runqueue.front();
	runqueue.pop_front();
	arg = queued.second;
	to_run.reset(queued.first);
      } else if (active < MAX_INFLIGHT_OPS && !in_queue.empty()) {
	auto queued = in_queue.dequeue();
	to_run.reset(new std::function<void(int)>(
	  [this,queued](int){
	    // TODO: move TPHandle out of ThreadPool, add impl here
	    //queued.second.run(osd, queued.first, 
	  }));
      }
    }
    if (!to_run)
      continue;

    (*to_run)(arg);
  }
}
