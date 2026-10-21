# Nonoverlapping instruction resynchronization

The SDK linear decoder can start in embedded jump-table data. When a trusted
case entry restarts decoding, an instruction from the new stream may cover
multiple stale starts from the old stream. Removing only the old instruction
that straddles the restart address leaves those stale starts in the merged list.
The lifter can then execute a byte inside a valid instruction as an extra opcode.

`aot_decode_resync.py` is an independent, explicit SDK overlay. Each inserted
instruction removes overlapping records across its entire byte range. A trusted
entry is decoded again even if the old stream already had a record there.
Speculative decoding stops before crossing another trusted case; incompatible
trusted instruction starts fail with an error. The final record list is checked
for overlaps. Normal decoding without resynchronization retains the SDK path.
No external SDK file or game-specific instruction body is edited.

The synthetic fixture contains only a made-up immediate, two stale instructions,
a four-byte memory load and a return. It reproduces the old overlap, verifies
corrected boundaries, reordered/duplicate entry points, unchanged ordinary
decoding, invalid size handling, installation idempotence and conflict rejection.

A private audit of the hash-crash caller found the same failure: two valid memory
moves plus a stale INC ESP record starting inside the second move. The corrected
decoder retains the two moves and removes the stale record. A separate private
call trace showed the caller's nonvolatile registers being corrupted during that
copy operation, before the bad hash length. This localizes the mechanism but does
not yet establish that a regenerated executable completes the startup path.

Invoke the wrapper with --sdk and --python-deps and pass existing frame wrapper
options (--simd-bits, --mul-flags) plus normal recompiler arguments. Keep output
in a new private generation and record this overlay's hash in its build proof.
Do not relabel old objects or cached proofs as regenerated. The currently frozen
startup executable has not yet been rebuilt with this overlay. Full function
regeneration and differential execution must precede a claimed runtime fix.
Game bytes, disassembly, generated game C and captures remain under local/.


## Regenerated diagnostic validation

`regenerate_decode_unit.py` accepts a stopped complete diagnostic, the matching
analysis container/database, an original frozen unit and explicit function
addresses. It verifies source/SDK/container identities, regenerates those
functions with the frame/SIMD/multiply and decode overlays, and writes a new unit
plus an UNTESTED proof. Other function bodies remain intact. It rejects x87
bodies because this bounded tool does not reproduce the extended-x87 profile.
Compilation and execution require separate evidence, never a renamed old proof.

A private regenerated memcpy unit compiled and advanced the captured-startup
experiment past the previous hash-loop access violation to a controlled
CPUID-provider boundary:169591calls,1082exact ARM64 math hits and no misses.
The previous binary failed after1080math records. This is a captured-text desktop
experiment, not cold EXE startup, a game frame or hardware performance evidence.

A private differential test compares original x86 execution under Unicorn with
the regenerated C routine. It passed4637cases, including all4608tested disjoint
copies across16x16alignments and18lengths, plus29overlap cases. Checks include
all512bytes of synthetic memory, EAX, ESP and nonvolatile registers. The next
backward-overlap case (destination=source+1,length3) hits an unresolved indirect
target in the isolated generated routine; it remains explicitly unsupported,
so these results do not claim a complete memcpy implementation. The regenerated
routine also compiles with devkitA64; no Switch execution is claimed.

The reproducible tool's output was compared with the runtime-tested private
regeneration and is token-identical (formatting differs). The next startup
boundary requires an audited guest CPUID contract; it must not expose ARM host
features or enable instruction paths the translator cannot execute.

## Second copy routine reached after the registry boundary

The private captured-startup path subsequently crossed CPUID, callback
registration, Steam initialization failure and a missing installation key.
It exposed another stale decode in a second CRT copy routine: a translated
rotate wrote to an invalid guest address and caused a native access violation.
Matching the native instruction bytes to the original frozen COFF object
identified the containing function. The existing bounded regeneration tool
removed that rotate without modifying any other function body in the unit.

Original x86 versus regenerated C differential execution passed 4,637 cases
for this second routine as well (full synthetic buffers, EAX, ESP and
nonvolatile registers). The isolated backward-overlap case with destination
one byte after source and length three still needs an indirect target not
provided by that isolated fixture. The routine compiles for ARM64; neither
these results nor the existing pending instructions establish complete coverage.

A private startup experiment explicitly replaced both audited units and
rechecked all 1,080 ARM64 math answers. The native access violation changed
to a controlled unknown-target stop at `0x00EC7524` after 171,893 indirect
calls, with 1,082 exact math hits and no misses. That target is beyond the
second function's current recorded end (`0x00EC74FA`). The function boundary
and its computed-jump destinations require further audit before any alias
can be registered. No guest branch is skipped and no target is fabricated.
The experiment separately records both replacement source/object hashes;
it is not a production generation or a successful engine-startup proof.
