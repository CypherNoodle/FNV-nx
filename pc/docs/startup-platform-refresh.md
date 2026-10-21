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


The refresh also supports changes to windows_user32.c/h. Supply --user32-test
with a passing report that hashes both provider files and current common runtime
inputs; the proof itself is frozen in the new diagnostic. A changed USER32
header participates in the same forbidden dependency closure as other platform
headers. The rebuilt adapter creates/destroys its virtual desktop alongside
SYSTEM_INFO. This extends the native platform, never the generated game code.


The desktop harness now registers an unhandled-host-exception reporter. It logs
Windows exception code, image-relative PC, access type/address, native registers,
guest registers and the last indirect-call trace. A guest-stack preview uses
ReadProcessMemory and is printed only when all48bytes are readable. It returns
EXCEPTION_CONTINUE_SEARCH; access violations are never converted to successful
guest execution. This is a Windows x64 diagnostic, not the Switch crash handler.

The expanded exact-target experiment verified1080math answers before a repeatable
host access violation0xC0000005. Matching the native instruction bytes against
the unchanged linked object localized it to generated sub_00A5B270, a guest hash
loop reading with an implausibly large length. The calling path includes
sub_00A5B690/sub_00A5B5A0. Source of that invalid length is not established;
no function is skipped and no success stub or instruction patch is introduced.
All cache records are re-evaluated against the exact ARM64 model before reuse.
This diagnosis is not an engine frame or evidence of hardware performance.


The desktop and Switch CRT adapters now select the bounded guest CPUID limit
callback. Platform refresh requires --cpuid-test, verifies the current
native/guest_cpuid.h plus common Windows runtime hashes and freezes both the
header and proof before compiling. The callback does not expose host CPU bits.
See [guest CPUID contract](guest-cpuid.md) for supported/unsupported leaves.
