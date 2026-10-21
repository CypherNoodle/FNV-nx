# Memory budgets in the private startup diagnostic

The desktop CRT experiment is a bounded execution harness, not a complete game
launcher. Its default `bounded64` profile backs a 64 MiB guest arena, including
a 24 MiB managed VM pool with a 16 MiB commit limit. Fixed image, TLS, stacks,
parameters and heap regions occupy the preceding 40 MiB.

An application can legitimately reject this budget through
`GlobalMemoryStatusEx`. The native bridge reports the actual guest commit
limit and remaining budget; do not patch it to report fictitious host RAM.

`--memory-profile extended808` allocates an 808 MiB host-backed arena with the
same fixed regions and a 768 MiB VM/commit pool. The Windows host reserves and
commits that backing before execution. This is an explicit diagnostic capacity
change; it does not establish the same budget or behavior on Horizon.
Both profiles use unchanged generated functions, VM API semantics and import
traps. The report records the profile and actual arena, VM, commit, committed
and reserved byte counters. The runner rejects mismatched observed budgets.

When execution stops on `MessageBoxA`, the host diagnostic reads the saved
stdcall frame and bounded text/caption strings before cleanup. Bounds checks
and `ReadProcessMemory` prevent unreadable guest pointers from faulting the
host observer. Unreadable and truncated strings are labelled explicitly.
The call remains an unsupported-import trap; no successful UI response is
fabricated and execution does not continue past the warning.

The fixed Switch CRT probe still uses `bounded64`. Its packaging tool rejects
an enlarged desktop profile, even if that host observation reaches the desired
boundary. Increasing the console budget requires its own implementation and
validation. These desktop experiments establish neither cold EXE startup nor
an engine frame, audio, performance or hardware execution.
