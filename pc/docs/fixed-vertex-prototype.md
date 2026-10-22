# Unlit vertex transformation on Vulkan

`shaders/fixed_unlit.vs.hlsl` implements a bounded vertex prototype: XYZ position
with implicit w=1, world/view/projection transformation and unchanged TEXCOORD0.
`native/d3d9_fixed_vertex.h` composes row-vector world * view * projection and
packs its four columns for the shader's dot products. It uses staged local
outputs and does not alter the programmable constant bank. The owner must
serialize a matrix snapshot with COM changes. FP contraction must stay disabled.

The shader is original source compiled offline to D3D9 SM3, translated/linked to
SPIR-V using the existing tool, and registered with the real Vulkan provider.
Its reflected UBO plan uses64 bytes, c0..c3. The diagnostic fragment shader
encodes UV as color; it does not sample textures. The pair uses FLOAT3 POSITION
and FLOAT2 TEXCOORD0 with the linked semantic/location map.

Run from the project root with a fresh private output folder:

```powershell
python pc/tests/test_fixed_vertex_gpu.py --output local/fixed-vertex-new --linker local/vulkan-uniform-link01/link_d3d9_spirv.exe --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

The tool requires Windows D3DCompile and the prepared offline linker. It stores
own compiled fixture shaders only in the private output. Input/output hashes,
compiler invocation, reflection and logs are recorded there. The test compiles
with strict warnings and uses the Vulkan loader from Windows System32.

Passing private proof: `local/fixed-vertex-gpu04/validation.json`. Two GPU draws
check2048 pixels: identity covers the32x32 target; noncommuting world/view/
projection matrices cover the independently specified pixel rectangle x12..19,
y4..19. Constant UV .25/.75 yields its expected color within one UNORM unit.
Changing the original matrix bank and upload arrays after draw submission does
not alter readback. Wrong-transpose and reversed-order controls each fail with
a pixel mismatch. Earlier attempts failed strict indentation compilation, then
draw validation because depth was enabled without a depth target; the passing
fixture explicitly disables depth. These are not passing results for those
earlier attempts.

This proof uses explicit shader bindings and native callbacks, not translated
COM draw callers or automatic SetVertexShader(NULL) selection. The COM matrix
bank is not yet wired to this shader's constants. Lighting, normals, vertex
blending, XYZRHW, texture transform stages and fixed fragment processing remain
pending. Arbitrary float rounding, perspective cases, Switch/NVK operation and
game frames are not established. No Hardware T&L caps are enabled by this proof.

Next connect a strict supported variant to the COM draw path without overwriting
the application's programmable constants/bindings, then validate switching and
stateblocks with translated callers. Expand lighting before claiming the game's
required processing contract.
