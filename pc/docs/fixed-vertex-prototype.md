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

Passing private proof: `local/fixed-vertex-dispatch03/validation.json`. Four GPU
draws check4096 pixels: programmable identity; unlit transformed geometry;
programmable identity again without another constant upload; unlit identity
after Reset. Noncommuting world/view/projection matrices cover the independently
specified rectangle x12..19,y4..19. Constant UV .25/.75 yields expected color
within one UNORM unit. Source mutations cannot alter queued uniforms.
Wrong-transpose and reversed-order controls both fail with a pixel mismatch.
The return to the programmable shader proves the fixed matrix override did not
replace its register bank. Reset retains the prepared shader even after its
public reference has been released. Lighting defaults on after Reset, rejects
unlit draws, and rolls back if a state transaction subsequently fails.

The host registers the trusted original shader with
`fnv_vk_backend_fixed_vertex`; the provider validates its c0..3 column layout
and retains an internal pin. This does not change the visible shader binding.
COM buffered DrawPrimitive/DrawIndexedPrimitive select the bounded path only
with no programmable VS, FVF XYZ|TEX1 (0x102), identity texture0 matrix and an
explicit pixel shader. The backend additionally requires LIGHTING=FALSE and a
prepared linked shader pair. The matrix snapshot bypasses programmable VS
uniform packing without modifying its bank or binding. Unsupported variants
fail explicitly. UP draws do not use this path yet.

`local/vulkan-fixed-dispatch02` separately passes97 translated synthetic COM
callers and the previous46 real GPU draws,251/256 submissions and pixel-negative
controls. Its dedicated boundary fixture checks exact matrix columns, indexed
arguments, HRESULT propagation and rejection of unsupported FVF, texture
transform and missing PS. That fixture uses a callback spy and does not execute
a fixed GPU draw. The native pixel test and translated boundary test are two
separate proofs; a combined translated fixed-draw pixel fixture is still needed.
`local/uniform-fixed-override02` checks invalid override sizes, immutable copied
bytes, no implicit VS binding and required PS binding, plus800 concurrent uploads
and400 snapshots. Strict ARM64 A57/libnx compilation of COM and the backend
passes; this is not hardware execution.

Lighting, normals, vertex blending, XYZRHW, texture transform stages and fixed
fragment processing remain pending. Arbitrary float rounding, perspective
cases, Switch/NVK operation and game frames are not established. Hardware T&L
caps stay disabled; the game's startup rejection has not been bypassed.

Next validate the combined translated fixed GPU draw, then expand lighting
before claiming the processing required by the game.
