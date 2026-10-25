# Packed BGR24 textures

Legacy DDS RGB24 metadata now reports D3DFMT_R8G8B8 (20) for the exact BGR
masks. The bounded metadata parser accounts for three source bytes per pixel,
including a tight 1x1 image with 131 total bytes. Other masks remain unsupported.
Metadata does not imply arbitrary image decoding or mip generation.

D3D9 texture objects preserve packed three-byte guest rows and existing aligned
shadow pitches. Vulkan stores RGBA8, expands BGR to RGB with alpha255 and retains
immutable texture versions and fence ownership. Render-target creation in this
format remains rejected. The generic upload converter checks source/destination
extents, row pitches and overlap before writing output.

D3DXCreateTextureFromFileInMemory follows the Windows D3DX default conversion:
BGR24 DDS becomes X8R8G8B8 (22). Each supplied mip expands BGR to four-byte rows
without changing the original DDS or returning a partially uploaded object.
This route still requires a complete power-of-two uncompressed mip chain;
resampling and missing uncompressed mip generation remain explicit stops.

Validation includes 14,401 metadata comparisons against Windows D3DX, with three
negative cases and an ARM64 fixture compile; 3,000 CPU upload cases across six
formats and23 negatives; translated COM callers covering11 texture formats and
44 mips; native D3DX pixel/descriptor comparisons and upload rollback; and actual
headless Vulkan readback of BGR24 with an odd pitch and partial patch. Existing
compressed mip-tail differences remain reported by the D3DX suite rather than
being treated as identical. No Switch hardware result is implied.

The motivating private desktop diagnostic reached a real 1x1 BGR24 DDS during
new-game loading. Only own code and synthetic tests are published: no image,
shader bytecode, captured game code, executable or NRO is included.
