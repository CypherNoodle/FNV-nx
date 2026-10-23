# Shader resource creation before Vulkan pair preparation

`fnv_vk_backend_declare_shader` admits exact original D3D9 bytecode through a
trusted host catalog. Its caller must first validate the instructions offline;
the API checks only bounded size, stage/version and END. It copies the tokens,
is idempotent by stage plus bytes, and creates no SPIR-V or GPU module. Ordinary
guest calls cannot declare arbitrary shaders: the COM bytecode catalog still
requires an exact match before the backend is called.

CreateShader can then return a real owned resource with identity, references,
bindings and normal release semantics. It has no executable Vulkan module yet.
Draw still requires an explicitly prepared VS/PS pair, input mapping, uniforms
and resources. A missing pair returns INVALID before recording/submitting GPU
work. This changes resource lifetime, not rendering support or advertised caps.

Preparing a real program adds immutable payload entries without replacing the
original identity referenced by live resources. Pair lookup compares original
tokens and selects its actual payload and reflected uniform plan. Modules for
such variants are created lazily through the existing variant path. Raw identity
entries are excluded from program linking; they cannot become an executable
pair. Setup requires exclusive host access with no draws or GPU work in flight;
partial preparation failures still require backend destruction.

The configurable resource limit is now 2..2048, with existing defaults unchanged.
Catalog entries remain capped at 4096; raw identities and translated variants
both count. Admission does not raise these limits or the GPU allocation budget.

## Verification

The synthetic constant-bearing pair test adds a deferred mode: admit both
originals, poison admission input storage, create/bind resources, reject a link
without payloads, create/release 600 additional resource identities without any
GPU modules, and reject the first draw without any submission. It then prepares
the real pair while retaining the original handles and compares all six frames
(6,144 pixels) against Windows D3D9. Wrong-stage operations reject and repeated
admission does not grow the catalog. Teardown checks all modules are released.
The normal pair and five incorrect-render controls remain checked separately.

The existing shared backend regression passed 46 draws, 251 completed submissions,
resource/fault/reset/state tests. The final backend source also passes the pair
pixel test and strict actual ARM64/libnx compilation. None is a Switch execution
or actual game-frame claim.

## Native bytecode audit

`tools/audit_native_shader_catalog.py` and `native_shader_validator.c` create a
hidden Windows D3D9 NULLREF device, perform bounded native disassembly, then call
CreateVertexShader/CreatePixelShader and immediately release each object. Inputs
are an explicit unique SHA-256 manifest and matching private blobs. The helper
passes originals to Windows, never centroid-normalized replacements. Reports
freeze source/compiler/system-DLL/input identities and contain no bytecode.
`tests/test_native_shader_validator.py` checks two own HLSL inputs, eight negative
cases, repeated-open rejection and idempotent cleanup.

Native creation accepted all 642 bytecodes requested in the private startup
trace. This does not establish Vulkan translation or pixel correctness. The
separate offline translator audit accepted 641 after temporary centroid
normalization; PARTICLE.vso still rejects five relative constant accesses. Its
CTAB describes InstanceData at c18 with count two, while actual instructions also
address c19/c20 relative to a0.x. No array bounds or replacement metadata are
invented, and the particle shader is not yet renderable through this backend.

## Private engine experiment

Using the actual package 002, audited shader admission and a larger diagnostic
resource table removes the earlier null shader access. The captured-code replay
then reaches D3DXMatrixMultiplyTranspose. After that bridge is connected, the
next explicit stop is binkw32.dll!_BinkSetSoundSystem@8 (134 imports implemented,
146 trapped), with zero engine draws/submissions. Neither admission nor these
counts demonstrate a cold EXE boot, a game frame, or hardware performance.
Original packages, captured/generated game code, bytecode and SPIR-V stay private.
