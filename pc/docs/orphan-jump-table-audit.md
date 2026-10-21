# Orphan pointer-table census

`audit_orphan_jump_tables.py` inspects an identity-checked PC analysis scan.
It finds a narrow class of false `gap_prologue` entries: an unreferenced
two-byte hotpatch prefix followed by at least three executable pointers and
only established alignment padding. Each pointer must be a decoded boundary
inside an established function. The entire candidate is rejected when another
indexed entry or any recorded reference enters its range.

The additional required evidence is an original seven-byte indexed indirect
jump whose absolute table displacement equals the first pointer's address.
Its scale must be four, with an index and no base or segment override. The
dispatch must lie on a decoded local control-flow path from an established
prologue or called entry, within the bounded entry decode. Matching immediate
bytes inside another instruction, an unreachable dispatch, or a successful
decode beginning at an arbitrary offset does not qualify.

The tool produces a private candidate report. It does not edit the function
index, executable, generated C, dispatcher, or runtime traps. A candidate count
is not a reduction in unresolved calls: a separately verified selection,
generation and exact target-set comparison are required for that claim.
Retain the original scan and previous generation when testing a selection.

Use `--analysis`, `--python-deps` and a fresh private `--output` path. Detailed
reports contain game addresses and remain under the ignored `local/` tree.
The `.xbe` analysis container is an internal interchange format of the supplied
analysis SDK; the input game and target remain PC EXE to Nintendo Switch.

`test_orphan_jump_tables.py --python-deps <dependencies>` uses constructed x86
bytes and checks positive dispatch evidence, contradictory references, weak
owners, wrong scales, instruction interiors and unreachable dispatches. It
also verifies that the original synthetic inputs remain unchanged.
