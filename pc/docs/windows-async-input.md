# Virtual async keyboard state

GetAsyncKeyState reads an explicitly supplied native virtual-key snapshot. No
snapshot means an explicit diagnostic stop, not invented neutral input. A native
producer calls fnv_windows_user32_update_keys with 256 bytes indexed by Windows
VK, each 0 or 128, and a boolean focus state. The desktop mutex serializes
updates and queries. Unsupported malformed samples leave the stored state intact.

The returned SHORT sets bit15 for held keys and bit0 for a rising edge since the
last query for that key. A read consumes only its own recent-press bit. Release
does not discard an unconsumed press; repeat samples while held do not create
new edges. Focus loss clears held/edge state and makes queries return zero.
Left/right shift, control and alt derive their aggregate VK states. Mouse-button
VKs are physical buttons, independent of the swap-button setting. Invalid VKs
outside0..255 return zero. LastError is preserved by this virtual provider; this
is an explicit policy, NOT a Windows differential LastError guarantee. The
native Windows oracle returned error5 in this noninteractive session. Microsoft
specifies the SHORT result but does not define GetLastError for this function.

windows_user32_input converts a DirectInput snapshot using a fixed US physical
layout table, plus five physical mouse buttons. DIK0x1e becomes VK_A0x41, NOT
VK0x1e. Unsupported held scan codes/buttons reject the whole update. It does not
change keyboard layout, translate Unicode text/IME, track lock-key toggles,
implement GetKeyState, or translate gamepad buttons into keyboard events.

The adapter is reusable by the native input polling loop; adding it does not
prove the final application invokes it at each poll. Desktop startup diagnostic
explicitly supplies a neutral snapshot and claims no live keyboard/gamepad
playback. Existing DirectInput/XInput providers remain separate.

Validation: 3018 controlled snapshot checks, modifier and mouse tests, malformed
sample rollback, adapter mapping/rejection,32 native Windows invalid-key SHORT
comparisons, and an uninitialized-provider negative. Synthetic IAT callers verify
stdcall cleanup. No synthetic key presses are sent to the user's desktop.
USER32 window/message/display regressions remain separate. The provider/adapter
compile for actual Switch ABI in both legacy and extended-x87 configurations.

https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate
