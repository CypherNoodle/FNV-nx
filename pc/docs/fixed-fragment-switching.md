# Same-device fixed fragment switching and vertex-buffer FVF

The selection path is now checked with all three prepared PS variants registered
on one Vulkan device. The test uses50 synthetic x86 callers -> native CPU/COM
bridge -> Vulkan. Its input PE, shaders, texture and geometry are original
fixtures, not generated game code or game assets.

## Common vertex interface

`test_fixed_fragment_switch.py` compiles the original textured directional VS
and all three original PS sources, then links the VS independently with each PS.
One common VS module is used at runtime. Before creating it, the test compares
uniform plans and resolves SPIR-V pointer/vector/float types, checking each PS
input location against the common VS output. Linked pairs register the same
POSITION/NORMAL/TEXCOORD0 map with sampler mask0 for diffuse and1 for texture.
No numerical SPIR-V type-id equivalence is assumed between modules.

## Nine frames on one device

`local/fixed-fragment-switch04/validation.json` passes9216 pixels compared with
Windows D3D9 HAL fixed lighting/fragment processing, within one UNORM unit.
The sequence selects modes0,1,2,1,0,2,1,0,2. Frames3/7 use explicit programmable
VS/PS while an unsupported texture-stage operation is active; those draws keep
working and use the explicit shader. Other frames bind VS/PS NULL and let stage
state select the internal PS. GetPixelShader returns the application binding
throughout, and constants uploaded once per Reset epoch remain correct.

Frame4 records a diffuse selection while modulation remains live, then applies
the block and checks diffuse pixels. Frame6 performs a real translated COM Reset,
restores required bindings/states and resumes switching on the same device.
All prepared VS/PS public references were released before COM shader creation;
internal pins retain the prepared resources. Managed VB/texture and explicit
shader references are retained through Reset. Cleanup requires all GPU handles
and allocations to be released.

Negative controls select the wrong texture alpha mode, deliberately register the
modulation shader for the texture-alpha slot, and skip applying the recorded
block. Each produces a pixel mismatch at the expected frame. These are deliberate
test faults, not alternate supported shader contracts.

An initial test-driver attempt passed a directory where the shared runner
hashes file arguments; it failed before compilation. The corrected driver passes
the directory as a runtime argument and freezes each binary/SPIR-V input file.
Switch02 established48 translated callers and9216 pixels; switch03 adds VB FVF
metadata callers; switch04 adds native valid/invalid buffer creation checks.

## Nonzero vertex-buffer FVF

The Vulkan buffer provider formerly rejected every nonzero VB FVF. It now accepts
formats supported by `fnv_d3d9_fvf_layout`, preserving FVF metadata without
selecting an active vertex declaration. Index-buffer format validation is
unchanged. Reserved/unsupported VB FVF fails before allocation and leaves the
output handle unchanged. This is still the existing bounded unblended subset,
not support for every Windows vertex format.

The Windows oracle `local/vertex-buffer-fvf01` checks six formats (0,0x102,0x112,
0x152,0x104,0x4002),257-byte buffers not divisible by a vertex stride, lock/unlock,
GetDesc metadata and unchanged active FVF. The switch04 fixture creates those
same formats natively and rejects reserved-bit FVF1 without allocation.
It creates the drawn managed VB with FVF0x112 through translated COM, verifies
GetDesc.FVF and size, and confirms the active FVF remains0 until SetFVF.
This also closes the previously documented nonzero CreateVertexBuffer FVF gap
for that subset. No shader or draw behavior is inferred from buffer metadata.

The backend passes strict ARM64 Cortex-A57/libnx compilation. Regression
`local/vertex-buffer-fvf-regression01` retains the previous103 translated callers,
46 actual GPU draws,251/256 submissions and fault/pixel-negative checks.

## Reproduce

```powershell
python pc/tests/test_fixed_fragment_switch.py --sdk ../sh2-nx/third_party/xboxrecomp --python-deps ../python-deps --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --linker local/vulkan-uniform-link01/link_d3d9_spirv.exe --output local/fixed-switch-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
python pc/tests/test_vertex_buffer_fvf.py --output local/vb-fvf-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

The shared Windows oracle can now choose the fragment mode at runtime; prior
single-mode fixtures retain their compile-time behavior. No Hardware T&L caps,
engine startup, new game replay, Switch performance or new NRO is claimed.
Next broaden the required lighting/state contract and connect it to the actual
startup configuration before deciding whether to enable Hardware T&L.
