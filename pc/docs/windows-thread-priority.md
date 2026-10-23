# Bounded guest thread priorities

The runtime exports `SetThreadPriority` and `GetThreadPriority`. Priority changes
reach the native scheduler; they are not recorded as successful no-ops. The
implemented relative levels are -2, -1, 0, 1 and 2, matching the ordinary Windows
thread-priority constants. See Microsoft's [priority reference](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadpriority)
and [scheduling model](https://learn.microsoft.com/en-us/windows/win32/procthread/scheduling-priorities).

## Handle and scheduling contract

- The current-thread pseudo-handle acts on the executing native thread.
- Other handles must belong to a live guest thread object with a retained native
  worker. Suspended workers are supported. `worker_guard` then `kernel_guard`
  protect lookup and the native handle against close/reap/reuse during the call.
- A closed, foreign, non-thread or already-reaped handle fails with error 6.
  This bounded bridge does not retain native handles indefinitely to support
  priority calls on every terminated guest thread object.
- Ordinary levels use Windows `SetThreadPriority`/`GetThreadPriority` directly
  on desktop. Native failures are returned to the guest. Guest LastError is
  preserved on success. The stdcall cleanup is 8 bytes for set and 4 for get.
- Horizon maps level `p` to native priority `0x2c - 2*p`, using the application's
  usual main-thread priority as the base. Lower Horizon numbers run first.
  Reading performs the inverse only for these five mapped priorities. SVC
  failures return access denied. This is an explicit Switch policy, not an
  emulation of Windows process priority classes or dynamic priority boosting.
- POSIX returns error 50: ordinary `SCHED_OTHER` does not provide these relative
  priorities. No process-wide niceness adjustment is substituted.
- Idle/time-critical and background begin/end modes return error 50. Other
  out-of-range values return 87. A failed get returns `0x7fffffff`.

No CPU affinity, clock settings, audio priority or renderer scheduling changes
are part of this implementation. Horizon priority behavior has been compiled
but has not been measured on the Switch.

## Verification

`pc/tests/test_windows_priority.py` generates synthetic x86 IAT callers and a
stdcall thread entry. The native test creates 100 managed suspended workers,
checks five levels against independent Windows threads, and checks the current
native thread's actual priority after each guest change. It restores the test
thread's original priority and joins all workers. There are 1,000 priority
checks, closed-handle checks and six unsupported/invalid-level cases.

The existing worker regression also passes: 285 managed threads, 1,000 Windows
oracle comparisons, explicit exit, nested creation, TLS isolation, abandoned
mutexes, close while running and eight strict failure cases. The runtime
translation unit compiles for ARM64/libnx with warnings treated as errors.

In the private eight-worker captured-startup fixture this removes the
`SetThreadPriority` stop. The I/O worker reads 262,144 bytes from the user's BSA
before stopping on the still-unimplemented `GetCurrentDirectoryA`. This run has
zero engine draws/submissions. Worker fault counts include primary API faults
and are not a successful engine boot. These observations are not a performance
comparison against the earlier four-worker run, which could not create the I/O
worker at all.
