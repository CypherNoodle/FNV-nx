# HW16 observation and HW17 tail-cycle correction

HW16 still takes a long time to show menu options and does not complete new-game
loading. It now leaves a native exception record. Its last heartbeat is around
250 seconds, with the independent observer still updating and submissions still
increasing. That differs from the observed HW15 lock starvation; it does not
prove that the mutex has solved every loading or performance problem.

The exact HW16 ELF resolves the native crash to the entry of a translated
fragment. Its first instruction writes 96 bytes below SP. The recorded fault
address is exactly SP minus 96, and the saved native frames alternate repeatedly
between that fragment and a second fragment. The generated code represents
the backward guest jump and its fallthrough as C calls followed by return.
The ARM64 object uses CALL26 for both edges, growing the native stack for each
iteration. This is evidence of native stack exhaustion in that cycle.

The existing tail-optimization tool recompiles the unchanged private unit with
the sole additional flag `-foptimize-sibling-calls`. It verifies both original
CALL26 edges and both replacement JUMP26 edges in the machine code. The hw17
artifact build admits this one replacement only with that proof, unchanged
source hashes, matching compiler flags/SDK identity and the baseline object's
receipt. The x87 mutex remains enabled. Guest instructions and native stack
budgets are not changed. Loading completion remains a hardware test.

## Menu performance is still open

During the extended Ranger interval, the log reports about 20 presentations
per second and two draws per presentation. Recorded completed fence waits add
roughly 3-4 ms per second. These are host timings, not GPU execution timestamps.
They do not explain the multi-minute delay before the menu options appear.
The native samples frequently land in Windows critical-section and interlocked
boundaries. That identifies useful profiling targets, not their total CPU cost.
The next live sample should distinguish menu initialization work from polling.
No menu FPS improvement is claimed for the tail-cycle correction, and internal
resolution control remains pending.
