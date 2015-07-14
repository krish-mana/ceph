Scrub/Repair
============

Currently, scrub is almost entirely opaque to the user.  The user has some control
over when scrub happens, and can see the results in cluster log, but cannot
subsequently get detailed information about which objects are inconsistent and can
only repair an entire PG at a time using ceph's default repair strategy, which is
not terribly clever.  The goal is to ensure that users:

#. Can query at any time inconsistent objects along with a summary of
   the inconsistency (in a pageable fashion) from any pg.
#. Can repair a single object at a time, and are able to specify how the repair
   should be done.  At least:
   - Use osd <osd> as the correct copy (not applicable to an EC pool)
   - Delete all copies of the object
