# Directional material lighting with one texture

The new original `fixed_directional_textured.vs.hlsl` forwards FLOAT2 TEXCOORD0
alongside the existing lit COLOR0. It retains the same160-byte directional
constant layout, normal transform and material contract. The original
`fixed_directional_modulate.ps.hlsl` samples s0 and multiplies RGBA by the
interpolated lit color. Both alpha factors participate; this is not the default
D3D9 SELECTARG1 alpha operation.

Host preparation uses the existing shader registry and directional internal pin.
The linked pair declares POSITION/NORMAL/TEXCOORD0 semantic locations and pixel
sampler mask1. Buffered COM draws use SetFVF0x112, no programmable VS, an explicit
prepared PS, LIGHTINGtrue/COLORVERTEXfalse/SPECULARfalse and an identity texture0
matrix. The existing resource/version/sampler backend supplies the texture.
No new GPU backend, CPU translation system or Xbox code path was introduced.

This is an explicit single-texture RGBA modulation shader, not automatic
SetTextureStageState emulation. It does not select a PS when SetPixelShader(NULL)
is used, and it does not make all game shader interfaces compatible. The
nontextured directional pair remains separately available. Hardware T&L caps
remain disabled until the required contract is implemented and verified.

## Verification

`local/directional-texture-gpu02/validation.json` passes7168 pixels against real
Windows D3D9 HAL fixed lighting with software vertex processing. The oracle
explicitly enables COLOROP and ALPHAOP MODULATE with TEXTURE and DIFFUSE args.
It loads a2x2 A8R8G8B8 texture with distinct R/G/B/alpha at all four texels.
Point sampling tests texel centers across frames and interpolated UVs across
both triangles, covering four quadrants. All pixel channels agree within one
UNORM unit. Frames also exercise normal scaling, ambient/emission, a return to
programmable rendering without reuploading its constants and shader retention
through Reset. Texture bindings are explicitly restored after Reset.

The native fixture overwrites the texture after the draw is submitted but before
color readback; the draw retains its original texture version. Wrong-normal,
wrong-direction, wrong-UV and forcing texture alpha to255 all fail pixels.
Initial gpu01 passed center sampling; gpu02 additionally tests interpolated UVs.

`local/directional-texture-com03/validation.json` passes38 synthetic x86 callers
and5120 pixels through translated COM -> Vulkan. CreateTexture, LockRect,
UnlockRect and SetTexture travel through translated calls, as do matrices,
materials and lights. Texture updates after a submitted draw do not change its
readback. The fixture also unbinds the required texture: draw returns INVALIDCALL
and both draw/submission counters remain unchanged. Five negative controls
ignore matrices, overwrite programmable constants, ignore material, alter UVs
and replace texture alpha. Each is detected at its expected frame.
Earlier COM01 covered four center-sampled frames; COM02 adds interpolated UVs;
COM03 adds the missing-texture check and accurate test summary count.

The color-only path remains verified by `local/directional-color-regression01`
(7168 pixels, two controls) and `local/directional-color-com-regression01`
(34 callers,4096 pixels, three controls). The runtime source is unchanged from
the prior ARM64/libnx build; this change adds shaders and extends desktop GPU
fixtures. No new Switch shader execution or timing measurement is claimed.

The UV tests use point sampling, orthographic positions and this specific pixel
grid. Perspective interpolation, linear/mipmap filtering, texture matrices,
multiple stages, fog and automatic fragment-state selection need further work.
No engine startup, game frame or new NRO is established by these fixtures.

## Reproduce

Use fresh private output paths:

```powershell
python pc/tests/test_directional_vertex_gpu.py --textured --output local/directional-texture-new --linker local/vulkan-uniform-link01/link_d3d9_spirv.exe --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe
python pc/tests/test_directional_vertex_com.py --sdk ../sh2-nx/third_party/xboxrecomp --python-deps ../python-deps --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --fixture local/directional-texture-new --output local/directional-texture-com-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

Omit --textured for the color-only fixture. The COM driver reads and verifies
the prepared fixture hashes and chooses matching shaders, caller slots and
negative controls. Only original sources are published; compiled shaders,
synthetic generated callers and reports remain local.

Next implement bounded automatic fragment-state selection, starting with
single-stage texture/color combinations, before broadening point/multiple lights.

A bounded automatic NULL-PS path is now implemented; see [fixed fragment selection](fixed-fragment-selection.md). The explicit pair remains available.
