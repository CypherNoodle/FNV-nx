# PC window initialization prelude

The PC runtime now provides the resource and class metadata used immediately
before the initial borderless window. This is not a window implementation or
a successful engine launch. The Xbox route remains paused.

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
  classes return 1411. No windows can yet reference these classes.
- `AdjustWindowRect`: visible/popup borderless styles without a menu preserve
  the rectangle. Decorated windows remain unsupported rather than using
  desktop dimensions for the Switch.

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

The private captured-code startup diagnostic with the original PC executable
now passes four new imported calls and stops at `USER32.dll!CreateWindowExA`:
184186 indirect calls, 1082 verified arithmetic-cache hits and no cache misses.
The requested window is 320x240, visible, borderless, and titled
`Fallout: New Vegas`. There is no cold disk-EXE startup proof, engine frame,
new NRO or new console result. Private capture artifacts are not published.

## Next boundary

CreateWindowExA must deliver the guest window procedure's creation messages,
including guest-layout CREATESTRUCTA, and support nested translated callbacks
without corrupting the caller's stack or register state. A successful HWND must
also refer to a real presentation window: Horizon's valid NWindow must back the
existing Vulkan surface adapter before D3D9 CreateDevice/Present uses it.
Returning a fabricated successful HWND would not prove either requirement.

The existing controlled-stop diagnostic still reports worker failures and a
busy teardown; this prelude does not claim to repair that separate limitation.
