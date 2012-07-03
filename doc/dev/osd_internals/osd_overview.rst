======================
OSD
======================

Concepts
--------

*Messenger*
   See src/msg/Messenger.h

	 Handles sending and reciept of messages on behalf of the osd.  The OSD uses
	 two messengers: 
	   1. cluster_messenger - handles traffic to other osds, monitors
		 2. client_messenger - handles client traffic

	 This division allows the OSD to be configured with different interfaces for
	 client and cluster traffic.

*Dispatcher*
   See src/msg/Dispatcher.h

	 OSD implements the Dispatcher interface.  Of particular note is ms_dispatch,
	 which serves as the entry pointer for messages recieved via either the client
	 or cluster messenger.  Because there are two messengers, ms_dispatch may be
	 called from at least two threads.  The osd_lock is always held during
	 ms_dispatch.

*WorkQueue*
   See src/common/WorkQueue.h

	 The WorkQueue class abstracts the process of queueing independent tasks
	 for asyncrounous execution.  Each osd process contains workqueues for
	 distinct tasks:
     1. OpWQ: handles ops (from clients) and subops (from other osds).
        Runs in the op_tp threadpool.
     2. PeeringWQ: handle peering tasks and pg map advancement
        Runs in the op_tp threadpool.
        See 
     3. CommandWQ: handles commands (pg query, etc)
        Runs in the op_tp threadpool.
     4. RecoveryWQ: handles recovery tasks.
        Runs in the disk_tp threadpool.
     5. 

*ThreadPool*
   See also above.
	 
