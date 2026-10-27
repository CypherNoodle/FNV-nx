# Complete PC CPU/runtime platform link

The complete translated PC generation and native providers now pass a final
Horizon AArch64 ELF link with the existing static Mesa/NVK, Vorbis/Ogg and
FFmpeg archives. This extends the earlier relocatable-object coverage check.
It does **not** demonstrate engine startup or create a game NRO.

`tools/link_switch_engine_platform.py` consumes the accepted CPU partial-link,
native-runtime and static renderer proofs. It verifies their source/object and
archive identities, then compiles the own link entry and engine memory helpers.
Every public definition in the CPU/runtime object is a linker root. The final
symbol inventory must retain every root, not merely a few representative ones.
This prevents an empty `main` and section collection from hiding missing game
or provider references.

Normal section collection still applies to external archives. An initial check
that disabled it globally pulled in unreachable Unix/Rust standard-library
functions from Mesa and failed. No replacements for those unused functions
were added. Explicit CPU roots preserve the intended coverage while allowing
the platform's normal archive collection.

The accepted local run retained 89,675 CPU/runtime definitions. Its ELF is
177,205,408 bytes including symbols/debug information, with a loadable virtual
span of 173,867,224 bytes. It has three loadable segments, no writable executable
segment, no unresolved strong symbol and no dynamic library dependency. These
figures exclude the guest arena, GPU allocations, thread stacks, prepared
shader assets and all other runtime allocation. They are not a RAM-fit or
performance measurement. Hardware execution remains untested.

## Engine memory configuration

`switch/engine_layout.c` describes a separate engine experiment: an 808 MiB
guest arena, 48 MiB offset to the 760 MiB VM pool, and 32 workers backed by a
dedicated arena before that pool. Image, PEB, command line, main TEB/TLS/stack,
process heap, worker arena and VM pool are checked for overlap, alignment,
32-bit address overflow and worker-stride capacity.

`switch/engine_process_plan.c` turns this layout into process, VM and main-thread
configuration. It performs no allocation, permission change or guest execution.
The caller must provide real aligned backing, load and verify the PC image,
initialize the dedicated regions, create the VM, assign `process.memory`, bind
the providers/IAT/TLS, and initialize CPU state before attaching the thread.
The root is `sdmc:/switch/FNV-nx`, and the executable is `FalloutNV.exe`.

The older bounded CRT probe is unchanged. Its memory map and stopping point
cannot be relabeled as engine evidence. The older desktop `extended808` profile
also uses a different worker/VM split and is not this engine layout.

Own tests exercise invalid overlap, oversized workers, overflow and unchanged
failure outputs; process-plan checks run with both CPU state ABIs. The new
helpers also compile and link with the real extended-x87 AArch64 runtime.
Six negative platform-proof controls fail before creating output or compiling.

## Remaining engine bootstrap

The next stage must initialize the native window, WSI renderer, complete import
providers, prepared private shader programs, live input and audio, and execute
the verified captured-code entry. The link-check `main` deliberately does none
of those things and must not be packaged as a playable or engine-test NRO.

The desktop room-image experiment used an explicit private caps override and
an adapter-driver string override. Production `GetDeviceCaps` remains conservative:
prepared shader programs alone do not establish general D3D9 shader-model or
fixed-function conformance. A first engine bootstrap must label any such
experimental admission profile explicitly and use the actual Switch adapter
identity, rather than silently copying desktop overrides.

Cold startup from the on-disk EXE is still unproven. Private captures, generated
game code, shader assets, archives and linked binaries stay outside publication;
only the own helpers, synthetic tests and documentation are public.
