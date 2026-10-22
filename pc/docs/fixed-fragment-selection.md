# Bounded automatic fixed fragment selection

With no programmable VS or PS, the prepared directional path now chooses an
internal pixel shader from texture-stage state. This is the first bounded
SetPixelShader(NULL) path; the prior explicit shader path remains unchanged.
The directional XYZ/NORMAL vertex, material, lighting and FVF0x112 requirements
still apply. This does not extend fixed selection to unlit or UP draws.

## Prepared variants

The host registers trusted zero-uniform shaders using
`fnv_vk_backend_fixed_pixel(device,mode,id)` and links each matching pair with
the existing semantic/location map and sampler mask. Registration checks PS
stage and empty uniform plan; the host verifies shader semantics offline.

| Mode | Stage0 color operation | Stage0 alpha operation | Result |
| --- | --- | --- | --- |
|0 | SELECTARG1 DIFFUSE | SELECTARG1 DIFFUSE | interpolated lit RGBA |
|1 | MODULATE TEXTURE,CURRENT | MODULATE TEXTURE,CURRENT | texture*lit RGBA |
|2 | MODULATE TEXTURE,CURRENT | SELECTARG1 TEXTURE | texture*lit RGB,texture alpha |

Stage1 COLOROP must be DISABLE. CURRENT at stage0 is the incoming diffuse color.
Argument ordering is bounded exactly as shown, with no complement/alpha-replicate
modifiers. Unsupported state tuples, active extra stages or missing prepared
variants return NOTAVAILABLE. A required but unbound texture fails INVALIDCALL
before submitting a draw. No fallback shader silently approximates a tuple.
The new original texture-alpha PS is distinct from the RGBA modulation PS.

COM slots66/67 implement Get/SetTextureStageState through the existing guarded
numeric state machinery. Stages0/1 store six DWORD fields: COLOROP/ARG1/ARG2 and
ALPHAOP/ARG1/ARG2. Other stage indices or field names are rejected. State values
are retained verbatim, and a fixed draw rejects unsupported combinations.
Programmable PS draws do not select these internal variants.
Initial and successful Reset defaults match the tested Windows defaults:
stage0 {4,2,1,2,2,1},stage1 {1,2,1,1,2,1}. Numeric transaction validation/rollback,
recording, Capture and Apply include the stage arrays.

## Binding and lifetime

Three internal pins retain prepared PS resources through healthy Reset, even if
their public references and visible bindings are gone. Device close releases
those pins. In-flight draws retain their own resource pins until completion.
GetPixelShader continues returning the application binding, including NULL;
there is no temporary bind/unbind pair during fixed draws.

`fnv_uniform_state_prepare_fixed` copies the fixed VS payload and leaves PS
uniforms empty. Neither programmable binding nor either register bank changes.
The ordinary and explicit-PS snapshot APIs retain their previous requirements.
Only the trusted zero-uniform internal PS uses the empty-PS snapshot path.

## Evidence

Each native GPU fixture compares7168 pixels with Windows D3D9 HAL software
vertex processing and real fixed fragment states, within one UNORM unit:

- `local/fixed-fragment-gpu02`: RGBA modulation, four pixel-negative controls.
- `local/fixed-alpha-gpu03`: texture alpha, four pixel-negative controls.
- `local/fixed-color-gpu02`: diffuse color, two pixel-negative controls.

Final native fixtures release both prepared VS/PS public references before
Reset and render successfully afterward. They check stage transaction rollback;
the Windows oracle checks the initial twelve stage values. Each fixture prepares
one of the three PS modes in a fresh device; transitions between different
internal modes within one device have not yet been pixel-tested.

Translated proofs:
`local/fixed-fragment-com02` and `local/fixed-alpha-com02` each pass45 synthetic
x86 callers,5120 pixels and five negative controls. `local/fixed-color-com01`
passes41 callers,4096 pixels and three controls. GetPixelShader stays NULL after
fixed draws; returning to programmable shaders preserves their constants.
Stage recording leaves live state intact, and Apply/Capture restore expected
values. Unsupported stage0 operations or enabled stage1 fail fixed draws.
An early test swapped Capture/Apply wrapper indices and failed; the corrected
fixture passes without a runtime stateblock change.

`local/uniform-fixed-fragment01` verifies null/size rejection, immutable copied
VS payload and empty PS output without installing bindings, plus800 concurrent
uploads and400 snapshots. `local/fixed-fragment-regression01` retains the earlier
103 translated callers,46 actual GPU draws,251/256 submissions, lifetime/fault
checks and pixel-negative controls. Strict Cortex-A57 ARM64/libnx compilation
passes for COM and the Vulkan backend.

The shader sources, wrappers and synthetic tests are original; no game binary or
asset is part of this change. Prepared fixture shaders/generated callers and
reports remain local. Hardware T&L caps stay off. No new game replay, motor
startup, NRO or Switch rendering measurement is claimed.

## Reproduce

```powershell
python pc/tests/test_directional_vertex_gpu.py --automatic --textured --output local/auto-new --linker local/vulkan-uniform-link01/link_d3d9_spirv.exe --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe
python pc/tests/test_directional_vertex_com.py --sdk ../sh2-nx/third_party/xboxrecomp --python-deps ../python-deps --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --fixture local/auto-new --output local/auto-com-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

For mode2 use --automatic --texture-alpha. For mode0 use --automatic alone.
Next verify multiple prepared modes switching on the same device, then broaden
required fragment states and lighting support before advertising full T&L.
