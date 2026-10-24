# D3D9 pixel samplers without a texture

The Vulkan backend samples an internal opaque black texel when a prepared
2D pixel sampler has no texture bound. Binding NULL remains a guest-visible
NULL; the fallback has no public resource handle or device reference.
A nonzero missing resource or an uninitialized real texture still rejects.

The one-texel image is created and uploaded lazily under the backend lock.
Its immutable version follows the existing ordered transfer queue and real
completion fences. Each draw retains its version until GPU completion;
backend teardown releases it after all frame references. It persists across
healthy device resets and does not consume a public texture slot.

The Windows D3D9 HAL oracle samples an unbound sampler and returns BGRA
(0, 0, 0, 255). The catalog pair GPU suite compares six frames, including
viewport and shader-constant variants, against that oracle. Normal texture
comparisons and incorrect-data negative controls still run separately.

Run pc/tests/test_catalog_pair_gpu.py with a trusted external shader linker,
Vulkan headers, a Windows C compiler and a fresh private output directory.
The null-texture mode is included in single-texture tests, including
--constants. This is desktop differential validation, not Switch hardware
validation or a general D3D9 sampler conformance claim.
