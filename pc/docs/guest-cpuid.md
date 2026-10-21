# Bounded virtual x86 CPUID contract

The translated game first queries EAX=0x80000000,ECX=0. `guest_cpuid.h`
implements this extended-limit query with EAX=0x80000000 and zero EBX/ECX/EDX.
It advertises no extended feature or brand leaves. The response is independent
of the ARM64 Switch hardware and the Windows development PC. ECX is ignored
for this non-indexed query. Basic leaves, extended feature/brand leaves and
all other requests remain explicitly unsupported; this is not a full virtual
CPU personality. It does not assert SSE/AVX, CPU frequency or cache topology.

The translated synthetic CPUID fixture executes1000queries with varying input
ECX/EBX/EDX, checks output register ordering and preserved EBP/ESI/EDI/stack,
checks unsupported requests do not overwrite their output array, and checks
three unsupported leaves fault through the real CPU runtime. Both desktop and
Switch CRT adapters select this callback. The Switch adapter compiles strictly
with devkitA64/libnx; no new NRO or hardware test is implied.

The native platform refresh requires --cpuid-test with current header and
common-runtime hashes. It freezes that proof and the new header in its result.
The private diagnostic with the separately regenerated copy unit passes this
query and next stops at steam_api.dll!SteamAPI_RegisterCallback after171867calls,
1082exact ARM64 math hits and zero misses. That later import remains unsupported.
No successful Steam client or ownership result is synthesized. A callback
registration boundary by itself does not establish a license-check failure.
This captured-text experiment still does not prove cold EXE startup or a frame.

The callback fixture supplies a conservative modeled limit, not a measurement
of hostCPUID. Full x86 identity and feature bits require additional audited
instruction/runtime contracts if the game requests them.

References: [Microsoft CPUID intrinsics](https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex),
[Steamworks API overview](https://partner.steamgames.com/doc/sdk/api).
