Scrub/Repair
============

Currently, scrub is almost entirely opaque to the user.  The user has some control
over when scrub happens, and can see the results in cluster log, but cannot
subsequently get detailed information about which objects are inconsistent and can
only repair an entire PG at a time using ceph's default repair strategy, which is
not terribly clever.  The goal is to ensure that users:

#. Can query at any time inconsistent objects along with a summary of
   the inconsistency (in a pageable fashion) from any pg.
#. Scrub a single object at a time
#. Can repair a single object at a time, and are able to specify how the repair
   should be done.  At least:

   - Use osd <osd> as the correct copy (not applicable to an EC pool)
   - Delete all copies of the object
   - Reset object_info omap or data digests seperately to match actual digests
	 - Reset size to match disk size

#. Can read all data and metadata for an object directly from a particular osd.
   (exported using a variant of the ceph_objectstore_tool format)

Questions:

#. Do we want to hide interval changes from the user?  

   - If so, we need to save the inconsistency information in a rados level
     object -- probably as an omap.
   - If we do that, how do we handle the saved information after the pg
     membership has changed?

#. json into and out of the rados tool seems like the natural user level
   interface, but how do we plumb it one layer down?  I think we want to
   implement the client<->osd communication using OSDOps, perhaps a new
	 scrub_repair class of OSDOp.
