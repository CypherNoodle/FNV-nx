# Texture mip surfaces

`IDirect3DTexture9::GetSurfaceLevel` now exposes ordinary 2D texture mips,
in addition to the existing render-target texture surfaces. Each live mip
has a stable surface identity, its own public references and a retained
parent texture. `GetDesc` reports the parent's format, usage and pool with
the selected mip's dimensions.

Surface `LockRect` and `UnlockRect` use the same guest shadow and lock state
as texture `LockRect` and `UnlockRect`. A successful writable unlock copies
the selected rectangle through the existing upload callback. Failed uploads
remain retryable; read-only unlocks do not upload. Holding a mip surface keeps
its parent storage alive after the texture's public reference is released.

Vulkan represents these surfaces as logical aliases. Creating them allocates
no additional GPU image or image view. Uploads continue to use the parent's
immutable versions and completion fences. Ordinary mip aliases cannot be
bound as render targets or passed to color-surface readback. Standalone
surface locking and resetting while public surface references are held
remain unsupported by this implementation.

Validation:

- `test_d3d9_textures.py` executes SDK-translated synthetic x86 COM callers
  against the bridge and a Windows D3D9 NULLREF provider. Four managed mips
  cover descriptors, identity, shared shadows, writable/read-only locks,
  upload failure/retry, invalid levels and parent lifetime. The existing
  ten-format texture and negative tests still run.
- `test_vulkan_lazy_texture.py` checks actual Vulkan mip descriptors,
  stable identity, absence of extra GPU allocations, rejected render-target
  binding, parent retention and complete resource cleanup. Existing BC1/A8
  readbacks, partial uploads and transfer backpressure checks still run.
- Both production translation units compile with devkitA64/libnx and strict
  warnings. This is a compile check, not Switch hardware validation.

In a separate private captured-code desktop diagnostic, this route allowed
the translated engine to upload its font textures and draw readable Spanish
menu options. The result was verified by actual GPU readback. That diagnostic
uses captured text, prepared shader pairs, expanded diagnostic caps and a
target-math evaluator; it does not establish EXE cold startup, autonomous NRO
execution, general shader coverage or Switch performance. No captured game
data or generated game code belongs in this repository.
