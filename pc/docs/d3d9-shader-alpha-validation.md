# Shader alpha and blending differential

`pc/tests/test_catalog_pair_gpu.py --constants --alpha-sweep` compares the
selected shader pair on Windows D3D9 HAL and the Vulkan provider. It uses
synthetic geometry and textures, a colored background, source-alpha blending,
and six draws with tint alpha 0, 0.25, 0.5, 0, 1 and 0.75. Alpha returns to zero
after a nonzero draw to exercise state changes. A clipped viewport on the final
draw still checks pixel-center correction.

Both zero-alpha reference frames must preserve every background channel. This
is an independent invariant in addition to the per-channel D3D9/Vulkan comparison
(tolerance one UNORM byte). Deliberately changing the Vulkan source blend factor
to ONE must cause a pixel mismatch, alongside the existing texture, coordinates,
constant-bank and pixel-center negative controls. Deferred shader registration
and NULL textures are also exercised with this blending state.

The mode requires the single-texture constant-bank fixture. It accepts either
own synthetic shaders or an explicitly hashed private package and shader pair.
The chosen pixel shader must use its float-bank alpha as expected by this
fixture; it is not an unrestricted shader conformance test. Extracted bytecode,
SPIR-V, packages and reports stay in the private output directory.

Validation on Windows desktop passed for both the own shader pair and a private
menu shader pair: 6,144 pixels per normal run, six negative controls, 6,144 NULL
texture pixels and 6,144 deferred-registration pixels. These checks are headless
and open no audio device. They do not establish interactive menu handling,
autonomous EXE startup, Switch execution or performance. No production blending
change was justified by these results.
