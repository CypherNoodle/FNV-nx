# Private captured-code startup experiments

These tools test selected captured code against the native runtime. They do
not establish a cold entry from the executable on disk, a DRM-free execution
profile, engine startup, or Switch compatibility. The original installation
and analysis database remain read-only. All generated C, addresses, reports,
compiled objects and executables must remain private.

`tools/test_captured_cookie.py` requires explicit bounds and cookie/complement
addresses. It verifies a complete bounded leaf with exactly five normal IAT
calls (system time, process/thread ID, tick count and performance counter).
The host delegates those calls to the real native bridges and records their
values to check the cookie calculation and complement. It checks preserved
registers, stack, frame shadows, thread context and LastError across 3,000
cycles, including 1,000 preseeded valid-cookie cycles. `--without-overlay` is
an expected failing control for EBP restoration. Optional `--arm-cc` compiles
the generated leaf for ARM64; it does not execute it there.

`tools/probe_captured_startup.py` is a bounded diagnostic experiment. Supply
the installation, existing analysis, verified helper report, external SDK and
Python dependencies, a fresh output directory and an explicit analysis entry.
Its `--budget` bounds selected functions to at most 256. `--include` prioritizes
an existing function observed as an indirect target. `--stop-at` leaves an
existing function as a diagnostic trap and excludes its descendants.

An optional `--entry-end` permits only a complete 10-byte direct call followed
by a direct tail jump, with both destinations already in the function database.
This is a private wrapper experiment, not production entry registration. The
report explicitly records `cold_entry_verified: false`.

All known function boundaries remain visible to translation even when only
a subset is compiled. In particular, an excluded continuation of a split
hotpatch function must trap instead of returning silently. References in
direct-call macros are included when generating declarations and traps.
Incomplete decodes and rejected translations become explicit traps.

The probe binds known native Windows, module and memory bridges. Every other
normal import is an explicit diagnostic fault, never a successful fallback.
The report separates implemented imports from trap imports. Dynamic module
lookup advertises only the actual native catalog. A known module with an
unsupported export may legitimately return not-found and exercise the CRT's
fallback path. Binding all import slots does not implement all those APIs.

The host supplies isolated process parameters, TLS, stacks, a process heap
and VM-backed private heaps. It records the last indirect call site, register
state, trace, heap counts and cleanup result. A fault can leave live private
heaps; the isolated host retains their storage until OS exit, rather than
freeing an arena still referenced by the process runtime. This is not a clean
guest shutdown. Host subprocesses have a 120-second timeout; generated guest
memory accesses remain unchecked, so run only as an isolated diagnostic.

Current evidence: the cookie leaf passed on Windows desktop and compiled for
ARM64. A broader captured CRT experiment reached an explicit missing-import
fault at `WideCharToMultiByte`, after passing the standard-handle and environment
bridges and creating two heaps and three live blocks. It did
not reach the engine entry or produce a game frame. Earlier bounded probes
that omitted split continuations are exploratory and cannot support startup
claims. Neither experiment has executed on Switch.

The environment in that experiment is explicitly empty. Normal launch still
requires an audited process configuration; no desktop environment is inherited.
See [native CRT services](native-windows-crt.md) for implemented contracts and
their narrower validation scope.
