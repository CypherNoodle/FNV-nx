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
