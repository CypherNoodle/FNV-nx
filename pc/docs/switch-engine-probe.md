# Private captured-PC engine NRO: hw01

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
