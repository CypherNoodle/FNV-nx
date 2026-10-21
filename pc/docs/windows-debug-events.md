# Bounded PC thread-name events

The captured PC startup requests RaiseException with code 0x406D1388,
continuable flags, four x86 words and the 0x1000 thread-name record type.
Its name is Main, its thread ID is the current guest thread, and reserved
flags are zero. This event is a debugger naming convention, not evidence of
an application fault.

The optional windows_debug import provider treats this exact event as a
continuing diagnostic observer: it copies the bounded ASCII label to the
current thread's metadata and preserves LastError. It accepts the current
thread ID or -1. Metadata is cleared on detach/reattach. Other exception codes,
flags, counts, record forms, foreign thread IDs, non-ASCII/overlong names and
inaccessible memory fault explicitly. No general guest SEH, C++ exception
unwinder, debugger attachment or foreign-thread naming is implemented. A real
application exception is never swallowed by the name-event path.

Run pc/tests/test_windows_debug.py with the usual sdk/python-deps/cc/output
arguments. Synthetic translated callers are compared with 200 native Windows
RaiseException events observed by a continuing vectored handler. The oracle
checks the naming record, copied labels and LastError; it models a continuing
event observer, not an unhandled exception without a debugger. Nine negative
cases cover distinct exception/record/memory failures. Native Windows marks
software-origin events in the delivered exception flags, so the observer checks
the noncontinuable bit rather than requiring all native flags to be zero.
The guest API input is still required to use flags zero.

With this provider bound, the private captured-code startup advances to
DINPUT8.dll!DirectInput8Create. The naming event is not a complete SEH test or
proof of a game frame, cold EXE startup or Switch hardware execution.
