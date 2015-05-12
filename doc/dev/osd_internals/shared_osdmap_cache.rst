==============================
Shared OSDMap cache
==============================

High density chassis might have 60 osds and far less than the 120 GB
of ram we would normally suggest.  There are reports of 4MB OSDMaps on
large cluster leading to the default 500 OSDMap cache taking up 2GB of
memory per OSD process, or 120 GB total for a 60 drive chassis.  The
fact that all 60 must be caching the same maps presents us with an
oportunity to make the cache 60 times more efficient if it can be
shared across OSD daemons on the same node.

Proposed Design
---------------

I suggest that all OSDs on a single host access cached maps from a
common chunk of shared memory.  There are some challanges here:

1) What manages the shared memory?
2) What happens if the process responsible for management dies?
  - What happens if the manager dies mid-update?
3) OSDMap itself currently uses normal stl structures which don't
naivelly work when mapped at different offsets.

boost::interprocess provides a shared memory library and some stl-like
structures which use only shared-memory friendly boost offset
pointers.

A new config option osd_shared_map_cache_sock_location will enable the
shared cache.  On startup, the OSD will attempt to create a socket at
that location.  If it succeeds, it will be the shared map cache
server.  Otherwise, it will connect to it as a client.

The Server will handle updates to the cache by accepting commands over
the socket from Clients and returning handles to cached maps (which
then need to be cached in the client OSD process to avoid
re-requesting maps).  The client interface may be something like

IPMapCache::MapHandle create(epoch_t epoch, const bufferlist &bl);
void close(epoch_t epoch);

When invoked, create() will request from the Server a handle to a
cached version of the map at epoch either using bl, or using a
previously cached version.  The Server will ensure that the handle
remains valid until the Client calls close() (~MapHandle()?).

- What if the Client dies without calling close()?

We cannot bound the amount of memory used by the Server, so we can't
really pre-allocate a region of sufficient size.  Instead, we'll
exploit the fact that OSDMaps are immutable and that generally
speaking we are allocating and cleaning up in an FIFO fashion to
instead allocate in chunk_size chunks (larger than any OSDMap!) and
allocate into that region until it fills.  We then simply collect the
region once all maps contained in that region have been trimmed.

This seems to satisfy our crash safety requirement so long as when a
new server takes over it always chooses to start with a new segment.
Even if the Server dies, the other Clients will still have the
relevant regions mapped, and they will be destroyed when the last
reference goes away.

Gotchas
-------
- Embed the current version string in the socket name to avoid compat
  issues!
