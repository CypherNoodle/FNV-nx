# D3D9 render-to-texture on the native Vulkan provider

The provider supports a bounded render-to-texture path: a single-level,
DEFAULT-pool A8R8G8B8 texture with D3DUSAGE_RENDERTARGET. GetSurfaceLevel(0)
exposes its image as a render target; later draws can sample that same image.
This is needed for offscreen game passes, rather than copying every target
through CPU memory.

The Vulkan image uses BGRA8 with color-attachment, sampled and transfer usage.
The surface shares the texture's image/view and owns a version reference and
parent pin. It allocates no second image or CPU upload shadow. Repeated surface
queries preserve COM identity. Surface GetContainer returns the parent texture
for the supported resource/texture interfaces, including when its public
reference count had reached zero. The surface keeps its parent alive internally.

Clears and draws mark the version initialized only after successful submission.
Sampling flushes earlier queued clears, and image barriers order attachment
writes before shader reads on the same graphics queue. No CPU readback or idle
is inserted between the two passes. Ring pressure uses the existing optional
finite fence watchdog. Sampling the current render target rejects explicitly.
Recreating a surface wrapper preserves the initialized image's GENERAL layout.

GPU-only textures reject uploads and LockRect. Other formats, multiple mip
levels, managed pool, depth textures, MSAA and automatic mip generation are
outside this implementation. A zero mip count still requests the full chain;
it is accepted only when that chain has one level. Standalone surface
GetContainer remains unsupported. This does not broaden shader-model or HWTL
caps, and is not a complete D3D9 implementation.

## Verification

`test_catalog_pair_gpu.py --render-texture --constants` compares six two-pass
frames, 6,144 pixels, against Windows D3D9. Both passes use the same explicit
prepared shader pair and synthetic resources, first rendering into a texture
and then sampling it into the backbuffer. The test also runs with an unchanged
original shader pair from the user's private SDP package. A control that samples
the original source instead of the rendered texture must produce wrong pixels.
This pair has not been observed as a real engine draw.

The native test checks image sharing, feedback rejection, invalid mip/pool,
upload rejection, surface recreation and return to the initial GPU allocation
after releasing resources and bindings. The broader backend regression injects
image/memory/view creation failures and verifies unchanged handles and no leaked
allocation or published texture.

`test_catalog_pair_com.py` accepts that passing private fixture and runs the
same pixels through 47 SDK-translated synthetic x86 COM callers. Texture and
surface creation, Set/GetRenderTarget, GetSurfaceLevel, GetContainer, GetDesc,
binding, constants, Clear and Draw traverse COM. It verifies parent resurrection,
surface identity and that a rejected LockRect leaves its output unchanged.
The host uses native calls for provider setup and diagnostic readback only.

ARM64 Cortex-A57/libnx compilation covers both the provider and COM layer.
These are desktop pixel comparisons and cross-compilation, not Switch runtime
measurements, a cold EXE boot or a frame produced by the New Vegas engine.
Original shader bytecode, SPIR-V, generated callers and fixtures stay private.

```powershell
python pc/tests/test_catalog_pair_gpu.py --render-texture --constants --output local/render-texture-new --linker local/shader-describe-build01/link_d3d9_spirv.exe --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

For original shaders, provide the existing exact package and shader hashes.
Pass the resulting fixture directory to `test_catalog_pair_com.py` with the
SDK, Python dependencies, Vulkan headers and compiler paths. This fixture mode
is separate from mip-clamp and two-source-texture modes.
