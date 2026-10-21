# Steam callback registration boundary

The captured PC startup reaches `SteamAPI_RegisterCallback` before any
demonstrated engine frame. The native provider implements registration and
unregistration only. It does not implement a Steam client, initialization,
ownership checks, authentication, call results or callback delivery.

## ABI and implementation

Valve's [legacy Steam header](https://github.com/ValveSoftware/Proton/blob/proton_11.0/lsteamclient/steamworks_sdk_133/steam_api.h)
declares the two exported functions and `CCallbackBase`. For its Windows x86
layout, the vtable pointer is at offset 0, flags at 4 and the signed callback
ID at 8 (12-byte base). The public registration flag is bit 0; bit 1 identifies
game-server callbacks. No Valve implementation is copied into this provider.

The registry keeps up to 128 unique guest object addresses under a host mutex.
Registration sets the ID and registered flag; unregistration clears only the
registered flag, preserving the ID, game-server flag, vtable and padding.
Duplicate registration with the same ID is idempotent. Rebinding to a different
ID, foreign registered objects, unknown flag bits and capacity exhaustion stop
explicitly. These are bounded implementation policies, not Windows oracle claims.
Objects must remain valid until unregistered. Concurrent guest mutation or
unmapping of an object during an API call is unsupported.

Both exports are void **cdecl**: they pop only the return address. The native
bridge preserves EAX and guest LastError. It validates the full object range
and, for managed VM ranges, writable access before touching the object.
The provider never calls guest virtual methods during registration. Destruction
requires detached guest threads and drops host bookkeeping without dereferencing
guest objects. Each registered native function has its own token storage.

## Verification

`pc/tests/test_steam_callbacks.py` translates synthetic x86 callers that push
arguments, call through the IAT and clean their own argument stack. The native
fixture checks 10,240 calls over repeated full-capacity register/unregister
cycles, object bytes, duplicates, slot reuse, EAX, nonvolatile registers and ESP.
Six separate negative executions cover null/out-of-range objects, unsupported
flags, foreign registration, rebinding and capacity exhaustion. The provider
also compiles for ARM64/libnx with warnings as errors.

These tests do not execute Valve's DLL and do not establish Steam compatibility
or a game frame. The desktop captured-startup adapter includes this provider in
both the static import and dynamic module catalogs. The platform refresh tool
requires a current synthetic proof, rebuilds the provider separately and rejects
a baseline that already links a provider of the same name. Existing game objects
remain frozen; a corrected generated unit needs its own replacement provenance.
The Switch CRT adapter has not yet been wired to this provider.

The private captured-startup experiment rebuilt all 12 platform units and
reused 459 frozen objects, then explicitly replaced the independently audited
copy-routine unit. It rechecked all 1,080 ARM64 math cache answers before
execution. The next import stop moved from `SteamAPI_RegisterCallback` to
`SteamAPI_Init` after 171,870 indirect calls (1,082 cache hits, no misses).
The replacement source/object hashes and baseline identity are recorded in a
separate private experiment report; it is not a cold-EXE or Switch build proof.
Four earlier diagnostic workers still report failure, and process cleanup
remains busy; callback registration does not resolve those existing limits.
