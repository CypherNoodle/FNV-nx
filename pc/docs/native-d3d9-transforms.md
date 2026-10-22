# Matrix state for the fixed-function vertex path

`d3d9_transform_state.h` owns 266 row-major binary32 matrices: view, projection,
eight texture transforms and 256 world transforms. Initialization sets identity.
Set/get preserve stored bits. Multiply stages the complete result before writing,
so the input can alias a stored matrix. The order is input * current, as required
by MultiplyTransform. Invalid identifiers/pointers leave state/output unchanged.
Callers must serialize access and validate guest memory before passing pointers.
Compile without floating-point contraction or fast-math.

COM slots44/45/46 now expose SetTransform/GetTransform/MultiplyTransform through
three registered native methods (100 total). Guest matrices are serialized as
64 little-endian bytes. Argument memory and identifiers are validated before
state/output mutation. Access is serialized under the COM guard.

Recorded SetTransform values are stored with a per-matrix mask. GetTransform
reads live state during recording. Windows MultiplyTransform modifies live state
even during BeginStateBlock and is not recorded; the provider follows that
observed behavior. Capture updates only masked matrices. Apply commits them only
after the existing numeric/binding/constant transaction succeeds. Empty blocks
leave matrices untouched. CreateStateBlock typed snapshots remain unsupported.
Successful Reset restores all matrices to identity; failed Reset preserves them.
Retained recorded blocks can reapply their matrices after Reset.

Matrix state is not yet consumed by GPU fixed-function draws. Lighting,
skinning and fixed texture stages remain pending; HWTRANSFORMANDLIGHT stays off.
The existing programmable draw path still requires linked vertex/pixel shaders.

The desktop oracle `pc/tests/d3d9_transform_state_host.c` creates a real Windows
D3D9 HAL device with software vertex processing. It compares default matrices
and set/multiply/get across all266 valid slots (532 comparisons), rejects invalid
slots without mutation and checks source/destination aliasing. Products use small
integers with exact results to distinguish multiplication order; this does not
prove arbitrary rounding, exceptional float handling or Switch performance.

Example from the project root (use a fresh private output directory):

```powershell
New-Item -ItemType Directory local/transform-state-new
clang -std=c11 -O1 -Wall -Wextra -Werror -fno-strict-aliasing -ffp-contract=off -Ipc/native pc/tests/d3d9_transform_state_host.c -ld3d9 -luser32 -o local/transform-state-new/oracle.exe
./local/transform-state-new/oracle.exe
```

The local MSYS clang64 run passed after correcting signed HRESULT comparisons in
the fixture. The extended oracle also verifies Windows recording behavior with a noncommuting
scale/translation pair. Private Vulkan regression `local/vulkan-transforms-com02`
passes97 translated synthetic callers, all266 matrix slots, invalid/null matrix
arguments, recording/capture/apply, failed/successful Reset and recorded blocks
surviving Reset. Existing46 draws,251/256 submissions and pixel/failure controls
still pass. A strict ARM64 Cortex-A57/libnx compile of the complete COM unit
passes. The first regression attempt failed because a test enum name collided
with the texture fixture; it was renamed before the passing run.

These results do not claim transformed GPU pixels or hardware execution. Next
select a bounded fixed VS variant consuming world/view/projection and required
lighting state. Validate transformed pixels before advertising DevCaps.

References: [MultiplyTransform order](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-multiplytransform),
[transform identifiers](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtransformstatetype).
