=============================
ObjectMap based snap trimming
=============================

Rados exposes two slightly different snapshot interfaces.  In either case, we
need a lightweight way to determine when deleting a snapshot which clones can be
removed.

Overview
========
There are two slightly different snapshot interfaces:
1. Pool snaps.  All objects in the pool share the same set of snapshots.
   The set of current snaps for the pool is defined by the *pg_pool_t::snaps*
	 member.  Removals are implemented by removing snaps from this set.
2. Self managed snaps.  Each object has an independent *snapset*.  The
   set of current snaps is defined by
	 [0, *pg_pool_t::snap_seq*] \ *pg_pool_t::removed_snaps*.

In either case, the set of valid snaps is defined by the current *osdmap*.
An attempt to write to an object whose most recent clone is older than
the most recent snap defined for that object will result in a new clone
being created with a *snapid* of the most recent *snapid* defined on
that object.  We asynchronously process the removed snapshots in order
to remove clones which are no longer necessary (all of their snapshots
have been removed).

Previous design
===============

Currently, we keep several kinds of metadata to manage clone removal.

* The *snapset* on the *head*, or on the *snapdir* if the head has been removed,
  (implemented as an xattr) contains three key pieces of information:
  1. *head_exists*: true iff the head currently exists
  2. *snaps*: a vector containing all snaps defined for this object
  3. *clones*: a vector containing all clones defined for this object
  Note, the *snapid* for a clone is the last *snapid* for which the clone
  is defined.  It is this value which is stored in *clones*.

* The *object_info* on each clone (implemented as an xattr) contains a *snaps*
  vector containing all snaps for which this clone is relevant.  As snapshots
  are removed, this vector is opportunistically filtered to remove snapshots
  which have been removed by the user.  Note, this vector may at any time
  contain *snapids* which have already been purged, but not yet removed.
  However, the first and last *snapids* in this vector must not yet have
  been purged.

* 
