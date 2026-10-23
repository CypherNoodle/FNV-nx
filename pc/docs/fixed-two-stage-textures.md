# Bounded two-stage fixed texture modulation

The automatic NULL-VS/NULL-PS directional path can now select an original
two-sampler fragment shader. Stage 0 multiplies texture 0 by the lit diffuse
RGBA; stage 1 multiplies texture 1 by that result. Each stage clamps its result.
Both stages must select COLOROP/ALPHAOP MODULATE, ARG1 TEXTURE and ARG2 CURRENT.
Both explicitly use TEXCOORD0. Other active stage-1 tuples return NOTAVAILABLE.

The fourth internal PS registration, mode 3, uses sampler mask 3 and the existing
160-byte directional VS constant layout. It shares the existing XYZ/NORMAL/UV
FVF 0x112 vertex path and material/one-directional-light restrictions. Public
VS/PS bindings remain NULL during automatic draws. Internal pins retain the
four PS variants through Reset; submitted draws retain texture versions.

Get/SetTextureStageState now also supports TEXCOORDINDEX (field 11) for stages
0 and 1. Values are stored verbatim and participate in recording, Capture,
Apply, transaction rollback and validation-only transactions. Defaults and Reset
restore indices 0 and 1. This two-stage draw requires both indices to be zero;
it rejects the default stage-1 index rather than silently reading the wrong UV.
Nondefault coordinate selection is also rejected for explicit-PS fixed draws.
Fully programmable draws retain their own shader coordinate semantics.

Both texture matrices must be identity when both fixed stages are active.
Unsupported texture-1 matrices return NOTAVAILABLE before a draw is submitted.
An early negative test found that this new rejection returned success without
drawing after a successful state query; the return path was corrected.
An unbound required texture returns INVALIDCALL. These failures do not increase
draw or submission counters.

## Verification

`test_fixed_two_stage.py` compiles original synthetic shaders, links four pairs,
checks the common VS varying types/locations and executes 50 translated x86
COM callers against the native Vulkan provider. A separate Windows D3D9 HAL
fixed-function oracle renders matching states, geometry, material and textures.
Twelve frames switch among the prior three modes and the new two-stage mode on
one device, including Reset and returns to programmable rendering.

The fixture checks 12,288 pixels within one UNORM unit. Two distinct 2x2 textures
have varying RGB and alpha; interpolated UVs cover their quadrants. Five pixel
negative controls detect wrong mode selection, alpha, stateblock application,
second texture and second-texture alpha. Additional assertions verify coordinate
state recording/rollback, unsupported coordinates and matrix rejection, missing
texture rejection and a second-texture update after submission retaining the
draw's previous version. The final frame checks that retained version against
the Windows result. Resources, shaders and internal pins are released at close.

The previous single-stage switching fixture passes 9,216 pixels and its three
negative controls. The broader Vulkan regression retains its 46 draws, resource
and fault checks, with 251 headless and 256 WSI submissions. COM and the Vulkan
backend compile for Cortex-A57 ARM64/libnx with warnings treated as errors.

Reproduce using a fresh private output directory:

```powershell
python pc/tests/test_fixed_two_stage.py --sdk ../sh2-nx/third_party/xboxrecomp --python-deps ../python-deps --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --linker local/shader-describe-build01/link_d3d9_spirv.exe --output local/fixed-two-stage-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

The SDK folder is the unchanged translator dependency; this uses synthetic PC
callers and does not resume Xbox work. Compiled shaders, generated callers and
reports stay private. No Switch execution, engine frame or FPS result is claimed.

## Remaining capability boundary

This is an explicit two-stage subset, not general D3D9 stage emulation. UV1,
coordinate generation, texture matrices, additional operations/argument modifiers,
other fixed vertex formats and UP fixed draws remain outside this path. The
startup program bundle does not yet install this internal fixed shader family.
HWTL, shader versions and the normal stage caps are unchanged. Before raising
the game profile's limits, connect the verified family and cover the stage and
coordinate combinations actually used by the engine.
