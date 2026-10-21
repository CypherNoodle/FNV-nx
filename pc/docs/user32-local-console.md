# USER32 local-console subset

windows_user32.c provides an independent native export catalog for USER32.
GetSystemMetrics currently implements only SM_REMOTESESSION (0x1000). The guest
runs on a local console without Windows Terminal Services, so its value is zero.
The desktop diagnostic models the same guest environment rather than exposing
the host remote-desktop setting. LastError is left untouched by this provider;
Microsoft does not specify extended error information for this function.

Other metrics explicitly fault with the requested index. In particular, a zero
remote-session metric does not establish a display width, height, mouse presence
or window system implementation. There is no host HWND or Windows DLL loading.

The catalog is connected to the captured desktop startup harness and Switch CRT
probe, including native module lookup. Their complete builders compile/link the
new provider. The original windows_thread.h config layout and export count are
unchanged. The Switch probe's existing memory/worker/observation gates remain.

Verification:
- Three synthetic x86 wrappers bind USER32/KERNEL32 imports and execute through
  translated callers. 1,000 local-session queries preserve guest LastError and
  obey stdcall cleanup. Four separate negative processes verify that width,
  height, unknown and negative metric indices remain explicit faults.
- The Windows metric value is recorded for comparison. Initial USER32 lazy
  setup can change native LastError; that value is not a contractual oracle.
- devkitA64/libnx compile the provider and updated Switch CRT adapter with
  strict warnings. No new NRO or hardware execution is asserted.
- The unchanged captured PC entry passes its remote-session branch. The exact
  target-math diagnostic advances to 166,047 calls, next stopping at
  KERNEL32.GetSystemInfo. No engine frame or cold EXE startup is established.

advance_startup_user32.py adds this catalog to a frozen complete private host
diagnostic: it rejects changed native/shared headers, verifies all old objects,
rebuilds only the catalog adapter and new provider, and relinks separately.
Its adapter/dependency policy has four synthetic rejection checks. Generated
game code, captures, objects, replay records and reports remain private local/
files. The existing synchronization refresh tool was not weakened.

Next GetSystemInfo must describe the guest x86 environment, backed address-space
limits and deliberate guest CPU availability; it must not copy the ARM64 host
structure or guess unlimited processors. SYSTEM_INFO32 layout/pointer validation
need tests before continuing the engine.

Reference: [Microsoft GetSystemMetrics](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsystemmetrics).


FindWindowA now searches a process-owned virtual desktop. The native window
adapter can register/remove up to16 top-level windows with ASCII class/title
strings; guest handles never contain a host HWND. Search is case-insensitive,
NULL class/title are wildcards, and found/missing results preserve LastError.
Removed handles are not reused during the desktop lifetime. Window creation
through the guest Win32 API is still unimplemented: a fresh startup desktop
is empty until the native window adapter registers a window.

Create this singleton desktop before attaching guest threads and destroy it
after every thread detaches. Runtime token registration makes premature destroy
return BUSY. The registry is protected by a blocking mutex. Class atoms,
non-ASCII strings, overlong strings and inaccessible pointers explicitly fault;
there is no fallback success or unconditional no-window result.

The synthetic translated test checks positive and negative lookups, three
comparisons with a real Windows top-level window (including case folding and
NULL class), NULL title, removal/stale handles, capacity exhaustion, singleton
lifecycle, LastError and stdcall. Eight negative processes cover unsupported
metrics and query encodings/pointers/atoms/length. USER32 and the Switch CRT
adapter pass strict devkitA64/libnx syntax checks. This is not a hardware result.

Platform refresh now allows the USER32 source/header only with a fresh current
USER32 proof covering its source, header and common Windows runtime. Dependency
reuse still rejects game units that include a changed platform header; the
extended guard suite has ten tests. Older profile/kernel proofs remain valid
only while all of their recorded runtime input hashes are unchanged.

Contract: [Microsoft FindWindowA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-findwindowa).


The fresh captured-startup diagnostic passes FindWindowA and next stops at
ADVAPI32.GetUserNameA (166070 calls,56 exact ARM64 math records/58 hits).
The earlier copy run counted166083 calls; that is not a regression metric:
the private profile now already contains FALLOUT.INI, so startup takes its
existing-config branch. These are bounded desktop diagnostics with captured
text, not cold EXE startup, a game frame or Switch execution.
