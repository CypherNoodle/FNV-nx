# Sampled cube textures

The sampled cube path uses one cube-compatible Vulkan image with six array
layers and a cube view. Each face has its own full mip chain. Uploads update
owned CPU staging and install an immutable GPU version on first use. Pending
submissions retain the exact image version through real fences. Updating one
face preserves every other face and mip; backend memory limits still apply.

The COM device and D3DX memory loader create real cube resources. Guest object
identities remain separate from host handles. GetType reports CUBETEXTURE,
QueryInterface recognizes the cube/base-texture/resource identities, and texture
bindings retain internal references independently of public COM references.
The initial loader accepts square power-of-two legacy DDS files with all six
faces and a complete mip chain. Missing cube mip generation, image resampling,
premultiplied DDS conversion and cube render targets remain explicit unsupported
paths. They are not substituted with 2D placeholders.

Prepared shader pairs derive sampler shape from the offline-validated linked
pixel SPIR-V. Only non-array, non-depth, non-multisampled combined 2D/cube image
samplers are accepted. Descriptor bindings must agree with the supplied mask.
The framing/shape reader is not a SPIR-V semantic validator. A bound resource
with the wrong shape rejects before rendering. An unbound cube sampler receives
a real six-face opaque-black fallback image, matching the existing null-2D
policy. No general shader-model/cubemap caps are advertised by this change.

Synthetic verification covers 84 GPU face/mip transfer readbacks for RGBA8,
packed BGR24 and BC1, transactional rejection, independent updates and bound
resource lifetime. Own HLSL compiled to D3D9 and linked to SPIR-V samples all six
axes plus an unbound sampler: 1,792 actual GPU pixels. A Windows D3DX oracle
compares 60 face/mip descriptors and shadow copies through translated x86 COM
callers; compressed sub-block mip tails have 12 recorded native byte differences,
so byte identity with D3DX is not claimed for those tiny mips. All tests run
without audio playback. Desktop results and ARM64 compilation do not establish
Switch engine boot, visible gameplay or Switch performance.

`GetCubeMapSurface`, cube render-target creation, LOD methods and dirty-rectangle
methods remain outside this first COM profile and stop explicitly. This is
sampled-cube support, not a complete D3D9 cube interface.

The backend's configurable resource table now accepts up to 8,192 entries,
matching the existing COM ceiling. Defaults and device memory budgets are not
raised automatically. A synthetic Vulkan check creates/releases 2,050 real
buffer objects, in addition to the cube draw and pixel checks, and verifies
normal device teardown.

The larger historical all-in-one Vulkan fixture still fails its expectation
that an unbound 2D texture must reject a draw. The same failure occurs on the
pre-cube publication checkout; the backend already supplies an opaque-black null
2D texture there. This existing fixture inconsistency is not reported as a pass
or changed to hide a failure. The targeted cube and 2D transfer suites above
remain the evidence for this implementation.

Private desktop loading diagnostics pass the former D3DX cube-import stop. They
then expose two separate diagnostic limits: the 64 MiB COM shadow pool becomes
full, and after raising it privately to 128 MiB the 2,048-entry resource table
becomes full while substantial shadow memory remains. A private 4,096-entry
configuration passes those points. The later run reaches a diagnostic deadline
without a completed engine report; thread-wait investigation remains necessary.
These observations neither raise production defaults nor establish Switch
memory requirements or performance. The cinematic readback is still black.
