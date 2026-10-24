# Bounded DirectInput8A keyboard and mouse bridge

`IDirectInputDevice8A::Poll` is implemented for the existing virtual keyboard and
mouse. Acquired snapshot devices return DI_NOEFFECT; unacquired devices return
DIERR_NOTACQUIRED, and foreground focus loss returns DIERR_INPUTLOST while
clearing acquisition/queued input consistently with GetDeviceState/GetDeviceData.
Poll never consumes relative axes or buffered events and never queries HID per
guest call. Windows device oracles check new/acquired/unacquired states for both
devices; the synthetic1000-snapshot test checks Poll preserves movement, queued
events and controlled focus loss. Other polled-device types remain unsupported.
See [Microsoft's Poll contract](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417913(v=vs.85)).

The PC executable requests DirectInput8Create version 0x0800 and
IID_IDirectInput8A without aggregation. The original bridge implements that
factory and a guest-layout COM vtable, with host-owned reference metadata.
No Wine code or Windows DLL implementation is incorporated.

## Implemented path

- Factory, IUnknown QueryInterface/AddRef/Release, root CreateDevice,
  GetDeviceStatus and EnumDevices. Only the configured virtual system keyboard
  and mouse are enumerated; XInput controllers remain a separate provider.
- Enumeration uses 580-byte DIDEVICEINSTANCEA32 records and real translated
  stdcall callbacks. It supports ALL/DEVICE/POINTER/KEYBOARD/GAMECTRL filters
  and attached-only; GAMECTRL has no DirectInput devices in this provider.
  A temporary parent reference protects ordinary callback releases. No provider
  lock is held while guest callback code runs. Early callback stop is honored.
- Device IUnknown, SetDataFormat, SetCooperativeLevel metadata,
  Acquire/Unacquire and immediate GetDeviceState. Standard DIKeyboard,
  DIMouse and DIMouse2 layouts are validated field by field, including GUIDs,
  offsets and object instances, then retained as an owned format identity.
- Each mouse object has its own cursor in accumulated relative motion. Reading
  one object does not consume another object's motion. Repeated successful
  Acquire returns S_FALSE without discarding unread motion. Invalid snapshot
  updates preserve the previous input state and cumulative motion.

The host explicitly configures virtual keyboard/mouse devices. Their neutral
initial state is backed by snapshot storage, not by a dummy successful object.
fnv_dinput8_update publishes key/button states and relative motion under a host
mutex. Host setup/teardown must be serialized with polling; destruction requires
all guest threads detached. The caller retains a dedicated committed object
arena, separate from guest heap/image/thread regions. Optional VM backing is
validated against the CPU mapping and retained until provider destruction.

The bounded arena allows 64 lifetime objects, including roots, without reusing
stale guest addresses. Released slots are retired. Exhaustion returns out of
memory with a null output. This is a diagnostic capacity, not a demonstrated
long-session budget. Devices retain their parent root until final release.

## Switch HID feeder

`dinput8_hid` translates bounded USB key usages to physical DIK scan codes;
unknown pressed usages are counted. Modifier fallback avoids replacing a known
right Ctrl/Shift/GUI with a synthetic left key. Lock-toggle state is not treated
as a physical key press. This is not a complete international keyboard map.

After HID initialization the app must call hidInitializeKeyboard and
hidInitializeMouse, zero the poll structure and poll on one host input thread.
Up to 17 retained mouse samples are accumulated once each. Duplicate polls,
including sample number zero, do not repeat motion. Gaps beyond retained data
are counted, and large sums saturate rather than overflow. HID forward/back
bits are converted to DirectInput back/forward button ordering. Wheel values
currently retain raw HID units; physical wheel scaling needs HW verification.
Input polling commits its sample cursor only after provider update succeeds.

The feeder is available and compiles for libnx; it has not been wired into a
new game NRO or exercised on Switch. Existing Joy-Con/Pro Controller and rumble
mapping stays in XInput. There is no keyboard emulation of controller buttons.

## Explicit remaining work

Foreground Acquire requires an explicit native focus probe. Missing/invalid
probes still stop diagnostically. A valid unfocused window returns
DIERR_OTHERAPPHASPRIO. Loss during a read returns DIERR_INPUTLOST without
writing input data; regaining focus requires Acquire, clears buffered stale
input and resets the relative-motion cursor. These checks are separate from
the calling thread's GetActiveWindow selection. They do not implement queued
WM_ACTIVATE/WM_SETFOCUS delivery.

Buffered input now supports device-level DIPROP_BUFFERSIZE Set/GetProperty and
GetDeviceData with the x86 DirectInput8 20-byte event layout. A Windows oracle
confirms that the legacy 16-byte record is rejected for this interface.
Requested size is reported verbatim; actual storage is capped at 4096 events
per device. Allocation failure preserves the previous queue. Press/release and
relative-axis changes are timestamped from the same host uptime clock used by
GetTickCount, with wrapping 32-bit sequence numbers. PEEK preserves events,
ordinary reads consume them, and a null array permits querying/flushing counts.
Guest output validation uses the actual copy count, including UINT_MAX requests.
Buffered reads and immediate relative-motion reads consume independent cursors.

The bounded overflow policy retains recent events and reports DI_BUFFEROVERFLOW;
PEEK preserves that indication until a consuming read. That policy is tested
with controlled snapshots, not injected native Windows input. Exact overflow
capacity/retention equivalence to the Windows driver remains unverified. Host
snapshot updates can miss transitions occurring entirely between polls; this is
not a full OS input-event capture pipeline. Action mapping is unsupported, so
the unused event app-data field is zero.

GetCapabilities accepts the Windows 24/44-byte layouts. The configured virtual
keyboard has 256 keys and no axes; the mouse has three relative axes and eight
snapshot button slots. Both advertise attached/emulated and no force feedback.
These describe logical providers, not attached physical hardware.

Remaining methods include other properties, object/device queries, device
polling/event notification, force feedback, action mapping, Unicode/legacy
interfaces, aggregation and other data layouts. Unimplemented COM slots retain
specific diagnostics. The current engine boundary is ole32!CoInitialize after
its initial keyboard/mouse setup, before full engine initialization.

## Verification and current engine boundary

Run pc/tests/test_dinput8.py with --sdk, --python-deps, --cc and a fresh --output.
Optionally provide --x87-build with a verified native extended-state core.
Synthetic x86 callers go through the IAT and actual guest COM vtables.

Tests compare native Windows factory/refcount behavior, configured system
status, three standard formats, cooperative level and unacquired reads. They
validate 1000 controlled keyboard/mouse snapshots, cumulative-motion consumption,
repeated Acquire, malformed GUID rejection, invalid-update rollback, callback
continue/stop/filter behavior and the retired object limit. Six explicit faults
cover version, aggregation, stale object, output memory, callback and missing
foreground focus provider. USB/mouse collection checks cover modifier side, unmapped usages, retained
samples/gaps, duplicates, sample zero and forward-button ordering.

Additional checks cover focus loss/reacquisition, buffered PEEK/read/flush,
overflow indication, invalid flags, UINT_MAX requests with a one-event output,
independent immediate motion, capability layouts and native unacquired-read
errors. The suite passes with extended x87 CPU state. Both provider and HID feeder also
compile for Cortex-A57/libnx with strict warnings and soft TLS options. None of
these checks is a Switch gameplay/input result.

The private captured-code diagnostic with a real native hidden backing window
and explicitly neutral input snapshots now passes factory/enumeration, reaching
ole32.dll!CoInitialize after 202980 indirect calls (114 implemented imports,
166 traps). Arithmetic replay remains 1082 hits and zero misses. This does not
prove cold disk-EXE startup, complete engine initialization, a game frame or a
playable NRO. The existing controlled-stop worker/teardown limitation remains.

API references: [DirectInput8Create](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416756(v=vs.85))
and [IDirectInputDevice8](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417816(v=vs.85)).
