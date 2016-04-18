=====================================
Partial write operations for EC pools
=====================================

The current RADOS implementation of erasure coded pools doesn't
support less than a full stripe write. This prevents the direct usage
of EC pools by RBD and CephFS. This is an architecture to enable that
capability.

MAIN OPERATION OVERVIEW
=======================

A RADOS put operation can span
multiple stripes of a single object. There must be code that
tessellates the application level write into a set of per-stripe write
operations -- some whole-stripes and up to two partial
stripes. Without loss of generality, for the remainder of this
document we will focus exclusively on writing a single stripe (whole
or partial). We will use the symbol "W" to represent the number of
blocks within a stripe that are being written, i.e., W <= K.

There are three data flows for handling a write into an EC stripe. The
choice of which of the three data flows to choose is based on the size
of the write operation and the arithmetic properties of the selected
parity-generation algorithm.

(1) whole stripe is written/overwritten
(2) a read-modify-write operation is performed.
(3) a parity-delta is computed and applied.

WHOLE STRIPE WRITE
------------------

This is the simple case, and is already performed in the existing
code. The primary receives all of the data for the stripe in the RADOS
request, computes the appropriate parity blocks and send the data and
parity blocks to their destination shards which write them. This is
essentially the current EC code.  

READ-MODIFY-WRITE
-----------------

The primary determines which of the K-W blocks are to be unmodified,
and reads them from the shards. Once all of the data is received it is
combined with the received new data and new parity blocks are
computed. The modified blocks are sent to their respective shards and
written. The RADOS operation is acknowledged.

PARITY-DELTA-WRITE
------------------

The primary reads the current values of the "W" blocks and then uses
the new values of the "W" blocks to compute parity-deltas for each of
the parity blocks.  The W blocks and the parity delta-blocks are sent
to their respective shards.

The choice of whether to use a read-modify-write or a
parity-delta-write is complex policy issue that is TBD in the details
and is likely to be heavily dependant on the computational costs
associated with a parity-delta vs. a regular parity-generation
operation. However, it is believed that the parity-delta scheme is
likely to be the preferred choice, when available.

The internal interface to the erasure coding library plug-ins needs to
be extended to support the ability to query if parity-delta
computation is possible for a selected algorithm as well as an
interface to the actual parity-delta computation algorithm when
available.

OSD Object Write and Recovery
-----------------------------

Regardless of the algorithm chosen above, writing of the data is a two
phase process: prepare and apply. The primary sends the prepare
operation to each OSD along with the data for that specific OSD (some
OSDs will not have any data -- they are said to “witness” the
operation) -- here data means user data and parity data. Each OSD
receives the prepare operation, performs it and then sends an
acknowledgement (ACK) to the primary. The primary waits until a
sufficient number of the OSDs have returned an ACK (sufficient is
described below) and then generates an acknowledgement of the client’s
original write request operation and issues an apply operation to each
OSD. Once the apply is received, the operation is committed -- any
future recovery operation must NOT attempt to undo this write
operation. Conversely, any operation that has received a prepare but
no corresponding apply can be rolled back.

As an optimization, the primary can delay the issuance of an apply
operation for an arbitrary period of time leading to the obvious
optimization that apply operations can be batched together and done in
the background. This optimization will be particularly important for
supporting high sequential write speeds when the client cannot be
convinced to perform full stripe writes.

As described above, all OSD shards in the PG have some participation
in each write operation. While this is conceptually correct, the
implementation is somewhat different for performance reasons. It is
believed that many write operations will modify data in only a small
number of PG shards and that optimization of this case is
important. As described above, each witness OSD has the transmission
of its corresponding prepare and apply operations delayed (buffered)
at the primary until either an artificial limit is reached OR an
operation that actually involves that OSD is transmitted.  The amount
of buffering is a tradeoff between efficiency of applying a larger set
of updates at once and the amount of unstable data which must be
buffered on the primary for reads. The buffering of operations must
NOT permit reordering of them.  Essentially, this is just a lazy
transmission algorithm similar to Nagle's algorithm for TCP.

Stripe Cache
------------

One application pattern that is important to optimize is the small
block sequential write operation (think of the journal of a journaling
file system or a database transaction log). Regardless of the chosen
redundancy algorithm, it is advantageous for the primary to
retain/buffer recently read/written portions of a stripe in order to
reduce network traffic. The dynamic contents of this cache may be used
in the determination of whether a read-modify-write or a
parity-delta-write is performed. The sizing of this cache is TBD, but
we should plan on allowing at least a few full stripes per active
client. Limiting the cache occupancy on a per-client basis will reduce
the noisy neighbor problem.

Recovery and Rollback Details
=============================

Implementing a Rollback-able Prepare Operation
----------------------------------------------

The prepare operation is implemented at each OSD through a simulation
of a versioning or copy-on-write capability for modifying a portion of
an object.

When a prepare operation is performed, the new data is written into a
temporary object. The PG log for the
operation will contain a reference to the temporary object so that it
can be located for recovery purposes as well as a record of all of the
shards which are involved in the operation. 

In order to avoid fragmentation (and hence, future read performance),
creation of the temporary object needs special attention. The name of
the temporary object affects its location within the KV store. Right
now its unclear whether it's desirable for the name to locate near the
base object or whether a separate subset of keyspace should be used
for temporary objects. Sam believes that colocation with the base
object is preferred (he suggests using the generation counter of the
ghobject for temporaries).  Whereas Allen believes that using a
separate subset of keyspace is desirable since these keys are
ephemeral and we don't want to actually colocate them with the base
object keys. Perhaps some modeling here can help resolve this
issue. The data of the temporary object wants to be located as close
to the data of the base object as possible. This may be best performed
by adding a new ObjectStore creation primitive that takes the base
object as an addtional parameter that is a hint to the allocator.

Sam: I think that the short lived thing may be a red herring.  We'll
be updating the donor and primary objects atomically, so it seems like
we'd want them adjacent in the key space, regardless of the donor's
lifecycle.

The apply operation moves the data from the temporary object into the
correct position within the base object and deletes the associated
temporary object. This operation is done using a specialized
ObjectStore primitive. In the current ObjectStore interface, this can
be done using the clonerange function followed by a delete, but can be
done more efficiently with a specialized move primitive.
Implementation of the specialized primitive on FileStore can be done
by copying the data. Some file systems have extensions that might also
be able to implement this operation (like a defrag API that swaps
chunks between files). It is expected that NewStore will be able to
support this efficiently and natively (It has been noted that this
sequence requires that temporary object allocations, which tend to be
small, be efficiently converted into blocks for main objects and that
blocks that were formerly inside of main objects must be reusable with
minimal overhead)

The prepare and apply operations can be separated arbitrarily in
time. If a read operation accesses an object that has been altered by
a prepare operation (but without a corresponding apply operation) it
must return the data after the prepare operation. This is done by
creating an in-memory database of objects which have had a prepare
operation without a corresponding apply operation. All read operations
must consult this in-memory data structure in order to get the correct
data. It should explicitly recognized that it is likely that there
will be multiple prepare operations against a single base object and
the code must handle this case correctly. This code is implemented as
a layer between ObjectStore and all existing readers.  Annoyingly,
we'll want to trash this state when the interval changes, so the first
thing that needs to happen after activation is that the primary and
replicas apply up to last_update so that the empty cache will be
correct.

During peering, it is now obvious that an unapplied prepare operation
can easily be rolled back simply by deleting the associated temporary
object and removing that entry from the in-memory data structure.

PG Log Entry modifications
--------------------------

The natural place to record the temp object name is in the pg log
entry reflecting the write.  PG Log Entries already have a system for
describing at a high level the operation being committed to allow it
to be rolled back: ObjectModDesc.  Each log entry has a rollback and
cleanup visitor methods which read the ObjectModDesc and fill in the
operations needed to either rollback the operation or cleanup after it
(once it has been committed).  The cleanup operation is essentially
there to remove the renamed objects we keep around in case of needing
to rollback a delete.  Each call to ECBackend passes the pg's
min_last_complete_ondisk as a safe value up to which the operations
can be trimmed.  We can expand cleanup to be a roll-forward operation,
and add in overwrites as some additional primitives to ObjectModDesc.
Note: it's not entirely clear how that primitive works, the
roll-forward information and what is actually written to the temp
object depend on how the backend is doing the update and probably
needs to include the checksum update.  PGTransaction takes an
ObjectModDesc ref?

Another change that will be required is expanding both object_info_t
and pg_log_entry_t to include a mapping shard_id_t->eversion_t
indicating the minimum valid version a particular shard can hold of
the object at the object_info and pg_log_entry's logical object
version. (See open questions section)

Peering/Recovery modifications
------------------------------

The delaying (buffering) of the transmission of the prepare and apply
operations for witnessing OSDs creates new situations that peering
must handle. In particular the logic for determining the authoritative
last_update value (and hence the selection of the OSD which has the
authoritative log) must be modified to account for the valid but
missing (i.e., delayed/buffered) pglog entries to which the
authoritative OSD was only a witness to.

Because a partial write might complete without persisting a log entry
on every replica, we have to do a bit more work to determine an
authoritative last_update.  The constraint (as with a replicated PG)
is that last_update >= the most recent log entry for which a commit
was sent to the client (call this actual_last_update).  Secondarily,
we want last_update to be as small as possible since any log entry
past actual_last_update (we do not apply a log entry until we have
sent the commit to the client) must be able to be rolled back.  Thus,
the smaller a last_update we choose, the less recovery will need to
happen (we can always roll back, but rolling a replica forward may
require an object rebuild).  Thus, we will set last_update to 1 before
the oldest log entry we can prove cannot have been committed.  In
current master, this is simply the last_update of the shortest log
from that interval (because that log did not persist any entry past
that point -- a precondition for sending a commit to the client).  For
this design, we must consider the possibility that any log is missing
at its head log entries in which it did not participate.  Thus, we
must determine the most recent interval in which we went active
(essentially, this is what find_best_info currently does).  We then
pull the log from each live osd from that interval back to the minimum
last_update among them.  Then, we extend all logs from the
authoritative interval until each hits an entry in which it should
have participated, but did not record.  The shortest of these extended
logs must therefore contain any log entry for which we sent a commit
to the client -- and the last entry gives us our last_update.

Deep scrub support
------------------

The simple answer here is probably our best bet.  EC pools can't use
the omap namespace at all right now.  The simplest solution would be
to take a prefix of the omap space and pack N M byte L bit checksums
into each key/value.  The prefixing seems like a sensible precaution
against eventually wanting to store something else in the omap space.
It seems like any write will need to read at least the blocks
containing the modified range.  However, with a code able to compute
parity deltas, we may not need to read a whole stripe.  Even without
that, we don't want to have to write to blocks not participating in
the write.  Thus, each shard should store checksums only for itself.
It seems like you'd be able to store checksums for all shards on the
parity blocks, but there may not be distinguished parity blocks which
are modified on all writes (LRC or shec provide two examples).  L
should probably have a fixed number of options (16, 32, 64?) and be
configurable per-pool at pool creation.  N, M should be likewise be
configurable at pool creation with sensible defaults.

RADOS Client Acknowledgement Generation
=======================================

Now that the recovery scheme is understood, we can discuss the
generation of of the RADOS operation acknowledgement (ACK) by the
primary ("sufficient" from above). It is NOT required that the primary
wait for all shards to complete their respective prepare
operations. Using our example where the RADOS operations writes only
"W" chunks of the stripe, the primary will generate and send W+M
prepare operations (possibly including a send-to-self). The primary
need only wait for enough shards to be written to ensure recovery of
the data, Thus after writing W + M chunks you can afford the lost of M
chunks. Hence the primary can generate the RADOS ACK after W+M-M => W
of those prepare operations are completed.

Outstanding Questions
=====================

Inconsistent object_info_t versions
-----------------------------------

A natural consequence of only writing the blocks which actually
changed is that we don't want to update the object_info_t of the
objects which didn't.  I actually think it would pose a problem to do
so: pg ghobject namespaces are generally large, and unless the osd is
seeing a bunch of overwrites on a small set of objects, I'd expect
each write to be far enough apart in the backing ghobject_t->data
mapping to each constitute a random metadata update.  Thus, we have to
accept that not every shard will have the current version in its
object_info_t.  We can't even bound how old the version on a
particular shard will happen to be.  In particular, the primary does
not necessarily have the current version.  One could argue that the
parity shards would always have the current version, but not every
code necessarily has designated parity shards which see every write
(certainly LRC, iirc shec, and even with a more pedestrian code, it
might be desirable to rotate the shards based on object hash).  Even
if you chose to designate a shard as witnessing all writes, the pg
might be degraded with that particular shard missing.  This is a bit
tricky, currently reads and writes implicitely return the most recent
version of the object written.  On reads, we'd have to read K shards
to answer that question.  We can get around that by adding a "don't
tell me the current version" flag.  Writes are more problematic: we
need an object_info from the most recent write in order to form the
new object_info and log_entry.

A truly terrifying option would be to eliminate version and
prior_version entirely from the object_info_t.  There are a few
specific purposes it serves:
(1) On OSD startup, we prime the missing set by scanning backwards
		from last_update to last_complete comparing the stored object's
		object_info_t to the version of most recent log entry.
(2) During backfill, we compare versions between primary and target
		to avoid some pushes.

We use it elsewhere as well
(1) While pushing and pulling objects, we verify the version.
(2) We return it on reads and writes and allow the librados user to
		assert it atomically on writesto allow the user to deal with write
		races (used extensively by rbd).

We can avoid (1) by maintaining the missing set explicitely.  It's
already possible for there to be a missing object without a
corresponding log entry (Consider the case where the most recent write
is to an object which has not been updated in weeks.  If that write
becomes divergent, the written object needs to be marked missing based
on the prior_version which is not in the log.)  THe PGLog already has
a way of handling those edge cases (see divergent_priors).  We'd
simply expand that to contain the entire missing set and maintain it
atomically with the log and the objects.  This isn't really an
unreasonable option, the addiitonal keys would be fewer than the
existing log keys + divergent_priors and aren't updated in the fast
write path anyway.

The second case is a bit trickier.  It's really an optimization for
the case where a pg became not in the acting set long enough for the
logs to no longer overlap but not long enough for the PG to have
healed and removed the old copy.  Unfortunately, this describes the
case where a node was taken down for maintenance with noout set. It's
probably not acceptable to re-backfill the whole OSD in such a case.

Thus, it would be desirable to have a way to quickly determine whether
an object shard is up to date.  I don't yet have a way of making
hashes solve this problem for me: at first glance, they seem to pose
the same problem as versions in that the primary needs to know the
current set of hashes before issuing a write (so it can include it in
the object_info_t).

If the deep scrub machinery uses a crypto hash and a merkle tree, we
could always choose to query the merkel tree tower from the modified
block to the root along with the block from any replica we read from
(with parity-delta and replica splay optimization, the replica does
it).  The update sent to the affected shards would then overwrite the
hashes of the known shard hashes on each target, but leave the
unmodified ones with whatever the shard previously knew.  Thus, by
gathering K of those mappings and taking the highest version in each
slot, we know the current hash of each shard.  The log entry could
store the top level hash for each object?  That could be used during
backfill to solve the problem I think?  Hmm, if this suffices for
backfill, we can put the same information in the log entry and
reconstruct the missing set with it?  Wait, no, missing set
reconstruction is strictly local, so can't query K shards.

What about object_info updates and xattrs?  We could bump the version
for those since all replicas need to witness them anyway (actually,
they don't, but it's not worth optimizing for?), but not pure data
writes (distinguish in the log entry?).
