# Private captured-PC engine NRO: hw01

## hw10 pull-MP3 graph

HW09's actual FilterGraph request cannot be rejected safely: the original game
reads its null output. HW10 installs the [native pull-MP3 graph](native-directshow.md)
with guest IAsyncReader callbacks, real decoding and independent music mixing.
The class/interface lifecycle and 131,072 PCM samples passed the desktop fixture.
The actual game's source object and console behavior remain to be verified.
Audio is enabled; keep EXE, Data and UserData and replace only the NRO.
Return fnv-pc-engine-hw10.log and the hw10 crash log if execution stops.


## hw08 result and hw09 COM boundary

The supplied hw08 session creates the real NVK device and records 10 draws,
18 submissions and one present, with 13 guest workers and zero worker failures.
It then stops at the unimplemented `ole32.dll!CoCreateInstance` import. The
native crash sink records no native fault. A present count is not proof of
correct menu pixels or gameplay.

HW09 implements the verified Windows rejection contract for absent COM classes
and logs the requested CLSID/IID/context/result. No DirectShow class is
registered, so FilterGraph/FilterMapper activation fails normally instead of
unconditionally stopping at the import trap. Whether the engine tolerates this
failure remains a console test. See [COM activation](native-com-initialization.md).
The user requested audible output again: hw09 sets audout volume to 1.0.
Keep the existing EXE, Data and UserData; replace only the NRO. Return the hw09
normal log and crash log if execution stops. The PC cold-boot limitation remains.


## hw04 copy correction

The hw03 native capture identified a byte-shifted register restore in a CRT
copy translation. The [bounded CFG correction](cfg-copy-correction.md) passed
3,744 original-x86 vs compiled-ARM64 cases; the legacy control failed 1,905.
One generated unit is replaced with pinned verification evidence. The hw04 NRO
reports that correction in its normal log and keeps native crash capture active.
Console verification is pending; return `fnv-pc-engine-hw04.log` and, on a
native closure, `fnv-pc-engine-hw04-crash.log`.

## hw02 native fault and hw03 capture

Atmosphere's hw02 report matches the exact ELF Build ID. Its stack reaches
the real captured `WinMain`, then the engine's string intern/hash path. The
faulting AArch64 instruction is a signed byte load in the translated hash
routine. Its effective address lies in guest backing that is not accessible
at the time of the fault. This does not yet identify why the input pointer or
length became invalid. It is not evidence that another guessed function alias
or a shader change would repair it.

`tools/symbolize_switch_crash.py` now verifies the ELF Build ID before resolving
PC/LR and the crashed thread's return offsets. Other threads' PCs are excluded.
The first hardware report therefore establishes entry into engine startup,
but still no game frame, menu or gameplay.

hw03 adds `switch/engine_crash.c`: a libnx native exception handler with its own
64 KiB exception stack and a preopened raw-FS crash sink. This path uses no
stdio, malloc or provider mutex. It writes native registers, image-relative PC
and LR, translated x86 registers, the guest stack, recent indirect targets and
kernel memory permissions around relevant addresses. Stack probes first check
kernel-readable bounds; the minimum register record is flushed before probing
stack contents. It cannot resume execution or fix the bad input by itself.
The handler compiles with general registers only; packaging checks that the
strong handler and full exception stack survive linking.

Return both `fnv-pc-engine-hw03.log` and `fnv-pc-engine-hw03-crash.log`. The crash
file initially states that no native fault has been captured; that header alone
is not a crash report. The diagnostic stops before guest execution if the sink
cannot be prepared. This capture path still needs its first hardware test.

## Hardware observation and hw02 correction

The first console log verifies the supported original EXE and both ROMFS
identities, successful Horizon VM permissions, a 1280x720 native surface and
NVK initialization on the Tegra X1. Preparation of all 54 shader programs
succeeded. Guest execution then stopped at an unresolved indirect target in
the captured CRT, before any engine frame was demonstrated.

The target belongs to a previously audited CRT floating-point classification
table. Five recovered entries lived in a supplemental desktop translation unit,
outside the numbered generation modules. The first engine package's body audit
did not include that auxiliary unit, so its lookup wrapper omitted the group.
The hw02 body comparison includes it explicitly: 88,830 common bodies remain
identical and the supplementary resolver now has seven entries, including the
two existing startup/gap bindings. The complete generated CPU is not rebuilt.

The CPU linker now accepts mandatory dispatch groups with pinned table/CFG
evidence and rejects a missing recovered entry even if someone removes the
same body from the candidate manifest. Supplemental units declare their peer
entries before compiling shared tails. A negative contract test exercises that
specific omission. These are existing audited translations, not success stubs
or guessed addresses. The 42 previously rejected data entries remain excluded.

The updated diagnostic identifies itself as hw02 and logs to
`sd:/switch/FNV-nx/fnv-pc-engine-hw02.log`. A follow-on diagnostic stop no longer
adds a second CPU fault after a real guest failure. The correction still needs
console execution; it does not establish that the engine reaches its menu.

## Original hw01 package

The first engine diagnostic NRO now builds with the complete translated PC CPU,
current native runtime and static Mesa/NVK. Unlike the older CRT stop probe and
synthetic renderer NROs, it is configured to execute the audited captured CRT
entry and continue into the engine. **Execution on Switch is still untested.**
It is not an independently established cold boot from the on-disk Steam EXE.

`switch/engine_probe.c` loads an identity-checked original PC EXE for its data,
overlays the verified captured text from private ROMFS, initializes the separate
808/760 MiB engine memory layout and binds the native import/TLS providers.
Unsupported imports have individual diagnostic traps, not successful stubs.
The external SDK and translated game sources remain separate private inputs.

Initialization includes a real Horizon NWindow/Vulkan surface, D3D9 COM factory,
the original shader catalog and 54 offline-prepared program pairs, plus the
prepared fixed-screen path. The actual catalog contains **1,809** entries;
the earlier 1,814 figure was incorrect. Packaging now reads and validates the
catalog rather than embedding an assumed count. It also checks original shader
array membership in the catalog before compilation.

The explicitly selected experimental caps profile admits this prepared engine
path; it does not establish general HW T&L, fixed-function or shader-model
conformance. Production generic caps are unchanged. Adapter properties retain
the actual Vulkan device/vendor. The diagnostic names the actual NVK driver
and overrides the engine's adapter preference and display-size queries to match
the actual prepared Switch surface, without editing an existing user INI.

## Input, audio and session lifecycle

The main guest executes on a separate native thread with a 4 MiB host stack.
Its 32 managed workers use the previously audited stack/TLS layout. The native
thread helper applies the application's allowed CPU mask instead of assuming
ownership of the console's fourth core.

The host loop polls HID, updates keyboard/mouse snapshots for both DirectInput
and USER32, polls the Switch XInput provider with printed A/B/X/Y labels, and
feeds app focus to the mixer. A separate event-driven audout worker pulls the
DirectSound mixer. MMIO, Vorbis/MP3 and the Bink sound/controller providers are
bound before guest startup. Bink resolves read-only `Data/Video` files below
the port root; the desktop diagnostic's fixed PC asset path is not reused.
Audio is deliberately set to volume zero for this diagnostic.

Hold **L + R + Minus** for about one second to request a diagnostic stop. Plus
remains a game button. Guest execution unwinds at existing CPU boundaries; if
the main thread does not unwind within five seconds, the host exits the process.
Provider, VM, window and GPU backing are retained until OS termination, so no
live guest can use freed state. This is not a graceful game/save shutdown.
Do not use valuable saves in this first experiment.

Progress and CPU faults are logged to
`sdmc:/switch/FNV-nx/fnv-pc-engine-hw01.log`. The host reports elapsed time,
draws/submissions/presents, guest workers, VM commitment and GPU allocation.
Those counters do not yet constitute a performance benchmark. A display-mode
change stops the diagnostic and requires restarting it.

## Packaging and test

`tools/build_switch_engine_probe.py` requires the accepted full platform link,
CPU/runtime proofs, original EXE/capture identities, independently audited
captured entry, prepared bundle and toolchain. It compiles the bootstrap with
the same extended-x87 ABI, links the final ELF, rejects unresolved strong or
dynamic library dependencies and packages the private NRO/ROMFS.

`tools/stage_switch_engine_probe.py` stages a fresh private SD directory. It
verifies the NRO hash and parses its actual embedded ROMFS to compare both
private file hashes against accepted inputs. It copies the user's default INI
and an own display preference template; it does not copy the EXE, bulk game
assets or Windows/Autorun DLLs. Synthetic tests cover bounded movie I/O and
traversal/seek overflow, catalog membership and corrupt ROMFS rejection.

Back up the previous NRO and UserData before copying the staged directory.
Place the supported **original PC** `FalloutNV.exe` and PC `Data` alongside
`sd:/switch/FNV-nx/fnv-nx.nro`; Xbox assets and a 4 GB/anticrash-patched EXE are
not accepted substitutes. Launch in application/title takeover mode, not the
Album. The diagnostic requires substantially more memory than applet mode.

The next evidence needed is the console log and visible behavior. A successful
compile/package does not demonstrate a menu, gameplay, stability or frame rate.
Generated game C, captured bytes, shader data, user INIs and NROs remain private;
only own implementation, tools, tests and documentation belong in the repo.
