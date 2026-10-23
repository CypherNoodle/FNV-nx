# Hardware vertex creation and the startup capability gate

`CreateDevice` now accepts either SOFTWARE_VERTEXPROCESSING or
HARDWARE_VERTEXPROCESSING, with the existing FPU_PRESERVE/MULTITHREADED flags.
Hardware creation additionally queries the provider's packed caps and requires
HWTRANSFORMANDLIGHT. A missing bit, unavailable query or failed provider remains
a creation failure; no device handle or presentation parameters are published.
Mixed, pure, missing and combined processing modes remain unsupported. The
selected flags are preserved in GetCreationParameters. Both modes use the
existing GPU provider; no new software transform fallback is introduced.

The normal Vulkan provider still does not advertise HWTRANSFORMANDLIGHT or
shader versions. There are verified fixed WVP and one-directional-light paths,
but their FVF, state and fragment-selection contracts are bounded. Three
prepared game program pairs do not establish complete shader-model coverage.

## Presentation mismatch

A private startup experiment changing only HWTRANSFORMANDLIGHT reaches the
next rejection: insufficient vertex/pixel shader versions. The observed minimums
are VS 1.1 and PS 1.3. A further private diagnostic injects just those versions
to inspect later initialization; this is not enabled in the project backend.

That diagnostic requests X8R8G8B8, 1280x720, one DISCARD backbuffer and no
automatic depth. The previous startup fixture provided A8R8G8B8 with automatic
depth, so its prepared device could not satisfy the request regardless of HWTL.

`fnv_vk_backend_presentation_format` now selects ARGB8 or XRGB8 for the implicit
backbuffer during exclusive initialization, before COM construction. XRGB uses
the same BGRA8 storage and opaque WSI composition; its unused byte is not alpha
and has no guaranteed readback value. This does not enable XRGB render textures,
texture sampling of the backbuffer or additional offscreen surface formats.
The descriptor reports the selected format, retained across Reset. Public
backbuffer references, queued work or rendering reject format changes.

The captured-startup fixture now prepares XRGB with automatic depth disabled,
matching the observed game request. Explicit depth surfaces remain a separate
resource path. This is a fixed diagnostic configuration, not dynamic resizing
or a hardware-tested Switch display policy.

## Checks

The translated COM/Vulkan regression checks both software and hardware creation,
missing caps, provider failure, mixed/pure/combined modes and preserved creation
flags. The hardware fixture injects its provider declaration explicitly while
the underlying Vulkan caps stay unchanged. It executes the same 46 GPU draws,
251 completed submissions, pixel-negative controls and cleanup checks; WSI
retains 256 completed submissions and four COM presents.

The separate XRGB fixture uses 103 synthetic translated callers. It verifies
the requested descriptor, absent auto-depth, two GPU clears with 2,048 pixel
checks, rejection of late format changes and preservation across COM Reset.
Both software and test-declared hardware creation pass. These are synthetic
resources, not a game frame. ARM64/libnx compilation passes for COM and Vulkan.

The private capability injection and generated game code are not published.
Caps in the ordinary backend and normal startup path remain conservative.

Microsoft documents the creation modes and separate vertex-processing limits
in [Processing Vertex Data](https://learn.microsoft.com/en-us/windows/win32/direct3d9/processing-vertex-data).
Its [SkinnedMesh sample](https://github.com/microsoft/DirectX-SDK-Samples/blob/main/C%2B%2B/Direct3D/SkinnedMesh/skinnedmesh.cpp)
also checks HWTL and shader version separately when selecting hardware mode.

## Observed boundary after the presentation correction

With the private HWTL/VS1.1/PS1.3 diagnostic injection, the captured game now
creates its device successfully using behavior flags 0x44 and then calls
GetDeviceCaps on that device. It still terminates with Gamebryo initialization
failure before issuing any draw. Static inspection of the next translated
routine shows that it rejects MaxTextureBlendStages < 2 or
MaxSimultaneousTextures < 2; our current profile reports both as zero.
The original startup gate remains in the normal, uninjected run.

The next required renderer work is therefore a verified two-stage texture
contract (including texture-stage operations and coordinate selection), and
matching reported stage/texture limits. Increasing those limits merely to
satisfy this branch would not implement that path. Shader-model coverage also
remains independent of the two-stage requirement. No engine frame or new
Switch NRO is established by the successful diagnostic CreateDevice call.
