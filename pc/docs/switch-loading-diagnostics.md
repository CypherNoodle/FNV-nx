# Loading and native-call diagnostics

HW10 was the first console session with the Ranger background and audible music.
The normal log records about 20 presents/s for 80 seconds, two draws per steady
frame, zero worker failures and no native exception. Committed guest VM rises
from about 246 MB to 263 MB. These facts establish an active engine, not a ready
or correctly rendered options menu. A/+ produced no visible change.
RendererInfo identifies actual NVK/Tegra X1 and shader package 4; it does not
prove the UI is ready or identify which loading task remains.

A prior desktop diagnostic also presented a background while its menu-visible
state remained false, then made the menu visible later. That is a hypothesis to
check on Switch, not evidence that waiting alone fixes the current session.

HW11 adds the existing file, heap, handle-wait and critical-section counters to
each ten-second progress record. Reads/bytes/read time and IO lock time separate
actual file progress from blocked IO; cumulative allocation/lookup steps help
identify an expensive heap path. Host HID button-down bits show whether the
console received a press, not whether the game accepted it.

`cpu_native_observer.h` supplies an opt-in observer immediately before native
CPU dispatch, outside provider locks. A NULL hook is the default. Setup/reset
must occur with no attached guest threads; callbacks cannot invoke guest code,
change CPU state, or unwind. Public CPU-state headers and generated code remain
unchanged. The runtime test exercises 1,000 concurrent synthetic calls, compares
CPU state before/after observation, disables the hook and verifies silence, and
retains all existing fault/peer-stop cases.

The Horizon observer throttles independently on each guest thread to ten seconds.
The owner copies its own GPR/stack snapshot after validating configured stack
bounds and readable Horizon memory mapping. It logs guest thread ID/name, native token, static-import
name when available, ESP/EBP, total indirect-call count and sixteen stack words.
It never reads another thread's volatile TLS or calls guest code. Stack words
are candidates for later call-site analysis, not a validated unwound call chain.

These are last-native-boundary samples, not exact current PC, stack sampling of
arbitrary guest code, per-core utilization or proof of deadlock. Long pure guest
loops do not emit samples. COM method tokens have no static import name.
Logging and callbacks add diagnostic overhead; this build is not a performance
benchmark. No menu-visible flag, loading state or engine result is forced.

For the next console session keep the same game files and allow up to five
minutes at the Ranger background if it continues presenting. Return the hw11
normal log and crash log if it stops; note whether options eventually appear.
Audio remains enabled. There is no claim yet that the missing options are fixed.
