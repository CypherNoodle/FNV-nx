# Process-local named synchronization

The PC runtime now separates a kernel handle slot from its synchronization
state. Each successful named creation returns its own generation-tagged handle.
Reopening an existing name shares event/semaphore/mutex state and reports
ERROR_ALREADY_EXISTS (183); a collision with a different object type returns
ERROR_INVALID_HANDLE (6). Existing initial state, manual-reset mode, semaphore
limit and mutex initial ownership are preserved.

Names compare case-sensitively. Default and Local names share the guest session
namespace; Global names have a separate namespace inside the guest process.
The implementation does not create Windows host objects and cannot communicate
with another process. A/W ASCII names are supported, including unaligned guest
strings. Non-ASCII encodings, security attributes, private namespaces, other
backslash forms and overlong names remain explicit diagnostic boundaries.
Empty strings create unnamed objects, as verified against Windows.

Handle liveness and pending-wait pins remain per slot. Closing an alias fails
waits using that closed handle, while another alias retains shared state. The
state is released after the final pinned slot goes away. The name becomes
available again after the last live handle closes. Ownership and abandonment
remain per guest thread. Wait-all rejects distinct handles for the same object;
wait-any can select the first eligible alias without consuming state twice.

## Verification

The translated x86 synthetic fixtures compare calls against actual Windows:

- 100 named event rounds across A/W and default/Local names, reopening with
  deliberately conflicting initial/reset flags, closing the original alias,
  checking shared auto-reset state, and recreating after last close.
- Named mutex recursion, ignored initial ownership on reopen, type collisions,
  named semaphore count/limit sharing, and duplicate-object wait-all.
- Closing an alias while another handle has a blocked native thread, reopening
  the name while that wait is pending, then signaling through the new alias.
- Existing 24,000 mixed/event oracle comparisons, 300 contention rounds,
  abandonment, FIFO grants, tagged handles and generation retirement.
- 285 managed guest threads, including nested workers, suspended workers,
  close-while-running, TLS isolation and abandoned mutexes. Also run with the
  extended x87 CPU ABI used by the captured startup diagnostic.

The modified windows_thread translation unit compiles with devkitA64/libnx.
This is compilation evidence, not a Switch execution or engine frame.

## Captured startup continuation

The restored original PC entry calls CreateMutexA with null attributes,
initial-owner false and FalloutNVGameMutex. The updated private diagnostic passes
that call and its ERROR_ALREADY_EXISTS test. It advances from 166,042 to 166,044
calls, stopping at USER32.GetSystemMetrics. Inspection of the unchanged generated
body shows index 0x1000 (SM_REMOTESESSION), rather than a screen-size request.
No USER32 success stub was introduced. Cold EXE startup and engine presentation
remain unverified; the full diagnostic still uses audited captured text and
bounded exact ARM64 math replay.

refresh_startup_sync.py rebuilds only the native synchronization translation
unit of a frozen diagnostic. It rejects changes outside windows_thread.c and
its two private includes, rejects additional include consumers, requires
matching regression proofs, verifies every reused object and unchanged shared
ABI header, then links a separate executable. It does not regenerate or modify
game functions. All observations, captures, generated C, objects and executables
remain under local/ and must not be published.

References: [Microsoft object names](https://learn.microsoft.com/windows/win32/sync/object-names),
[CreateMutexA](https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createmutexa),
[CreateEventA](https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-createeventa),
[GetSystemMetrics](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getsystemmetrics).
