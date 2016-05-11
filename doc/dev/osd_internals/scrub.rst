
Scrubbing Behavior Table
========================

+-------------------------------------------------+----------+-----------+---------------+----------------------+
|                                          Flags  | none     | noscrub   | nodeep_scrub  | noscrub/nodeep_scrub |
+=================================================+==========+===========+===============+======================+
| Periodic tick                                   |   S      |    X      |     S         |         X            |
+-------------------------------------------------+----------+-----------+---------------+----------------------+
| Periodic tick after osd_deep_scrub_interval     |   D      |    D      |     S         |         X            |
+-------------------------------------------------+----------+-----------+---------------+----------------------+
| Initiated scrub                                 |   S      |    S      |     S         |         S            |
+-------------------------------------------------+----------+-----------+---------------+----------------------+
| Initiated scrub after osd_deep_scrub_interval   |   D      |    D      |     S         |         S            |
+-------------------------------------------------+----------+-----------+---------------+----------------------+
| Initiated deep scrub                            |   D      |    D      |     D         |         D            |
+-------------------------------------------------+----------+-----------+---------------+----------------------+

- X = Do nothing
- S = Do regular scrub
- D = Do deep scrub

State variables
---------------

- Periodic tick state is !must_scrub && !must_deep_scrub && !time_for_deep 
- Periodic tick after osd_deep_scrub_interval state is !must_scrub && !must_deep_scrub && time_for_deep 
- Initiated scrub state is  must_scrub && !must_deep_scrub && !time_for_deep
- Initiated scrub after osd_deep_scrub_interval state is must scrub && !must_deep_scrub && time_for_deep
- Initiated deep scrub state is  must_scrub && must_deep_scrub

OSD Scrub Initiation
--------------------

When the user issues a 'ceph pg <pgid> (scrub|deep-scrub|repair)' command,
the ceph tool sends an MOSDScrub message to the PG primary. The OSD finds the
specified pg in pg_map, sets scrubber.must_* to true, and calls reg_next_scrub().
Because the scrubber.must_* is true, the PG is scheduled at the head of the line.

Proposed OSDOp/Rados Scrub initiation
-------------------------------------

The above process has some very annoying shortfalls. If the PG is not on the OSD
any longer or the OSD is no longer primary, the client is not notified, and
nothing happens.  Further, if the interval changes before the scrub completes,
nothing reinitiates the user's scrub.  Writing tooling to do scrub or
repair in the event of an inconsistency would be clumsy to say the least with
this interface.  The implementation would have to monitor the PG for changes
(which is hard, not everything that can trigger a pg state change is exposed
through the existing tools) and rescrub/repair as needed.  Making the ceph
tool do this transparently would be an option, but the required logic at that
point starts to look a lot like the objecter.  Implementing it as a RADOS
PG op would be quite a bit simpler.  It would also let us send a meaningful
reply upon completion allowing us to add a --wait-for-complete option to the
user scrub command.  It would also fit in with the design for fetching the
json scrub results and in the future with how we will do per-object repair.
