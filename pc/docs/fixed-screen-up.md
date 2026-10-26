# Bounded screen-space fixed-function UP draws

The Vulkan provider can now draw an unlit XYZRHW/TEX1 quad without application
vertex or pixel shaders. Previously its UP path always required a programmable
pair and returned INVALIDCALL for these legal D3D9 fixed-function draws. This
caused a black cinematic in the private desktop startup diagnostic even though
the menu and later loading artwork rendered.

The supported declaration is POSITIONT float4 at offset 0 and TEXCOORD0 float2
at offset 16, stream zero, stride 24. Stage zero modulates texture by default
white diffuse and selects texture alpha. Stage one is disabled, lighting and
depth testing are off, and viewport depth range is 0..1. Other fixed-function
combinations remain rejected; no capabilities are expanded by this change.

The host explicitly registers an original prepared vertex/pixel pair with
`fnv_vk_backend_fixed_screen`. It must compile and validate these shaders
offline and link POSITIONT/TEXCOORD inputs and sampler zero before use. The
vertex shader reconstructs clip W from RHW and applies the current viewport
scale and bias. Existing half-pixel rasterization handling remains authoritative.
This is a real texture-sampling pixel shader, not a flat-color substitute.

Both DrawPrimitiveUP and DrawIndexedPrimitiveUP use existing owned transient
buffers, resource pins and GPU completion fences. The pair has internal pins,
survives a healthy Reset and releases with the device. Guest shader bindings
and constant banks are unchanged. A missing or incompatible prepared pair
continues to fail explicitly.

`test_fixed_screen_gpu.py` compiles the original shaders offline and compares
9,216 RGB channels against actual Windows D3D9 rendering. Cases include a
shifted viewport, varying RHW with indexed drawing, and Reset after public
shader references reach zero. Invalid pair registration and lighting-enabled
draws reject. Native reference rendering uses a hidden window and never plays
audio. Existing unlit and automatic directional GPU regressions remain separate.

These desktop tests and a libnx ARM64 compile do not establish Switch gameplay
or frame rate. The game diagnostic's exact shaders, assets, generated sources,
screenshots and binaries stay local. Cinematic visibility must also be checked
in the diagnostic and then on hardware; other world-rendering paths remain
outside this bounded implementation.
