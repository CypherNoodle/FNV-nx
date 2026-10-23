# D3DX DDS texture creation through the COM device

The device module now supplies `d3dx9_38.dll!D3DXCreateTextureFromFileInMemory`.
It belongs to `fnv_d3d9_com_exports`, because it creates resources owned by that
module. An active module exposes this D3DX export even without a virtual factory;
a configured factory also exposes Direct3DCreate9. The metadata-only D3DX provider
remains independent and does not acquire a renderer dependency.

The initial supported profile is legacy DDS, power-of-two 2D dimensions up to
16384, with a complete mip chain. Formats are the existing COM texture layouts
ARGB8/XRGB8, RGB565, A8, L8, DXT1, DXT3 and DXT5. Premultiplied DXT2/4,
other pixel formats, non-2D images, resampling, missing mip generation and other
image codecs stop explicitly. Parsing a format's metadata alone does not mean
that resource creation or backend rendering supports it.

## Ownership and uploads

The existing CreateTexture path is shared through an internal helper; the COM
vtable and native method count are unchanged. It creates a managed texture,
uses the existing shadow allocator and parent/device reference accounting, and
calls the existing backend create/upload callbacks. There is no second texture
registry. Every mip is copied row by row into its correctly padded shadow and
uploaded through the provider. The input is snapshotted before allocation so it
can alias the output or shadow pool. Backend callbacks retain no guest source.

The output pointer is published only after all mip uploads succeed. A failed
upload releases the backend object, parent reference, mip metadata and shadow
allocation through the normal COM decrement/retire path. The caller's previous
output value is preserved on failure, matching the native D3DX failure oracle.
Successful resources participate in ordinary texture methods, binding and Release.
The translated x86 ABI pops four arguments (16 bytes).

## Compressed mip preservation is an explicit difference

Native Windows D3DX sometimes decompresses and recompresses compressed mip levels
smaller than a 4x4 block. The new path preserves the file's compressed bytes,
including these final levels. This avoids another lossy compression and keeps
the supplied DDS chain intact; it is intentionally not bit/pixel-identical to
that Windows utility behavior in all compressed tail levels. No native-parity
claim is made for those differences. DXT filtering/mip generation is not yet
implemented, and this preservation policy does not supply missing levels.

## Verification

`test_d3dx_texture.py` executes a translated IAT caller and uses a real Windows
D3D9 NULLREF device plus its D3DX9_38 DLL as the resource oracle. Across 160 DDS
textures, it checks level counts/descriptors and exact input-block copies for
921 mips. Independent Windows surface conversion compares visible pixels:
766 mip levels agree with native D3DX; 155 differ, restricted to compressed tails
with width or height below four. These counts are reported separately.

A 140-case failed-upload sweep covers every level and verifies unchanged output,
one backend destruction and unchanged native device references. Backend-create
failure also preserves output; a subsequent successful creation checks allocator
recovery. Invalid source/header and unsupported NPOT/missing-mip cases are checked.
The inherited texture suite passes its create/lock/upload/retry/lifetime tests.

The full synthetic Vulkan regression passes 46 draws, 251 completed submissions,
BC pixel/transfer checks, present/reset, failure injection and incorrect-render
negative controls. This is a regression of the shared COM/backend machinery,
not a rendered game frame or a dedicated D3DX-to-Vulkan pixel oracle. The private
engine experiment uses the Vulkan provider and passes complete-chain DDS creation.
The actual COM translation unit compiles for ARM64/libnx with strict warnings.
No new Switch hardware run has been performed.

## Current engine stop

With the full private assets, the startup diagnostic now passes prior DDS loads
and stops explicitly on missing mip generation: DXT5, 1024x1024, one supplied
level versus eleven required. It reports 39 reads totaling 4,897,280 bytes,
zero math-cache misses and zero engine draws/submissions. Production caps and the
paused Xbox route are unchanged. The next step needs genuine BC mip generation
(or a separately verified format-conversion policy), not fabricated mip contents.
