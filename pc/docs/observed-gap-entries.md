# Observed x86 function gaps

These tools operate on the PC executable's captured x86 code and a frozen
function list. A missing indirect destination is evidence to investigate, not
permission to register an arbitrary address or return a fabricated success.

## Repeatable audit

1. Identify the actual indirect destination and its caller. For a native host
   exception, symbolize the faulting host PC first: the last indirect-call site
   can be historical context rather than the failing instruction.
2. Check ownership against the frozen list. An interior label is not a missing
   function. Establish an explicit upper bound before decoding a gap.
3. Use `pc/tools/audit_gap_function.py` with `--code`, `--base`, `--functions`,
   `--start`, `--limit` and `--output`. Optional `--reference` records aligned
   pointer occurrences in the original image; these are supporting references,
   not proof of a table or a valid function.
4. If an observed virtual table is involved, supply its independently examined
   address and count to `pc/tools/audit_virtual_table.py` using `--image`,
   `--table`, `--count`, `--code`, `--base`, `--functions` and `--output`.
   It requires a raw-backed, immutable, readable PE32 data section. It reports
   existing entries, interior labels, audited gaps and rejected targets
   separately. It never infers the table boundary.
5. Run `pc/tools/merge_gap_audits.py` with the same `--code`, `--base` and
   `--functions`, one or more `--audit` files and a fresh `--output` path.
   It checks input byte hashes, independently reconstructs each CFG, compares
   the report and rejects duplicate or overlapping additions. Original rows
   are preserved. The resulting private list is input to generation; merging
   alone does not install a runtime entry or prove translation correctness.
6. Generate complete bodies, reject translation stops, verify lookup changes,
   cross-compile for ARM64 and rerun the exact diagnostic. Keep all image bytes,
   reports, merged lists, generated code and linked binaries private.

The CFG audit rejects branches into instruction interiors, overlapping existing
functions, reachable traps, truncated instructions, unverified external
branches, computed jumps and fallthrough beyond the bound. Verified direct
tail exits are allowed. Calls are recorded but their callees are not recursively
audited. A reachable return does not prove termination, ABI correctness or full
semantic equivalence. Computed jump tables need a separate explicit audit.

## Current diagnostic evidence

Five observed omissions were audited and privately translated without
unimplemented instructions. Four explicit read-only virtual tables were also
checked. The complete five-function translation unit cross-compiled for
Switch ARM64. The successful intermediate host replay advanced from 209,617
to 420,922 calls before the next missing destination; adding that fifth entry
then exposed a native null-object read during graphics resource initialization.
The null read remains unresolved; it is not counted as a successful replay.

These experiments inject hardware vertex processing, shader versions and two
texture stages into a private host fixture. Production caps remain unchanged.
An earlier PS 1.3 fixture caused a null HLSL profile: this path selects PS 2.0
or newer. Using experimental SM3 removed that particular failure, but does not
establish broad SM3 backend support. Resource observations now show repeated
null returns before the eventual dereference; the preceding cause still needs
to be isolated.

There are still zero engine draws/submissions in these startup diagnostics.
They do not demonstrate cold EXE boot, a game frame, Switch presentation or
gameplay performance. No new hardware test is requested by this work.

## Synthetic checks

With Capstone installed, run:

```text
python pc/tests/test_audit_gap_function.py
python pc/tests/test_audit_virtual_table.py
python pc/tests/test_merge_gap_audits.py
```

The tests use invented instruction bytes and a synthetic PE. They cover CFG
closure, verified tail exits, pointer alignment, interior labels, malformed
tables/ranges, altered hashes, forged reports and repeated additions.
