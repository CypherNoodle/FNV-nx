# PC borderless window initialization

The PC runtime provides a bounded borderless top-level window lifecycle with
real native backing and translated guest creation callbacks. This is not full
USER32 or a successful engine launch. The Xbox route remains paused.

## Implemented subset

- `LoadIconA`: integer IDs from the mapped executable. Walks numeric PE32
  resource directories and validates group-icon references against icon payload
  sizes. Returns a stable typed resource identity; it does not decode or paint
  the icon. Missing names return error 1814. Other module/name variants stop.
- `GetStockObject`: BLACK_BRUSH, represented as a typed black solid-brush
  identity. Other stock objects stop until implemented.
- `RegisterClassA`: copies the x86 WNDCLASSA layout and bounded ASCII name,
  verifies that its procedure is a registered translated function, and records
  supported class attributes. Duplicate names return 1410. Does not execute the
  window procedure. Extra bytes, cursor/menu resources and other styles stop.
- `UnregisterClassA`: releases a registered class by name or atom. Unknown
  classes return 1411; a class with a creating/live window returns 1412.
- `AdjustWindowRect`: visible/popup borderless styles without a menu preserve
  the rectangle. Decorated windows remain unsupported rather than using
  desktop dimensions for the Switch.

- `CreateWindowExA`: requires a configured native backend. Supports the observed
  top-level borderless subset at (0,0), a class name/atom, optional ASCII title,
  and an opaque guest create parameter. Builds CREATESTRUCTA32 on the trusted
  guest stack and sends WM_NCCREATE then WM_CREATE synchronously. Rejection
  sends the corresponding destruction messages and releases native backing.
- `DefWindowProcA`: handles those four creation/destruction messages only.
  Other messages remain explicit unsupported cases.
- `DestroyWindow`: owner-thread live windows receive WM_DESTROY and
  WM_NCDESTROY before their native object and virtual-desktop token are removed.

- `GetActiveWindow`: resolves only live native-backed windows owned by the
  current guest thread, using the backend's native queue-activation probe.
  No owned windows returns zero. No host HWND value is exposed to the guest.
- `SwapMouseButton`, `GetDoubleClickTime` and `SetDoubleClickTime` use locked
  per-guest settings. SM_SWAPBUTTON reads the same state. Defaults are unswapped
  and 500 ms; zero resets double-click time to 500 and values above 5000 clamp.
  These calls never modify the Windows host's global mouse settings. Raw
  DirectInput button ordering is unaffected by the USER32 logical swap setting.

Provider creation checks that its borrowed PE image is the CPU runtime's actual
backing allocation. Creation and destruction happen outside guest execution;
resource/class access is protected by a host mutex. Capacity is bounded to 16
icon identities and 32 live classes. Atoms are not reused during this provider's
lifetime. Resource lookup rejects invalid directories, selected duplicate IDs,
wrapped offsets and inaccessible payloads. Language selection is exact, neutral,
then the smallest available numeric language; this is a bounded policy rather
than complete Windows language fallback.

## Verification

Run `pc/tests/test_windows_boot_ui.py` with `--sdk`, `--python-deps`, `--cc`
and a fresh private `--output` directory. It translates synthetic x86 callers;
all PE resources and callback code are original synthetic fixtures.

The Windows oracle verifies resource bytes through FindResource/LoadResource,
decodes the synthetic icon, checks class registration/duplicate handling and
compares 200 borderless rectangle cases. Nine negative cases require explicit
faults. Additional cases cover provider backing identity, duplicate resource
IDs, language fallback and malformed resource ranges. Both native provider
translation units also pass strict devkitA64/libnx compilation. These checks
do not establish Switch hardware operation.

`pc/tests/test_windows_create.py` additionally compares three lifecycle paths
against real Windows: successful creation/destruction, WM_NCCREATE rejection,
and WM_CREATE rejection. Guest backing windows are real hidden Win32 windows;
message observation compares the four supported lifecycle messages, not the
whole Windows creation transcript. Checks include the x86 CREATESTRUCT layout,
nested DefWindowProc calls, FindWindow visibility, class-in-use errors, native
object release, backend failure and three strict rejection paths. The same
suite passes with the extended x87 state ABI.

The private captured-code startup diagnostic now passes actual guest window
creation and a verified current-thread naming event. It reaches
`ole32.dll!CoInitialize` after 202980 indirect calls, with 1082 arithmetic
replay hits and no misses. It uses the original PC executable identity and a
hidden native desktop window. This remains captured-code validation, not cold
startup from the disk EXE. There is no engine frame, new NRO or new HW result.
Private capture artifacts are not published.

## Horizon presentation binding and remaining boundaries

`windows_boot_horizon` supplies a single atomic lease of an initialized,
caller-owned NWindow. `fnv_windows_boot_ui_presentation` resolves the guest HWND
to that native object for the owner thread. This is the pointer type accepted
by `fnv_vk_backend_create_horizon`. The adapter does not close/resize the default
window, acquire services or create a Vulkan surface itself. Guest client
size is metadata; renderer configuration must match the actual native window.
The owner must release renderer/WSI use before destroying its window/provider.
Provider teardown must occur on the native window's owning host thread.

The adapter compiles for libnx, but is not wired into a new playable NRO or
validated on hardware. Guest D3D9 CreateDevice still needs to use this mapping;
returning a HWND alone is not a rendered game frame. Remaining USER32 work
includes visible-window notifications, resize/activation, painting and the
message queue. The supported lifecycle deliberately does not claim a complete
Windows creation-message transcript.

The existing controlled-stop diagnostic still reports worker failures and a
busy teardown. This change does not claim to repair that separate limitation.
The next observed engine import is CoInitialize. The [DirectInput8A provider](native-directinput.md)
now covers factory/enumeration, format/cooperative setup, buffers and capabilities.
Activation/focus message delivery remains window-layer work.

## Queue activation and input focus

The backend now exposes separate owner-queue activation and foreground/app-focus
probes. The desktop diagnostic selects its real hidden backing window with
SetActiveWindow; it does not force the Windows foreground window. Tests switch
the native queue selection to an unrelated native window and back, and attach
another guest thread on a real native thread to verify owner isolation.

For Horizon, the leased NWindow remains the app's selected queue window, while
input focus additionally requires AppletFocusState_InFocus. The corresponding
DInput focus probe is called outside the input/provider locks. A foreign or
invalid guest HWND is never treated as a focused input target.

This is a native-backed query model, not a complete activation/message pump.
It does not synthesize WM_ACTIVATE/WM_SETFOCUS callbacks in a query. Suspend/
resume and window-message delivery still require the host event-loop layer.
Native settings tests confirm the Windows host mouse swap/double-click values
are unchanged; they do not call the host's global setters as an oracle.

## Nested callback primitive

`fnv_cpu_invoke_stdcall` is now available to native bridges inside an active
execution boundary. It accepts a prepared guest stack frame below the active
ESP, invokes a registered translated/native target, validates stdcall cleanup
and nonvolatile registers, then restores the caller's CPU state and returns the
callback's EAX. Memory side effects remain. Register restoration includes vectors
and the selected x87 model, including all 80-bit state in the extended build.

A callback fault propagates to the outer execution boundary with its original
state; it is not converted into successful window creation. There is no nested
fault catcher. Guest thread exit likewise unwinds to the existing boundary.
Frames are limited to 256 argument bytes and nesting to 16. The invoking bridge
must validate its actual guest stack bounds and VM permissions before preparing
the frame: the CPU helper checks flat backing bounds, not page permissions.
No provider lock may be held across guest code execution.

`pc/tests/test_cpu_callbacks.py` exercises 300 invocations with 100 nested pairs,
checks whole-state restoration after XMM/x87 mutation and checks six fault
cases (target, frame overlap, return ABI, nonvolatile register, unsupported
instruction and nesting limit). Pass `--x87-build` with a verified host extended
core to repeat against the 80-bit state ABI. The extended CPU runtime also
compiles for libnx with Cortex-A57 and soft-TLS options. This primitive alone
does not by itself implement window messages or Horizon surface creation.
