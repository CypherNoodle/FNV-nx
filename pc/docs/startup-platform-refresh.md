# Refreshing the private native startup platform

`advance_startup_shell.py` rebuilds the native platform and host catalog adapter
of a complete stopped diagnostic when integrating the virtual profile folders.
It checks EXE/SDK identity, current profile and synchronization regression
proofs, and an explicit set of permitted source changes. It rebuilds every
linked native platform translation unit, including users of the updated Windows
header. Reused game and arithmetic objects retain their original hashes.

Before reuse, quoted-header dependencies are traversed from each compiler unit
and the forced runtime header. A dependency on any modified platform header
rejects reuse. Unknown quoted includes also reject reuse. A shared cache of
already-verified dependency closures avoids repeatedly scanning large generated
headers. The existing narrower refresh tools retain their original restrictions.

The diagnostic accepts an explicit existing `--file-root`. For a desktop test,
use a private directory containing copies of the needed root files and a read
view of the user's assets. The guest file provider confines mutations to its
virtual profile. This keeps `UserData` in the diagnostic root. File content and
the selected root affect the experiment and must be recorded with its results.
This option does not establish a console memory layout or filesystem benchmark.

`replay_startup_math.py` preserves the selected file root. Its optional
`--seed-replay` accepts a prior replay proof only when the target evaluator's
model and complete source/image identities match. The canonical cache and its
record count/hashes are checked. Every cached answer is then executed again on
the exact ARM64 evaluator and compared before the cache can be used in the new
host diagnostic. Changed inputs still miss and stop. This avoids restarting the
host separately for each previously verified mathematical request.

`test_startup_shell_advance.py` checks forbidden game/header changes, direct
and transitive dependencies, unresolved includes, stale regression proofs,
storage-root preservation, damaged caches and incorrect target answers.
All observations, request states, generated C, objects, caches, executables and
game files remain private under `local/`. These tools establish bounded startup
control flow; they do not establish cold EXE boot, an engine frame or hardware
performance.

MinGW launched by an absolute path still needs its runtime DLL directory in
`PATH` for child tools such as `cc1.exe`. A missing compiler `bin` directory can
make the GCC driver exit with status 1 and empty diagnostics. The combined
Windows synchronization fixture also needs an 8 MiB PE stack reserve; its old
2 MiB reserve overflowed before producing test results.

Folder semantics: [Microsoft SHGetFolderPathA](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shgetfolderpatha),
[Microsoft CSIDL](https://learn.microsoft.com/en-us/windows/win32/shell/csidl).
