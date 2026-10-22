# Bounded directional material lighting on Vulkan

This extends the unlit prototype with a real lit vertex shader and copied
per-draw constants. It is a PC x86 COM renderer change, with no Xbox route or
Xenos/eDRAM emulation involved.

## Supported contract

Buffered DrawPrimitive/DrawIndexedPrimitive select the directional variant with
SetVertexShader(NULL), FVF0x112 (XYZ|NORMAL|TEX1), a bound pixel shader, identity
texture0 matrix and a host-prepared linked shader pair. The shader consumes XYZ
and NORMAL and outputs COLOR0; it does not forward TEXCOORD0 yet. The prepared
pixel shader must consume that interface. It is not a complete fixed fragment
pipeline or general compatibility with the game's pixel shaders.

Require LIGHTING=TRUE, COLORVERTEX=FALSE, SPECULARENABLE=FALSE. Zero or one active
DIRECTIONAL light is supported; multiple active or point/spot lights return
NOTAVAILABLE. Zero/nonfinite light directions and singular/non-affine normal
matrices fail without modifying output. Shader preparation does not change
visible programmable bindings or register banks. UP draws are not connected.

The original HLSL shader computes position from world*view*projection, normals
from the inverse-transpose of world*view, optional normalization, and

```
saturate(material.emissive
       + material.ambient * (globalAmbient + light.ambient)
       + material.diffuse * light.diffuse * max(dot(normal,-direction),0))
```

Diffuse material alpha is carried separately and clamped with output color.
Diffuse/material/ambient preparation is finite-checked. Direction is transformed
by view's linear part and normalized. Normalization follows NORMALIZENORMALS.
The ten contiguous float4 registers are WVP columns c0..3, normal columns c4..6,
view light direction and normalize flag c7, diffuse product/alpha c8, and
ambient/emissive c9. The immutable160-byte override is independent of the
application's programmable constants.

The provider stores ambient/color-vertex/specular/normalization states with
numeric transaction rollback and Reset defaults. Host registration retains a
separate directional shader pin through healthy Reset. Teardown releases it.
The lost-device result takes priority over a missing prepared shader.

## Actual pixel evidence

`local/directional-vertex-gpu04/validation.json`:7168 pixels, within one UNORM
unit of real Windows D3D9 HAL using software vertex processing and the fixed
pipeline. Seven draws cover frontal and rear-facing light, nonuniform world
scale, material emission, global ambient with all lights disabled, return to
programmable rendering without reupload, shader retention after Reset without
a public vertex shader reference, and normals with normalization disabled.
Wrong-normal and reversed-direction controls produce pixel mismatches. CPU
negative checks reject a second active light, point-light type and zero direction
without changing output. Arbitrary view/projection and rounding equivalence are
not established by these specific frames.

`local/directional-vertex-com02/validation.json`:34 synthetic x86 functions
translated by the existing SDK reach COM and the real Vulkan provider. Four draws
compare4096 pixels with the same Windows oracle: programmable, fixed lit,
programmable again, fixed ambient. Material, matrix, light and enable updates
travel through translated COM calls. Ignoring matrices, ignoring material and
deliberately overwriting programmable constants are all detected by pixels.
Guest scratch data is cleared after submission before readback.

The Windows oracle is source code in tests/directional_vertex_oracle.h and the
shader sources are original. The fixture does not load game assets or execute
the engine. The output PE is synthetic. Compiled fixture shaders and generated
callers stay in private output directories.

Regression proofs: `local/vulkan-directional-regression02` retains103 translated
callers,46 GPU draws,251/256 submissions and pixel-negative controls.
`local/unlit-directional-regression01` retains4096 unlit pixels and both negative
controls. Final COM and backend objects compile with strict warnings for ARM64
Cortex-A57/libnx. These are desktop GPU and cross-compilation results, not Switch
execution or performance measurements.

## Reproduce

Use fresh output directories from the project root:

```powershell
python pc/tests/test_directional_vertex_gpu.py --output local/directional-new --linker local/vulkan-uniform-link01/link_d3d9_spirv.exe --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe
python pc/tests/test_directional_vertex_com.py --sdk ../sh2-nx/third_party/xboxrecomp --python-deps ../python-deps --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --fixture local/directional-new --output local/directional-com-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

Next add texture coordinates and a supported texture/color fragment combination,
then point/multiple lights and required color sources. Hardware T&L caps remain
disabled. No new game startup replay, engine frame, NRO or hardware trial is
claimed by this change.

The optional textured pair now forwards TEXCOORD0 and modulates RGBA; see [directional texture rendering](directional-texture-rendering.md). The original color-only pair and its limits remain unchanged.
