# D3DX legacy DDS image information

`windows_d3dx.c` exports `D3DXGetImageInfoFromFileInMemory` using an original
bounded DDS metadata reader (`dds_image_info.h`). It is independent of the GPU
backend. It does not decode pixels, create a COM texture or submit Vulkan work.

The x86 stdcall bridge reads three DWORD arguments and removes 12 bytes. The
28-byte D3DXIMAGE_INFO output is seven explicit little-endian DWORDs: width,
height, depth, mip levels, D3DFORMAT, D3DRESOURCETYPE and D3DXIFF_DDS (4).
Inputs are permission/bounds checked, metadata is staged, and output is written
only on success. Unaligned source/output and partial overlap work. LastError is
unchanged. Null source or zero byte count returns D3DERR_INVALIDCALL. A NULL
output with nonzero source/length returns success without examining source;
this behavior is confirmed by the native DLL, even with invalid image bytes.

The parser supports legacy DXT1..5, A8R8G8B8/X8R8G8B8, A8B8G8R8/X8B8G8R8,
R5G6B5, X1R5G5B5/A1R5G5B5, A4R4G4B4, A8, L8 and A8L8 metadata. Support here
does not imply all those formats are renderable in the Vulkan/COM provider.
It recognizes 2D textures, complete six-face cubemaps and volume textures.
Zero mip count means one level, and zero volume depth is normalized to one.
All mip payload sizes, block rows, faces and depth slices are checked against
the supplied byte count with bounded/overflow-safe arithmetic. Pitch padding
and reserved DDS fields are not interpreted as image content. Like the native
D3DX oracle, this reader does not require the legacy header-size DWORD to be 124.

Bad magic, incomplete headers, wrong pixel-format structure size, incomplete
cubemap faces or a truncated mip payload return D3DXERR_INVALIDDATA without
changing output. Unknown FourCC/pixel layouts, zero dimensions and more than
256 mips stop explicitly as unsupported rather than pretending to decode them.
Recognized PNG/BMP/JPEG input also stops explicitly; those codecs have not been
connected to this API. This is not complete D3DX image-codec compatibility.

## Verification

`test_d3dx_image_info.py` compares 14,400 translated x86 calls against the
installed Windows D3DX9_38 x64 DLL: 16 formats, 2D/cube/volume shapes, NPOT
sizes, mip chains, short/corrupted inputs, unaligned buffers, output overlapping
input, NULL output, output guards, HRESULT and LastError. Three strict cases
reject an inaccessible source, inaccessible successful output and unsupported
DX10 header. Synthetic callers and the actual provider compile for ARM64/libnx.

The existing matrix suite still passes 10,000 x64 and 10,000 independent x86 DLL
comparisons, alias cases and seven explicit negative cases. Oracle DLLs, private
input records, generated code and test binaries are not part of the repository.

The full private asset fixture reaches this API for
`Data/Textures/Effects/WindowEnvironmentMap01.dds` and now passes its metadata
query. The [resource-producing bridge](native-dds-texture.md) now handles complete
power-of-two 2D DDS chains. Its next stop is missing DXT5 mip generation.
The run reports 20 reads totaling 4,521,984 bytes, zero math-cache misses and
zero engine draws/submissions. The complete asset set changes the call order;
these counts cannot be compared as a completion percentage with smaller fixtures.
No game frame or Switch hardware execution is claimed.

## Next integration

The resource-producing bridge shares COM creation, references and uploads.
It does not make missing mip generation, cubemap conversion or all DDS formats
available automatically. Its tests and remaining limits are described separately.

References:
- [D3DXGetImageInfoFromFileInMemory](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxgetimageinfofromfileinmemory).
- [Microsoft DDS format definitions](https://github.com/microsoft/DirectXTex/blob/main/DirectXTex/DDS.h).
