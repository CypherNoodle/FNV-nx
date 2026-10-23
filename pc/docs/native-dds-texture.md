# D3DX DDS texture creation through the COM device

The device module now supplies `d3dx9_38.dll!D3DXCreateTextureFromFileInMemory`.
It belongs to `fnv_d3d9_com_exports`, because it creates resources owned by that
module. An active module exposes this D3DX export even without a virtual factory;
a configured factory also exposes Direct3DCreate9. The metadata-only D3DX provider
remains independent and does not acquire a renderer dependency.

The initial supported profile is legacy DDS, power-of-two 2D dimensions up to
16384. Formats are the existing COM texture layouts
ARGB8/XRGB8, RGB565, A8, L8, DXT1, DXT3 and DXT5. Premultiplied DXT2/4,
other pixel formats, non-2D images, resizing and other image codecs stop
explicitly. Missing levels are generated for DXT1/3/5; uncompressed inputs
still require a complete chain. Parsing a format's metadata alone does not mean
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
claim is made for those differences. Supplied levels remain byte-identical
even when additional levels must be generated.

## Missing BC1/BC2/BC3 levels

The bounded decoder expands the last supplied mip to straight RGBA bytes.
A rounded 2x2 box filter generates each missing level in place; edge texels are
duplicated when an axis reaches one. Filtering is in stored byte space, without
sRGB conversion or premultiplied-alpha weighting. BC1 alpha uses a threshold of
128; transparent colors are excluded from endpoint fitting. BC2 alpha is encoded
as four-bit values, and BC3 uses its interpolated alpha block. Partial 4x4 blocks
replicate their edge texels. This is an explicit filter policy, not a claim of
bit-identical D3DX mip generation for arbitrary images.

Color compression uses unmodified, pinned public `stb_dxt` under its MIT license;
its provenance and full upstream license are retained in `third_party/stb_dxt`.
The decoder, filter, DDS assembly and integration are project code. The helper
limits RGBA scratch to 64 MiB and compressed output to the configured shadow
budget. It allocates both buffers before generation, outside the COM lock;
failed creation/uploads retain the existing rollback path and output semantics.
Each invocation owns its scratch; no global encoder initialization is needed.
`tools/fetch_stb_dxt.py` verifies the pinned dependency offline by default.

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
recovery. Generated BC1/2/3 chains add 197 flat-color native pixel agreements and
three rollback checks. Invalid source/header and unsupported NPOT/premultiplied
cases are checked.
The inherited texture suite passes its create/lock/upload/retry/lifetime tests.

`test_bc3_mips.py` covers all three BC formats despite its historical filename:
4,000 native decoder comparisons (at most one channel unit difference), 1,000
exact independent box-filter cases, 81 complete/partial-chain cases, 108 negative
cases and two BC1 cutout checks. Random BC2/3 native comparisons use canonical
color endpoint order: the Windows D3DX utility interprets reversed endpoints
using BC1-style interpolation. Reversed BC3 endpoints are instead checked with
explicit expected blocks. A 1024-square BC3 chain took about 7 ms on this desktop;
Switch generation cost and image quality in the actual game remain unmeasured.

The full synthetic Vulkan regression passes 46 draws, 251 completed submissions,
BC pixel/transfer checks, present/reset, failure injection and incorrect-render
negative controls. This is a regression of the shared COM/backend machinery,
not a rendered game frame or a dedicated D3DX-to-Vulkan pixel oracle. The private
engine experiment uses the Vulkan provider and passes complete-chain DDS creation.
The actual COM translation unit compiles for ARM64/libnx with strict warnings.
No new Switch hardware run has been performed.

## Current engine stop

The private startup experiment passes the previously missing BC3 and BC1 chains.
A runtime-observed missing function entry was separately audited and translated
in private generated code. The experiment then exposed an omitted `Data/Shaders`
folder in its private asset fixture; adding the user's original packages moves
the stop to shader creation. A bounded observation of all 1,412 requested bytes
matches a water pixel shader in package 002, whereas the prepared catalog used
package 003. Selecting package 002 alone does not supply its translated programs
and still ends in a null shader access. Read-only creation tracing records
816 requests, 811 rejected by the provider because their programs are not
prepared. The next step is preparing the actual
requested programs and checking HRESULT handling, not bypassing the catalog.

No engine frame, cold EXE boot or new Switch hardware run is demonstrated.
Production caps and the paused Xbox route are unchanged.
