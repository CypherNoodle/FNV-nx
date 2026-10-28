# Horizon x87 lock contention

Live GDB samples on the hw15 console build found the guest main thread and
two workers repeatedly acquiring the same SoftFloat guard. A fourth worker
was inside a conversion or comparison protected by that guard. Across several
samples that worker stayed at the same instruction while the waiting threads
continued executing their atomic exchange loops. Other host threads were in
system calls. This is consistent with priority starvation around the guard;
the samples do not establish that it is the only loading or performance issue.

`x87_extended.c` now uses the Horizon newlib blocking lock API when compiled
with `__SWITCH__`. All seven previously guarded operations still serialize
access to the shared SoftFloat control and exception state. The arithmetic,
rounding, exception mappings and non-Horizon implementation are unchanged.
Do not remove exclusion: the SoftFloat build still has process-global state.

`pc/tests/test_x87_mutex.py` recompiles this branch with a Windows blocking-lock
shim and runs four workers using different rounding controls. It checks 80,000
composite cases and 480,024 lock acquisitions against serial expectations.
Removing the shim's exclusion must make the test fail. The tool also compiles
against real Switch newlib headers and checks the 4-byte lock ABI and blocking
lock imports. The shim does not verify Horizon scheduling or prove a loading
fix. A new full NRO and console comparison remain necessary.

The hw15 ELF was removed from the private build directory before this session.
Initial GDB identification used live disassembly and the retained own x87
object. Artifact recovery subsequently reproduced the delivered hw15 NRO
byte for byte, providing a new ELF with matching executable contents. Its
symbols confirmed the three waiters in `fnv_x87_from_binary` and
`fnv_x87_to_binary`, with the other worker inside `extF80_lt`, called from
`fnv_x87_compare`. The regenerated ELF's file hash differs from the deleted
ELF; its own hash is recorded alongside the exactly reproduced NRO.

`pc/tools/build_switch_engine_artifact_delta.py` is a separate diagnostic
recovery path for retained objects. It requires an exact baseline NRO
reproduction, verifies the old object and archive hashes, reproduces both
intermediate links, and permits only `x87_extended.o` to differ in the
replacement core. It then links and packages hw16, retaining its ELF and map.
It does not reconstruct missing historical audit receipts or claim that the
generated game sources were freshly audited. That limitation is explicit in
the output receipt. The ordinary source-audited builder keeps its checks.

`pc/tests/test_engine_artifact_delta.py` verifies that a changed baseline,
an additional replacement object, or a bad object digest is rejected before
packaging. Keep each delivered NRO with its exact ELF and build receipt.
