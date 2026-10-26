# Resource identity lookups

The backend previously searched up to five resource arrays for every pin and
unpin. A desktop loading snapshot placed the renderer inside this path during
draw retirement. That observation identifies a candidate CPU cost; a single
snapshot does not establish its fraction of frame time.

A bounded 128-entry direct-mapped cache now remembers successful identity
lookups, including their resource kind. A known resource of another kind can
therefore be rejected without scanning an unrelated table. Collisions fall
back to the original table scan. Unknown IDs are not cached as absent.

Each hit checks the identity still present in the resource slot. Collection
clears that identity, and slot reuse installs a new identity, invalidating old
entries without changing reference counts, pins or fence retirement. Arrays
remain at stable addresses until backend destruction; access uses the existing
backend guard. The cache does not retain a resource or alter its lifetime.

`test_vk_resource_lookup.py` exercises the actual backend find and pin functions
at capacity 8192, compares them with uncached searches, and covers cross-kind
queries, collisions, cleared slots, stale IDs, slot reuse and zero/unknown IDs.
Its lookup-only timing can also be run against a saved baseline implementation.
It is not a game benchmark or evidence of a Switch FPS improvement. Pixel and
Reset regressions and strict libnx compilation are separate checks.
