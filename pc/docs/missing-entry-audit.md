# Missing entry census

`audit_missing_entries.py` reads an identity checked complete static generation
and its original captured analysis container. It does not change the function
index or executable, add registrations, translate new functions, or replace
runtime errors with successful returns. Detailed reports stay private.

The first narrow census considers a missing target only when it is outside
all indexed functions, immediately follows a completely decoded indexed
function ending in RET, and fills the gap to the next indexed entry in at most
512 bytes. The entire gap must decode as straight line code with a final RET.
Interior branches, traps, privileged instructions and indirect calls reject
this pattern. An actual direct call/jump to the target must also be found in
the original bytes of a referenced indexed caller. Decoding alone is not
treated as proof of an entry or of its ABI.

The current 648-target census yields one candidate under those restrictions.
Another 599 targets overlap indexed functions, 14 lack the preceding RET
boundary, two fail the complete RET-body rule, and 32 lack a following indexed
boundary. The single candidate matches the next stop observed in the private
CRT experiment. Registering it, validating its ABI, handling internal entries,
and proving the complete constructor traversal remain separate work.

`test_missing_entries.py` uses only synthetic x86: it checks a valid gap and
original direct transfer, 14 rejected gap shapes and three rejected transfer
claims, and verifies that inputs remain unchanged. These checks do not prove
whole-program reachability or engine execution.

# Reusing compiled diagnostic units

`startup_object_reuse.py` permits the host CRT probe to reuse an old unit only
with identical compiler invocation/binary, flags, shared ABI/SDK headers,
generated headers, source bytes and object hash. Changed sources are compiled
again. It does not reuse the old arithmetic core: the caller independently
validates and links the freshly selected core. The resulting diagnostic is
still executed and records its real fault or return.

`generation_object_reuse.py` applies equivalent checks to generated Switch C.
When rebuilding an extended core in a new output directory, only its generated
`platform.h` may relocate, with identical bytes pinned by both core proofs.
Other header paths and hashes must match exactly. The compiled CPU mode must
also match, including the optional numerical model. Native CPU/PE units are
compiled again and the relocatable link and TLS checks run again. This reduces
development iteration time; it is not an in-game performance optimization.
