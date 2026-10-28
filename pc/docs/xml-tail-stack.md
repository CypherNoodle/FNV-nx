# Bounded native stack for a translated markup loop

HW11 continues reading assets through 140 seconds (about 80 MB, zero read errors),
then faults on the main native thread. The exact ELF maps the PC to the sampler
prologue storing into a guard page. The native frame chain repeatedly alternates
two translated frameless fragments inside the markup/XML parser. The guest ESP
remains near its parser frame while the host/native stack is exhausted.

The generated source models an original loop using a tail/fallthrough call from
one fragment to another followed immediately by return. The O1 compilation uses
CALL26 for both cycle edges, so native frames accumulate for each iteration.
Increasing the host stack would only postpone this problem.

`build_tail_optimized_unit.py` recompiles one unchanged generated unit with the
sole extra flag `-foptimize-sibling-calls`. It requires both baseline CALL26 edges
as negative controls and replacement JUMP26 edges in actual ARM64 relocations.
Original and copied source hashes must match exactly; no generated C bodies,
SDK translation, CPU state ABI or arithmetic instructions are patched by hand.
The compiler may optimize other eligible tail calls in that compilation unit.
This is compiler/source/machine-edge verification, not a new semantic-equivalence
proof for every game function or a successful hardware run.

The CPU linker admits this distinct certificate only with the exact known flag,
identical original/replacement source hashes, verified edge inventory and the
normal compiler/SDK/object/source pins. Tail-optimized units are counted separately
from the two existing CFG copy corrections. Negative admission tests reject an
extra fast-math flag, changed source, missing edges and wrong native edge type.

The sampler is also split into a small throttling entry and a noinline recorder.
The old prologue reserved its full snapshot frame even on calls that returned
without recording. The new large frame is entered only for an actual sample.
Stack readability uses the Horizon memory map as well as guest stack bounds:
main/worker stack arenas sit outside the dynamic VM manager, so asking only that
manager had incorrectly suppressed every hw11 stack snapshot. This changes
observations, not the guest memory permissions.

HW12 includes the optimized unit and both diagnostic repairs. It does not force
menu visibility or skip asset parsing. The actual Switch run must establish
whether parsing now finishes and the menu options become visible.
