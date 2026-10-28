# HW13 loading stop and nonblocking HW14 diagnostics

The user confirms that the menu no longer closes after the profile-write fix.
Starting a new game still stops: after skipping the movie the loading spinner
moves about half a turn, stops, and remains stopped for minutes until automatic
console suspension. The application was then closed manually. The crash file
contains only its initialized marker, not a captured native exception.

At the last completed progress window (450 seconds), the session has 115,563
draws, 132,776 submissions and 6,934 presents. Pipeline creation totals 3,111 ms,
maximum 3 ms. File reads total 9,689 ms, with 4 ms of file-lock wait and no errors.
These completed intervals do not support attributing the minutes-long stop to
pipeline compilation or SD alone. They do not measure an in-flight call after
the last snapshot. The movie section presents at approximately eight per second.
No controlled-clock FPS comparison or gameplay-start success is claimed.

## Diagnostic flaw corrected

HW13's monitor takes the renderer mutex to read stats and then takes worker,
VM, file, heap, handle and critical-section locks. Any one of these can hide the
remaining logs and prevent the monitor from polling the exit combination. Its
last complete line is therefore not necessarily the game's exact stopping point.
Native-boundary sampling also used a blocking thread-name getter.

HW14 uses a try-lock renderer snapshot, copying regular stats and pipeline
metrics under one lock. On BUSY both outputs remain untouched. A separate
atomic progress snapshot requires no renderer mutex. It exposes host-side
render phases (draw preparation, geometry validation, texture upload, allocation,
shader/pipeline creation, fence wait, submit, acquire and present). These are
CPU-side API phases, not GPU utilization counters. Uninstrumented operations can
still hold the renderer lock; idle does not prove GPU inactivity.

Fence and submit counters increment before entering the driver; their cumulative
host times include only completed calls, including failed/timeout returns. The
active phase age can expose a call that has not returned. Cross-field atomic
snapshots are approximate, without an unbounded sequence-counter retry loop.
The backend must remain alive; normal guest execution retains all backing.

Windows provider stats are sampled independently with try-locks. `available_mask`
bits are files=1, heaps=2, handles=4, critical sections=8, workers=16. An unavailable
part is zeroed and must not be interpreted as zero activity. `vm_ready=0` likewise
means no valid VM snapshot. Original blocking getters retain their behavior.
The native sampler now attempts a thread-name copy and uses `<busy>` on contention.
It never waits for a name lock merely to print a diagnostic.

This removes blocking statistics/name reads, not every possible source of a
monitor delay: file logging, input/platform services and scheduling still have
normal host behavior. No fake GPU completion, skipped synchronization, forced
load completion, raised clocks, or allocation/protection changes are introduced.

## Validation

The shared Vulkan probe checks 11,264 pixels, eleven draws, ten groups, WSI and
failure rollback. During an actual fence call its proxy reads atomic progress and
attempts the regular snapshot: BUSY returns with sentinel outputs intact, without
deadlocking. A successful snapshot is compared with the original counters.
The VM permission callback similarly checks that counters return immediately and
preserve output while the VM lock is held; randomized operations and real Windows
permission checks remain enabled. Windows worker/profile regressions cover the
shared stats refactor and the owner-thread debug-name copy.

## Console run

Replace only the HW14 NRO. Start a new game, skip the movie as before, and leave
the stopped loading screen for about one minute. Hold L+R+Minus for one second
to request a diagnostic exit; if guest code cannot unwind, the monitor attempts
OS exit after another five seconds. Return both HW14 logs. If the combination
does not respond, report that too. Do not wait for automatic console suspension.
The log should identify whether a renderer call or a provider snapshot is busy.
The underlying loading stop and overall low FPS remain open issues.
