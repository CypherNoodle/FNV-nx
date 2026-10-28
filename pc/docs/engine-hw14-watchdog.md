# HW14 observation and independent HW15 watchdog

The last complete HW14 heartbeat is at 240 seconds. It reports all five Windows
provider snapshots available, a valid VM snapshot, no worker failures and no IO
errors. File reads total about 11 seconds. Completed fence calls total 1,246 ms
and completed submits total 2,235 ms. Those figures are host times, not GPU draw
timestamps or measurements of a call that has not returned.

The monitor then stops logging, while other threads continue printing native
samples and rejected ALPHAFUNC state requests. The crash sink contains no native
exception. A completely stopped process is not established. The main thread's
last sampled boundary is a performance-counter query; that does not prove its
current PC or explain why loading never completes.

## Changes

The existing monitor still enters host input, audio-focus, controller and display
calls. HW15 marks each stage. It no longer queries VI display resolution and
NWindow dimensions every eight milliseconds: the prepared diagnostic window has
immutable geometry and docking changes request a restart. Initial dimensions
are still queried normally. Audio-focus updates run only on focus transitions;
initial focus and subsequent transitions still reach DirectSound.

A separate 128 KiB-stack observer uses only its own FILE, HID pad state, atomic
monitor/render progress and kernel context calls. It does not enter game/provider
mutexes or use the shared normal-log mutex. It writes one record per second to
`fnv-pc-engine-hw15-watchdog.log`, in the same port folder. Shared SD/filesystem
services and host scheduling remain dependencies; this is not a hard-real-time
profiler or proof that every possible logging failure is avoided.

If a monitor stage lasts at least three seconds, or submissions stop for ten
seconds after initial rendering, it attempts at most three samples per continuous
stall. Each sample pauses the monitor and main guest separately, copies registers
and up to 24 bounded native frame pointers, then resumes the target BEFORE stdio
or allocation. The capture path uses only context/query-memory syscalls and
bounded stack reads. Kernel SVC availability is checked first; denied/unavailable
calls are reported instead of assumed to work. Registers x0-x18 may be unavailable
when a target is executing a syscall. These are native ARM64 contexts, not guest
TLS or decoded x86 register snapshots.

Resume is attempted even after a failed context read. A failed resume is retried;
if that also fails the diagnostic exits the process rather than strand a paused
target. The independent L+R+Minus handler takes final samples and exits the process
when held for one second. It does not attempt graceful game shutdown or free live
backing. Use this diagnostic exit while the load is stopped, as requested.

This is bounded stall sampling, not pausing every frame. The extra observer and
small samples still have overhead. The eight synthetic pause-helper cases verify
callback ordering and resume after capture failure; real Switch register capture
and kernel permissions remain untested until the user's HW15 run.

## Interpreting the result

Return the normal log, crash log AND watchdog log after reproducing the stopped
new-game spinner. Wait about one minute, then hold L+R+Minus for one second.
If that fails, close the application and still return the watchdog file if present.
The monitor stage should distinguish input/audio/controller/window/logging waits.
The saved PCs/LRs and frame chains can be resolved against the retained HW15 ELF.

`pc/tools/symbolize_engine_watchdog.py` requires a build validation and exact ELF
SHA before resolving ELF text symbols. DWARF/addr2line is kept separately as
advisory data: overlapping debug ranges can attribute an address to the wrong
inline function. The ELF symbol-table result is retained independently.
It rejects offsets outside ELF load ranges and
keeps symbolized game function names private. It does not recover missing PCs,
prove a deadlock, or modify the EXE/generated game code.

The ALPHAFUNC errors remain a renderer compatibility gap; this observation alone
does not establish them as the cause. Loading completion and low FPS remain open.
