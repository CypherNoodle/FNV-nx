# FLDCW control-word normalization

The stack runtime normalizes reserved FLDCW input bits before checking the
supported exception and precision modes. Native 32-bit x86 readback for every
16-bit input word is `(input & 0x1f3f) | 0x40`. In particular, requesting
`0x023f` reads back as `0x027f`; rejecting that request prevented the bounded
CRT diagnostic from progressing.

`test_x87_control_word.py` builds the readback-only native oracle and the
actual ARM64 stack implementation. It compares all 65,536 inputs: 768 masked,
supported precision requests succeed with the native normalized word; 64,768
unsupported requests retain the previous complete state. Twenty additional
pending-exception, invalid-TOP and null-pointer cases preserve state. Untouched
registers, status, stack occupancy and output canaries are checked. The oracle
does not perform arithmetic with unmasked exceptions enabled.

This change does not implement Windows exception delivery, reserved precision
mode, unmasked arithmetic or additional environment-restore semantics. Those
preconditions remain explicit errors.

The private candidate CRT relink passed the previous FLDCW stop, advancing
from 260 to 559 calls before reaching a missing indexed function. It retained
all guest objects and the deliberate engine boundary trap. Its report is
`CANDIDATE_ONLY_NOT_PACKAGING_EVIDENCE`: it cannot replace a fresh matched
production core, generation, constructor observation or Switch test.
`probe_startup_control_candidate.py` preserves this narrow diagnostic method;
its input sources must match the historical pre-fix baseline exactly.

The complete Switch CPU build preceding this correction compiled every unit
and passed a validator-only recheck after adding newlib's `memcmp` to the
permitted platform symbols. The recheck pins the original profile, core,
source/object hashes, compiler mode, archived validator and relocatable link;
it cannot substitute another core. This historical build predates the FLDCW
fix and does not demonstrate engine execution. New matching proofs are needed
after the runtime change. There is no new hardware performance result.
