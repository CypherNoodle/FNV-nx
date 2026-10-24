# Steam callback registration boundary

## Import coverage from the original PC EXE

The original `FalloutNV.exe` import table contains 11 `steam_api.dll` names:
`SteamFriends`, `SteamAPI_Init`, `SteamAPI_UnregisterCallback`,
`SteamAPI_Shutdown`, `SteamUser`, `SteamAPI_RunCallbacks`,
`SteamAPI_RegisterCallback`, `SteamUserStats`, `SteamApps`,
`SteamAPI_IsSteamRunning` and `SteamUtils`. This provider exports
`SteamAPI_RegisterCallback`, `SteamAPI_UnregisterCallback`, `SteamAPI_Init` and
`SteamAPI_IsSteamRunning` and `SteamAPI_RunCallbacks`. The other six names
are not implemented: the five interface accessors and `SteamAPI_Shutdown`.
An import-table entry alone does not show that the guest calls it on startup;
runtime reachability still needs to be observed. Do not fill these gaps with
fake success or pretend to provide Steam interfaces. The supported `Init`
failure path returns false because this port has no Steam client.

The captured PC startup reaches `SteamAPI_RegisterCallback` before any
demonstrated engine frame. The native provider implements registration and
unregistration and the unavailable-client result of `SteamAPI_Init`. It does not implement a Steam client, successful initialization,
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

Both registration exports are void **cdecl**: they pop only the return address. The native
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
An additional 2,000 translated `SteamAPI_Init` calls check false, caller stack
and LastError preservation, including full-registry calls that must not alter
any registered object or host count (12,240 translated calls in total).
Six separate negative executions cover null/out-of-range objects, unsupported
flags, foreign registration, rebinding and capacity exhaustion. The provider
also compiles for ARM64/libnx with warnings as errors.

These tests do not execute Valve's DLL and do not establish Steam compatibility
or a game frame. The desktop captured-startup adapter includes this provider in
both the static import and dynamic module catalogs. The platform refresh tool
requires a current synthetic proof, rebuilds the provider separately and rejects
a baseline that already links a provider of the same name. Existing game objects
remain frozen; a corrected generated unit needs its own replacement provenance.
The private Switch CRT probe now links this provider into its native import
catalog and static IAT bindings, and tears it down after the guest thread and
module registry detach. Its builder requires a current passing synthetic proof
whose source hashes match the provider and host test. This only puts the same
bounded failure-path behavior on the Switch probe; it does not emulate Steam
or make `SteamAPI_Init` succeed.

The private captured-startup experiment rebuilt all 12 platform units and
reused 459 frozen objects, then explicitly replaced the independently audited
copy-routine unit. It rechecked all 1,080 ARM64 math cache answers before
execution. The next import stop moved from `SteamAPI_RegisterCallback` to
`SteamAPI_Init` after 171,870 indirect calls (1,082 cache hits, no misses).
The replacement source/object hashes and baseline identity are recorded in a
separate private experiment report; it is not a cold-EXE or Switch build proof.
Four diagnostic workers are marked failed, and process cleanup remains busy.
The worker counter increments when the process is faulted at worker completion;
it does not establish four independent worker faults. Callback registration
does not resolve that diagnostic cleanup limitation.

## Unavailable Steam client

Valve's [SteamAPI_Init contract](https://partner.steamgames.com/doc/api/steam_api#SteamAPI_Init)
specifies false when no Steam client is running. This provider has no Steam
client transport or interfaces and returns false on every initialization
attempt. The export takes no arguments and returns a false bool in EAX/AL using
cdecl. It creates no interfaces, session or events and preserves the registry.
This covers the failure path only; it does not establish successful game startup.
There is no configuration to force initialization to succeed or skip guest checks.

A fresh private captured-startup run with this result reached an ordinary
`RegOpenKeyExA` installation-key query after 171,875 indirect calls. Thus this
observed guest path does not immediately terminate on Init(false). It does not
prove that later Steam-dependent functionality works or that cold startup can
run without Steam. The executable, guest branches and ownership checks were
not patched.


## Guest client-state query

SteamAPI_IsSteamRunning returns false, consistent with the guest environment
having no Steam client. It does not inspect, start or modify Steam on the PC
host. It uses its own registered native token and preserves the zero-argument
cdecl ABI. Missing provider is still an explicit fault. No ownership,
authentication, interface, callback event or successful initialization is added.

The synthetic translated test adds 1000 IsSteamRunning calls alongside 2000
Init calls and callback registry checks; it verifies the returned false value,
caller-owned stack and unchanged guest LastError. This is a no-client contract
test, not a differential test against a running Steam client.

https://partner.steamgames.com/doc/api/steam_api#SteamAPI_IsSteamRunning


RunCallbacks is also exported for the same no-client environment. Init cannot
succeed and there is no event source, so the pump delivers no events and leaves
all registered callback objects and host registrations unchanged. It preserves
void cdecl behavior and does not invoke guest code. A future real event source
must replace this path with a tested dispatcher rather than silently dropping
events. 1000 translated calls are checked while128 registrations remain live,
with complete object-byte and registration-count checks before unregistering.
