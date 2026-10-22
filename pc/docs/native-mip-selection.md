# D3D9 maximum mip level in the Vulkan provider

The provider previously rejected every nonzero `D3DSAMP_MAXMIPLEVEL`. It now
accepts indices within the sampled texture's mip count. With point or linear
mip filtering, the value becomes Vulkan `minLod`; the coarsest available level
remains `maxLod`. The original sampler state is retained for GetSamplerState
and subsequent draws. No image data is discarded or vertex UVs rewritten.

Windows D3D9 pixel comparisons establish an important interaction: when
`MIPFILTER=NONE`, MAXMIPLEVEL is ignored and sampling stays at base level zero.
The initial implementation clamped NONE too, and the Windows reference rejected
that result. Both Vulkan LOD bounds now remain zero in this case. This behavior
is verified separately from the documented [MAXMIPLEVEL definition](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dsamplerstatetype).

Validation is deliberately bounded: indices outside the actual mip count fail
explicitly at draw time. Setter validation uses the provider's maximum possible
mip count (32), since the state may be set before binding a texture. Anisotropic
filtering, sRGB sampling, arbitrary border colors and out-of-range mip-index
compatibility remain unsupported. Shader capabilities are unchanged.

## Pixel and translated-call proof

The catalog fixture's `--mip-clamp` mode uploads an 8x8 texture with four mip
levels containing distinct BGRA colors and alpha. Six frames test NONE, POINT
and LINEAR mip filters, multiple nonzero clamps, magnification, minification
and a fractional LOD. A smaller viewport is used in the last frame. Constant
UVs isolate the clamp; changing UV gradients exposes filter selection.

Own shaders and an explicitly selected original VS/PS pair each pass 6,144
pixel comparisons against Windows D3D9 with one-level channel tolerance. The
original pixel shader is a different, tint-multiply program from the earlier
channel-mask test. Saturation in that older shader concealed the wrong-clamp
control, so it was unsuitable for proving this feature. The new test detects
both removal of the mip clamp and substitution of MIPFILTER=NONE.

The original pair also passes 6,144 pixels through 42 translated synthetic x86
COM callers. Texture creation, mip-level LockRect/UnlockRect, sampler Set/Get,
constant updates and draws traverse COM. Sampler values are reset after each
draw, before readback; the submitted draw retains its sampler snapshot. These
are synthetic resources and callers, not an engine frame or observed game pair.

The pure state test passes 48 depth cases, 783 sampler cases and 25 rejections.
The complete backend regression still passes 103 callers, 46 draws, 251
headless/256 WSI submissions and its injected-failure and wrong-pixel controls.
The updated state mapper also compiles with strict ARM64/libnx flags. No
Switch execution, performance improvement or engine startup is established.

```powershell
python pc/tests/test_catalog_pair_gpu.py --mip-clamp --output local/mips-new --linker local/shader-describe-build01/link_d3d9_spirv.exe --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

For original shaders add `--constants` plus the existing explicit package and
shader identity arguments. Then feed the validated fixture to
`test_catalog_pair_com.py`; it selects the mip sampler callers automatically.
All extracted original bytecode, SPIR-V and generated caller output stay private.
