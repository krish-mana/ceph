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
essentially the current EC code.  READ-MODIFY-WRITE The primary
determines which of the K-W blocks are to be unmodified, and reads
them from the shards. Once all of the data is received it is combined
with the received new data and new parity blocks are computed. The
modified blocks are sent to their respective shards and written. The
RADOS operation is acknowledged.  PARITY-DELTA-WRITE The primary uses
the "W" blocks of new data to compute parity-deltas for each of the
parity blocks. The W blocks and the parity delta-blocks are sent to
their respective shards.

The choice of whether to use a read-modify-write or a
parity-delta-write is complex policy issue that is TBD in the details
and is likely to be heavily dependant on the computational costs
associated with a parity-delta vs. a regular parity-generation
operation.However, it is believed that the parity-delta scheme is
likely to be the preferred choice, when available.

The internal interface to the erasure coding library plug-ins
needs to be extended to support the ability to query if parity-delta
computation is possible for a selected algorithm as well as an
interface to the actual parity-delta computation algorithm when
available.

OSD Object Write and Recovery
-----------------------------

Regardless of the algorithm chosen above, writing of the data is a two
phase process: prepare and apply. The primary sends the prepare
operation to each OSD along with the data for that specific OSD (some
OSDs will not have any data -- they are said to “witness” the
operation). Each OSD receives the prepare operation, performs it and
then sends an acknowledgement (ACK) to the primary. The primary waits
until a sufficient number of the OSDs have returned an ACK (sufficient
is described below) and then generates an acknowledgement of the
client’s original write request operation and issues an apply
operation to each OSD. Once the apply is received, the operation is
committed -- any future recovery operation must NOT attempt to undo
this write operation. Conversely, any operation that has received a
prepare but no corresponding apply can be rolled back.

As an optimization, the primary can delay the issuance of an apply
operation for an arbitrary period of time leading to the obvious
optimization that apply operations can be batched together and done in
the background. This optimization will be particularly important for
supporting high sequential write speeds when the client cannot be
convinced to perform full write writes.

As described above, all OSD shards in the PG have some participation
in each write operation. While this is conceptually correct, the
implementation is somewhat different for performance reasons. It is
believed that most write operations which modify data in only a small
number of PG shards are important to be optimized. As described above,
each witness OSD has the transmission of its corresponding prepare and
apply operations delayed (buffered) at the primary until either an
artificial limit is reached OR an operation that actually involves
that OSD is transmitted.  The amount of buffering is a tradeoff
between efficiency of applying a larger set of updates at once and the
amount of unstable data which must be buffered on the primary for
reads.

Recovery and Rollback Details
=============================

Implementing a Rollback-able Prepare Operation
----------------------------------------------

The prepare operation is implemented at each OSD through a simulation
of a versioning or copy-on-write capability for modifying a portion of
an object.

When a prepare operation is performed, the new data is written into a
temporary object (either we extend the existing temporary object
concept to allow for objects which can outlive an interval, or we use
the generation counter in ghobject to place it next to the main object
as with deletes -- I suggest the latter course). The PG log for the
operation will contain a reference to the temporary object so that it
can be located for recovery purposes as well as a record of all of the
shards which are involved in the operation.

The apply operation moves the data from the temporary object into the
correct position within the base object and deletes the associated
temporary object. This operation is done using a specialized
ObjectStore primitive. In the current ObjectStore interface, this can
be done using the clonerange function followed by a delete, but can be
done more efficiently with a specialized move.  Implementation of the
specialized primitive on FileStore can be done by copying the
data. Some file systems have extensions that might also be able to
implement this operation (like a defrag API that swaps chunks between
files). It is expected that NewStore will be able to efficiently
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


Peering/Recovery modifications
------------------------------

The delaying (buffering) of the transmission of the prepare and apply
operations for witnessing OSDs creates new situations that peering
must handle. In particular the logic for determining the authoritative
last_update value (and hence the selection of the OSD which has the
authoritative log) must be modified to account for the valid but
missing pglog entries.

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

The current implementation keeps a running checksum
of all shards on each shard in a special xattr.  This works great for
append only, but it doesn’t work if we allow overwrites.  TBD
