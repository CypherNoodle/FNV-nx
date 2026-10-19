# Native CRT startup services

The built-in Windows catalog now has 89 exports, separate from module, Virtual*,
graphics and input catalogs. Six additions described here unblock calls in the private
[captured CRT experiment](captured-startup.md). They do not establish engine
startup or executable cold boot.

Four more exports cover [character conversion and code-page queries](native-windows-charset.md).
Three cover [western classification, decoding and case mapping](native-windows-nls.md).

GetModuleFileNameA/W reads the copied main-image path from the process parameter
configuration, for NULL or the guest image base handle. The caller must supply
the intended virtual path; the service never queries a host executable path.
Other image handles return error 126. The current catalog's native API providers
have no corresponding guest DLL file image. Truncation includes a terminator,
returns the supplied capacity and sets error 122. Success clears LastError.
The W variant initializes the first UTF-16 unit even on a failed lookup; A leaves
the caller buffer intact on that failure. Modern Windows behavior is modeled,
not XP's unterminated truncation. Missing configuration or invalid guest memory
faults explicitly. Tests compare all capacities around the actual Windows test
executable path, both main-image handle forms and invalid modules; counts vary
with the private test path length.

SetUnhandledExceptionFilter stores an opaque guest filter pointer under the
process lock and returns the previous registration, including NULL reset. This
is registration support only: it neither installs that pointer as a native OS
handler nor implements guest exception delivery, unwind, resume or callback
execution. Unsupported guest exceptions still stop explicitly. Tests compare
5,000 registration/replacement operations to Windows and verify process reset;
they do not trigger exceptions or claim working exception handling.

`GetStdHandle` models a GUI process without a console or inherited standard
handles. The three documented selectors return NULL while preserving LastError.
Other selectors return INVALID_HANDLE_VALUE with error 6, as observed on Windows.
`STARTUPINFO32` A/W also reports zero standard handles without STARTF_USESTDHANDLES.
There is no SetStdHandle, console attachment or standard-stream redirection.

`SetHandleCount` follows the obsolete Windows NT behavior: return the requested
UINT, leave LastError and the process handle table unchanged. This does not
resize native file or synchronization pools.

`GetEnvironmentStringsA/W` returns independent process-heap allocations of an
immutable virtual environment supplied at process creation. The host configuration
is an ASCII NUL-separated NAME=VALUE block with an extra terminating NUL, at most
65536 bytes. NULL/zero chooses an empty environment. Configuration is copied;
its backing can be released or changed after create. Ordinary names must be
nonempty and contain no equal sign. Values can be empty or contain equal signs.
Non-ASCII text and drive pseudo-variables are rejected at creation. No implicit
host paths, credentials, environment or locale are imported.

For this ASCII contract OEM bytes and UTF-16 code units agree. Full OEM/Unicode
conversion, environment mutation, GetEnvironmentVariable and CRT locale services
remain pending. Allocations preserve LastError on success, or return NULL/error 8
on exhaustion. FreeEnvironmentStringsA/W requires the corresponding live allocation
and frees it under the heap lock. Invalid, interior, already freed or mismatched
pointers return false/error 87 as a defensive policy outside the documented
matching-API contract. Those errors are not claimed as a Windows oracle result.
LocalFree only releases its own command-line blocks, not environment blocks.

`tests/test_windows_stdio.py` compares 27 selector/LastError samples and 21 handle
count samples against actual Windows in a detached process. Translated x86
callers repeat 1728 standard-handle checks and 1344 handle-count checks, also
checking stack cleanup, preserved registers, STARTUPINFO and unchanged handles.
`tests/test_windows_environment.py` checks 2000 A/W copy/free cycles, independent
snapshot contents, empty blocks, malformed configuration, allocation exhaustion,
allocation ownership, matching frees and no live blocks after cleanup. A native
Windows environment block confirms two selected ASCII values and successful
Get/Free LastError preservation; it is not a differential oracle for the full
environment or unsupported encodings. Optional ARM64 fixture compilation does
not execute any of these tests on Switch.

References: [GetStdHandle](https://learn.microsoft.com/en-us/windows/console/getstdhandle),
[SetHandleCount](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-sethandlecount),
[GetEnvironmentStringsW](https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentstringsw),
[FreeEnvironmentStringsW](https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-freeenvironmentstringsw).
