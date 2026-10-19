# Local startup diagnostics

These reusable tools consume user-supplied executable and analysis inputs locally.
Neither the inputs nor generated bodies, detailed reports, captures, objects or
executables belong in this repository. Diagnostic results do not establish a
supported entry from disk, engine startup or Switch compatibility.

`tools/test_captured_cookie.py` checks an explicitly bounded cookie routine
against native runtime bridges. `--without-overlay` supplies a failing frame
restoration control. Optional ARM64 compilation does not imply ARM64 execution.

`tools/probe_captured_startup.py` requires an installation, existing analysis,
verified helper report, external SDK, dependencies and an explicit analysis entry.
Use a fresh output directory. `--budget` bounds ordinary selection; `--split`
limits functions per compiler unit and `--jobs` limits concurrent compilers.
`--include` prioritizes an existing indirect destination. `--stop-at` leaves an
existing function as a diagnostic trap and excludes its descendants.

`--include-table` revalidates an explicit initializer-table audit and selects
its known roots. Repeat the option for separate tables. Missing roots or an
insufficient selection budget cause rejection before compilation. Table
membership does not establish a calling convention or successful execution.

`--generation-name` and `--generation-validation` select exact bodies from a
complete, hash-verified generation. The SDK, source files, helper metadata and
selection proof must match. `--all-generated` requires that proof and split
compilation; it includes all known destinations while retaining explicit stops.
It does not resolve unknown targets or implement unsupported instructions.

`--captured-text-data` makes inline text tables consistent with the selected
analysis code. It checks the supplied text identity and bounds, writes only to
the isolated diagnostic image and never patches the original executable.
Other sections still come from the original input. This is not a cold-entry
profile or a way to support arbitrary executable versions.

Optional `--entry-end` accepts only the tool's restricted direct call/tail-jump
wrapper shape with destinations already present in the analysis database.
It cannot override existing functions or prove a production entry.

Local initializer observation must check cursor progression, table bounds,
local/global frames and stack movement across callbacks. Such private diagnostic
instrumentation and its detailed output are not distributed here. Separate
stable control flow from time-dependent register values. Reaching a terminal
marker alone is insufficient: traversal and return-state checks must also pass.

Known imports use actual native Windows, module and memory bridges. Unsupported
imports and unselected destinations must remain explicit faults. Import binding
is not API implementation. Unknown dynamic exports may return not-found under
the native catalog's documented contract; they must not advertise fake success.

The isolated host sets up process parameters, TLS, stacks, heap and VM state.
Host subprocesses have a timeout. Generated guest memory accesses are unchecked;
a guest fault is not a clean shutdown. Live storage may remain until process
exit. Run only as a local diagnostic, and keep all detailed output private.

See [normal EH3 helpers](native-windows-eh3.md),
[native CRT services](native-windows-crt.md) and
[native CPU runtime](native-cpu-runtime.md) for separate contracts.
