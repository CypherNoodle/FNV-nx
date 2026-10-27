# Combined Switch runtime build

`tools/build_switch_engine_runtime.py` cross-compiles the current native
providers together with the exact, previously validated Switch x87 core.
The inventory includes graphics and Horizon presentation, Windows compatibility,
files, heap and threads, input, PCM output, MP3, Vorbis and Bink adapters.
It contains no generated game code and produces no executable or NRO.

The checked build compiled 51 native units and combined them with 101 verified
x87 objects in one AArch64 relocatable object. Its 150 strong undefined symbols
were classified as 149 definitions present in the supplied platform/media
archives and one generated-engine dependency, `recomp_lookup`. No missing
symbols remained outside those categories.

## Inputs and checks

Run the tool with `--help` for its required paths. Supply the SDK, devkitPro,
Vulkan headers, compiler/linker/symbol tools, CPU ABI proof, and exact Switch
x87 core proof. Repeated `--include` arguments provide the external MP3,
Vorbis and configured FFmpeg headers. Repeated `--archive` arguments supply
the Vorbis/Ogg and FFmpeg archives. libnx, libc, libm, libsysbase and libgcc
are located from the selected devkitPro/toolchain.

The CPU proof selects compiler flags only: its old generated objects and old
native objects are not reused. Current native sources are recompiled. The
selected x87 objects, their source dependencies, and their compiler identity
must match their proof. Source/header/archive identities are frozen for the
build. Runtime and archive members must identify as AArch64.

Negative controls rejected an incompatible CPU ABI flag and an invalid x87
proof before creating a compilation output directory.

## What this establishes

The relocatable link detects duplicate native definitions and combines the
providers under the selected ABI. Archive symbol coverage checks that direct
external references have available definitions. It does not prove archive
selection order, transitive archive dependencies, a final executable link,
runtime initialization, or execution on Switch.

The remaining integration work includes the validated complete game generation,
observed engine entrypoints, an engine bootstrap and its guest memory layout,
provider initialization, shader preparation, and the final libnx/Mesa link.
The existing CRT diagnostic deliberately stops before engine entry and cannot
be relabeled as an engine hardware test. Desktop world rendering remains a
separate milestone; see `engine-presentation-checkpoint.md`.
