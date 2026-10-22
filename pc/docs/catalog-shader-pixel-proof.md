# Original catalog shaders: first bounded pixel comparison

Two original bytecodes selected explicitly by SHA-256 from one user's SDP
package now pass a desktop comparison between Windows D3D9 and our Vulkan
provider. The selected vertex program passes through POSITION and TEXCOORD0;
the pixel program samples a two-dimensional texture at s0. Both have no API
uniform registers. This is a deliberately bounded starting point, not a game
frame or evidence that the game binds these two programs together.

The test selects the originals without changing their tokens, translates and
links them offline, reflects the resulting modules, registers exact original
bytecodes in the Vulkan backend, creates the shader objects and a program,
then draws with synthetic vertex and texture resources. Shader inputs use an
explicit FLOAT4 position/FLOAT2 UV declaration. The original package is read
only; extracted bytecode and SPIR-V stay in private local output.

The Windows reference uses the same original programs, declaration, vertices,
point filtering and BGRA texture contents. Six frames cover four constant UV
texel centers and two interpolated UV draws, with changing texture contents.
All 6,144 pixels agree within one UNORM level per channel, including alpha.
Wrong-UV and swapped-texture-channel controls both produce a pixel mismatch.
An equivalent pair compiled from our own HLSL passes the same test and controls.
This establishes a functioning path for these particular original programs.

Transfers are drained before each oracle draw and every frame is read back.
Those waits are intentional test synchronization, not proposed engine pacing
and not a performance measurement. There is no new Switch execution or NRO.

## Reflection and inventory

Our offline adapter's `--describe` mode reports numeric input/output semantics,
uniform ranges, sampler types/indices and the legacy TEXBEM requirement. It
does not infer vertex element formats, runtime pairings or capability support.
It uses the existing external translator without changing its sources.

`describe_d3d9_package.py` checks the caller-supplied package hash, deduplicates
exact bytecodes by their digest, temporarily normalizes supported centroid
declarations for reflection and removes the temporary files afterwards.
Centroid accounting in this inventory is not restoration or rendering proof.
Unexpected translator process failures abort instead of becoming ordinary
shader rejections. Successful descriptions and explicit parse failures remain
distinct. One examined package has 983 records, 764 unique bytecodes, 763
accepted descriptions and one existing relative-array translation rejection.

The own-input description test verifies duplicate records, numeric semantics,
uniforms, sampler metadata, centroid accounting, rejected malformed bytecode,
identity mismatch and temporary-file cleanup. The existing centroid regression
still passes one/two-varying restoration and ten negative cases.

## Reproduce without game data

Build `link_d3d9_spirv.exe` with `build_shader_linker.py` after updating the own
adapter, then use fresh private output directories:

```powershell
python pc/tests/test_shader_description.py --output local/description-new --linker local/shader-describe-build01/link_d3d9_spirv.exe
python pc/tests/test_catalog_pair_gpu.py --output local/catalog-own-new --linker local/shader-describe-build01/link_d3d9_spirv.exe --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

To use original programs, the pixel test additionally requires `--package`,
`--package-sha256`, `--vertex-sha256` and `--pixel-sha256`. Both bytecodes must
belong to that exact package and satisfy this fixture's interface contract.
Neither filenames nor similar shader names are treated as pairing evidence.

## Constant banks and pixel-center correction

An additional explicitly selected original VS/PS pair now exercises four
vertex float registers (c0..3) and pixel register c5. The fixture's optional
`--constants` mode reflects upload plans instead of requiring empty UBOs.
Synthetic matrices vary scale and translation, and pixel constants vary tint.
The last frame uses a nonzero viewport origin with a smaller extent. Both the
original pair and an own HLSL equivalent pass 6,144 pixel comparisons each.
After submission the API constant banks are overwritten with zeros; pending
draws still read the snapshot they received at submission.

This exposed a real rasterization mismatch: D3D9 pixel centers are at integer
coordinates, whereas Vulkan uses half-integer centers. The provider now adds
0.5 to the framebuffer viewport X/Y origin after projection, retaining its
negative-height Y convention. Guest viewport values, dimensions, scissor and
shader constants stay unchanged. Bounds checks include the corrected origin.
See [Microsoft's D3D9 pixel mapping](https://learn.microsoft.com/en-us/windows/win32/direct3d9/directly-mapping-texels-to-pixels)
and [Khronos fragment-coordinate conventions](https://github.khronos.org/Vulkan-Site/glsl/latest/chapters/variables.html).

Five negative controls detect wrong UV, wrong texture channels, incorrect VS
constants, incorrect PS constants and removal of the viewport correction.
The last control intercepts pipeline creation to remove just that correction,
without altering original bytecode or the Windows reference. The fix also
passes the existing 103-caller backend regression (46 draws, 251 headless and
256 WSI submissions), the 9,216-pixel same-device fixed shader/Reset test and
strict ARM64/libnx compilation. This is still desktop GPU validation, not a
game frame or Switch performance result.

## Remaining integration

The startup catalog still contains original D3D9 bytecodes only. It does not
automatically install this privately prepared program. The engine still stops
at its Hardware T&L capability check; the required VS/PS versions remain zero.
Preparing actual bound pairs, their declarations, nontrivial uniform layouts,
resource types and shader variants remains necessary. The full game's shader
coverage and NVK execution on Switch are not established by this small proof.
