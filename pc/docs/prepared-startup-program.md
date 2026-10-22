# Prepared Vulkan program installed during startup

The captured-startup host can now install one exact offline-linked VS/PS pair
before creating its D3D9 COM adapter. This connects the previously separate
original SDP catalog and prepared Vulkan shader registrations. It does not
change caps, bypass startup checks or cause the engine to issue a draw.

`fnv_vk_prepare_program` checks both bytecodes against the loaded catalog
before registering either stage, then registers SPIR-V and reflected uniform
plans and links the explicit input-location map and sampler mask. Catalog
membership uses exact bytes, not names or guessed shader IDs. Successful
registration copies all consumed data. Initialization is exclusive and must
use a fresh backend. Membership rejection has no side effects. Later failures
may leave partial registrations, so the caller must destroy that backend
instead of retrying. The helper does not create public shader objects.

Subsequent preparations may reuse exactly identical stages and pairs; see
[prepared program sharing](prepared-program-sharing.md) for conflict checks,
same-device pixel tests and remaining per-pair variant limitations.

The shader pixel test now uses this same helper, checks that a missing pixel
shader in the catalog installs neither stage, and then verifies creation,
binding and rendering. Both the own-input and explicitly selected original
constant-bearing pair pass 6,144 Windows D3D9 pixel comparisons and five
negative controls. These are direct native backend draws with synthetic
resources, not translated game draw calls.

## Private build header

`emit_prepared_program.py` consumes an existing `translate_d3d9_pair.py` fixture
and emits a private C header containing original bytecode, SPIR-V, plans and
an explicit `fnv_startup_program` descriptor. Its output must never be committed
or published. It checks original/SPIR-V identities, stages, input locations,
UBO reflection and supported descriptor sets/sampler bindings. It does not
infer vertex element formats or prove semantic shader equivalence. The input
translation report and binaries are trusted build inputs, not guest inputs.

Compile the captured-startup host with `FNV_STARTUP_VULKAN`, `FNV_STARTUP_SDP`
and `FNV_STARTUP_PROGRAM_HEADER` set to the quoted absolute private header path.
Link both `native/d3d9_sdp.c` and `native/d3d9_vk_prepared.c`. Set the existing
`FNV_STARTUP_SDP_PATH` to the user's original package containing both bytecodes.
There are no additional runtime shader folders or DLL requirements.

The private refreshed startup diagnostic successfully loads 983 original
records, registers two SPIR-V programs and links their pair. It then stops at
the existing Hardware T&L rejection, still with zero draws and submissions.
It uses captured translated startup code plus an exact x87 result replay;
this is not a cold EXE boot or Switch execution. Failed guest workers and
the existing process-cleanup BUSY result remain unresolved in that diagnostic.

The own-input header test checks six metadata/identity mismatch cases and
refuses to overwrite a header. The helper and generated original-data header
also compile with strict ARM64/libnx flags. The next integration work is
observing actual engine-selected shader pairs/declarations and preparing their
required states/resources; one installed pair is not full shader-model coverage.

```powershell
python pc/tests/test_prepared_program.py --output local/prepared-test-new --linker local/shader-describe-build01/link_d3d9_spirv.exe
python pc/tools/emit_prepared_program.py --fixture local/private-pair-fixture --input 0,0,0 --input 5,0,1 --sampler-mask 1 --output local/private-startup/startup_program.h
```

Input mappings in the second command are examples, not automatic semantic
inference. Use the locations reflected from the particular linked vertex module.

## Translated COM creation and draws

`test_catalog_pair_com.py` now takes the validated constant-bearing private
pixel fixture through forty SDK-translated synthetic x86 COM callers. Both an
own shader pair and the explicitly selected original pair pass 6,144 pixels
against the same Windows D3D9 reference. This verifies a longer path than the
direct backend test: original code -> CreateVertex/PixelShader -> COM shader
binding -> register-bank setters -> resource locks/uploads -> DrawPrimitive
-> native Vulkan provider. It does not execute original game call sites.

The test creates a vertex declaration, managed VB and texture through COM,
fills their guest lock buffers, and changes matrices, tint, texels and viewport
over six frames. Guest shader-source buffers are poisoned after creation;
GetFunction still returns both exact originals. GetVertexShader/GetPixelShader
return the expected guest objects and their acquired references are released.
Constant source buffers and post-draw COM banks are also overwritten, proving
that copied state and submitted GPU snapshots survive those changes.

Four controls detect incorrect UV, texture channels, VS constants and PS
constants. The offline pair is installed by the same startup helper, while
all shader/resource/state/draw operations under test traverse translated COM.
Only provider initialization, explicit diagnostic transfer waits and readback
use host-native calls. The shared reference oracle is also used by the direct
backend test, whose pixel and five-control regression still passes after its
extraction into a common test header.

```powershell
python pc/tests/test_catalog_pair_com.py --sdk ../sh2-nx/third_party/xboxrecomp --python-deps ../python-deps --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --fixture local/private-validated-constant-pair --output local/catalog-com-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

The fixture directory must have a passing `test_catalog_pair_gpu.py --constants`
report with matching original and SPIR-V hashes. Original bytecode, translated
modules and generated caller output remain private. This adds no Switch test,
capability advertisement or evidence of an actual game frame.
