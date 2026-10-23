# Startup assets and worker limits

The captured PC startup diagnostic is not a cold executable loader or a Switch
game build. It needs both a correctly configured private fixture and complete
runtime services. An access violation alone does not distinguish missing assets
from missing API behavior.

## Reproduced null object

The failing guest call passed a null interface object after requesting
`Data\Menus\globals.xml`. Earlier diagnostics used a fixture without game data
or the original default INI. The fallback archive list did not include the
archive containing that XML. A private comparison used the user's original
default INI and `Fallout - Misc.bsa`; neither file is distributed here.

That comparison exposed a real bridge issue: the engine passed a valid
`SECURITY_ATTRIBUTES32` with a null descriptor and `bInheritHandle=1` to
`CreateFile`. The bridge rejected every non-null attributes pointer, returning
error 50 before opening the BSA. The implementation now accepts the bounded
default-descriptor form; see [file ABI](native-windows-files.md).

The original default INI also enables screenshots, requesting a lockable
backbuffer. This presentation feature is still unsupported. The private asset
fixture explicitly disables that option rather than accepting unsupported
presentation flags. It does not modify the user's Steam installation.

## Worker starvation

After the BSA opened, native thread sampling showed the main diagnostic thread
waiting in the guest's synchronous I/O request path. All four managed workers
were suspended. The game had requested five suspended workers, another running
dispatcher and an I/O thread; the four-slot fixture could not create all of them.
Signaling the completion event would hide this failure and was not used.

The desktop diagnostic now accepts worker capacities 0, 4 and 8. Four retains
its old address layout for exact replay comparisons. Eight uses the free region
between main-thread static TLS and the dedicated heap: eight 128 KiB stacks,
per-worker TEB pages and 68 KiB TLS budgets fit without changing the heap or VM.
Runtime range validation and the eight-worker experiment accepted this layout.

With eight workers the diagnostic first stopped explicitly on `SetThreadPriority`,
instead of waiting for an I/O worker that could not be created. The subsequent
[thread-priority bridge](windows-thread-priority.md) removes that stop and the
I/O worker reads 262,144 bytes before the next missing API,
`GetCurrentDirectoryA`. The run reports API faults and peer stops; it is not
counted as a successful engine boot or a loaded menu. There are still no engine
draws or submissions in that diagnostic.

## Capability status

The experimental shader-model correction remains private. PS 1.3 had selected
a null HLSL profile in a path requiring PS 2.0 or newer. A private SM3 fixture
passes that particular branch. This does not establish full shader support:
production HWTL, shader-version and stage caps remain conservative. None of
the asset, security-attributes or worker changes enable them.

## Checks

- 2,000 translated ANSI/wide security-attribute opens and reads versus Windows;
  four nonzero/zero BOOL encodings, layout/descriptor/sandbox negatives and an
  inaccessible-attributes strict stop.
- Existing 2,000-operation file oracle, shared cursor and profile-write,
  read/write transition, flush failure and close concurrency regressions pass.
- The native runtime translation unit and synthetic callers compile for ARM64.

Host stack reservation for the Shell fixture is 8 MiB, matching the file test.
Its previous 2 MiB reservation overflowed before the suite completed. These
checks do not measure SD performance, exercise Horizon scheduling or demonstrate
a game frame on Switch.
