# Linked shader variants per original VS/PS pair

Exact shader reuse is insufficient when another partner requires different
input locations or a different UBO layout for the same original shader.
Preparation now records each pair's exact linked payloads. Guest shader objects
still identify the original D3D9 bytecode; a draw selects the immutable program
for the bound original VS and PS, then its linked modules, input map, sampler
mask and constant plans. It does not select whichever variant was registered
last or replace existing shader objects.

The catalog shares identical payloads and can retain multiple linked payloads
for one original stage. Its 4,096-entry limit counts these payload variants.
The default module is still created for a public shader object. When a pair
needs another module, the provider creates it lazily and caches it on the
catalog entry under the backend lock. It remains alive until device teardown,
including across Reset. Creation failure returns the real error and leaves
the cache slot empty for a later retry. Cached modules are destroyed after
GPU retirement, before destroying the device.

`fnv_uniform_state_prepare_program` snapshots live register banks using the
pair's plans without changing either bound plan or the banks. The descriptor
range uses the resulting snapshot size. Thus a shader object whose default
plan is 80 bytes can draw through a pair needing a 64-byte plan, and vice versa.
Existing fixed-function snapshots keep their separate contracts; fixed draws
with a different linked payload from their configured shader object reject.

`fnv_vk_prepare_program` still requires both original bytecodes in the loaded
catalog before any registration. It delegates to the backend's trusted
`prepare_variant` initialization path. An already registered original pair
cannot be replaced with different payloads, maps or masks; that conflict is
checked before adding stage variants. Failure while initializing a new pair
may retain earlier registrations, so destroy the failed initialization.
The older strict registration APIs and exact-reuse APIs remain available.

## Verification

The sharing test's optional `--relocate-vertex` mode first validates two existing
pixel fixtures and their unmodified original bytecodes/modules. It derives a
private linked VS variant by changing only two input Location decorations and
the uniform block member Offset. Locations move from 0/1 to 2/3; the block
starts 16 bytes later. Reflection produces a matching 80-byte plan instead of
64 bytes. The instruction words and original D3D9 bytecode remain unchanged.
The transformation is a bounded test fixture, not a production shader rewrite
or evidence that the engine selects this particular pairing.

Both pairs alternate on one device, natively and through 40 translated synthetic
x86 COM callers. Each route compares 6,144 pixels to Windows D3D9 using the
original shader programs and synthetic resources. Seven native pixel controls
include incorrect variant locations; five COM controls include the wrong PS.
Repeated initialization must retain only four payloads for three original
stages. A conflicting existing pair rejects rather than silently changing it.

A native dispatch control forces out-of-device-memory specifically when the
first draw creates a cached variant module. The draw returns OOM, publishes no
module, and a retry completes all six frames. Module creation/destruction counts
balance at teardown. The register-bank unit test checks unchanged outputs on
invalid plans and verifies that explicit snapshots install no hidden bindings;
its existing 800 threaded uploads and 400 snapshots still pass.

Full backend regression and ARM64/libnx compilation cover the changed paths.
These are desktop pixel/failure tests and cross-compilation, not new Switch
execution, full shader-model support, an engine frame or cold EXE boot. HWTL
and shader caps remain unchanged. All original/derived binaries, private headers
and generated callers stay outside the published sources.

```powershell
python pc/tests/test_prepared_program_sharing.py --first local/first-pair --second local/second-pair --relocate-vertex --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe --sdk ../sh2-nx/third_party/xboxrecomp --python-deps ../python-deps --output local/variant-new
```
