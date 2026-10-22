# Light state and bounded normal transformation

Slots51..54 now implement SetLight, GetLight, LightEnable and GetLightEnable.
They serialize D3DLIGHT9's26 DWORDs (104 bytes) explicitly for the x86 guest.
Invalid/partial guest pointers and unknown get indices return INVALIDCALL
without changing output. Properties are stored verbatim; storage does not imply
that an arbitrary light type or parameter set can be rendered.

`d3d9_light_state.h` stores arbitrary DWORD indices in a sorted sparse array,
with binary lookup and geometrically grown storage. The allocation budget is
4096 distinct entries, independent of index magnitude. It does not limit indices
to4095 or announce4096 active renderer lights. Allocation failures return
OUTOFVIDEOMEMORY; changes reserve all live/recording destinations before
publishing new defaults. No light storage is allocated at device creation.

Setting/enabling a new index creates a disabled default directional light in
live state, even during stateblock recording. Its diffuse RGB is1, direction
is(0,0,1), and other fields are zero. SetLight/LightEnable recording masks are
separate: applying enable-only changes does not overwrite live properties.
Enabled gets return128, not1. Capture validates all recorded live indices before
the existing transaction; after Reset, a missing index fails Capture explicitly.
Apply reserves all needed storage first and can recreate recorded indices from
defaults. Successful Reset empties the live bank; failed Reset leaves it intact.
The native provider retains its block lifetime policy across Reset, without a
claim that Windows permits Reset with outstanding stateblocks.

Windows HAL proof `local/light-oracle03`:609 comparisons covering300 sets and
300 enables, default creation, recording of new/existing indices, separate
property/enable masks, Apply/Capture and Reset. The durable oracle stays below
index4001. An exploratory run with near-maximum DWORD indices terminated in the
Windows retail path; it is not a compatibility proof for those indices.
CPU-only budget/fault tests separately cover UINT32_MAX indices,4096 entries,
allocation rollback, field masks and recreation after Reset.

Translated COM proof `local/vulkan-lights-com02`:103 synthetic x86 callers,
light roundtrips/defaults/masks/stateblocks/pointer rejection and failed/successful
Reset, alongside the previous46 actual GPU draws and251/256 submissions.
Negative pixel controls remain enabled. These draws are not lit draws.
Strict ARM64 Cortex-A57/libnx compilation passes. An initial GCC build rejected
a same-line if as misleading indentation; the formatting was corrected.

## Normal matrix helper

`d3d9_normal_transform.h` prepares three column float4 registers from the
inverse-transpose of the affine world*view3x3. It uses the same row-vector and
FP-contraction policy as position transforms. Normal normalization remains a
separate shader operation. Translation is excluded from the3x3 result.
Double cofactor arithmetic is preparation work for matrix changes, not intended
as per-vertex CPU work. Singular, non-affine, nonfinite and out-of-float-range
results fail without changing output; no identity fallback is substituted.

`local/normal-transform01` passes2213 comparisons with an independent exact
rational Gauss-Jordan inverse and191 singular/nonfinite/non-affine rejections.
Inputs make world*view exactly representable, so this establishes matrix order
and layout rather than arbitrary binary32 rounding equivalence to Windows.
The helper also passes strict ARM64 compilation.

Run the source tests with fresh private output folders:

```powershell
python pc/tests/test_light_state.py --output local/light-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
python pc/tests/test_normal_transform.py --output local/normal-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

Next connect supported directional lights/materials and normal registers to a
lit shader, with actual pixel comparisons and explicit unsupported variants.
The unlit shader still ignores materials/lights. No Hardware T&L capability,
engine boot, game frame, Switch execution or updated NRO is established here.

A bounded lit path is now connected and pixel-verified; see [directional vertex rendering](directional-vertex-rendering.md). The unlit path remains separate.
