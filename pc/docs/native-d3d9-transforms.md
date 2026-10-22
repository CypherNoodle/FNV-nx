# Matrix state for the fixed-function vertex path

`d3d9_transform_state.h` owns 266 row-major binary32 matrices: view, projection,
eight texture transforms and 256 world transforms. Initialization sets identity.
Set/get preserve stored bits. Multiply stages the complete result before writing,
so the input can alias a stored matrix. The order is input * current, as required
by MultiplyTransform. Invalid identifiers/pointers leave state/output unchanged.
Callers must serialize access and validate guest memory before passing pointers.
Compile without floating-point contraction or fast-math.

This is a CPU component, not yet connected to COM slots44/45/46, stateblocks,
Reset or GPU constants. It does not transform drawn vertices, implement lights,
skinning or fixed texture stages, or justify enabling HWTRANSFORMANDLIGHT.
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
the fixture. No translated caller or hardware execution is claimed. Next integrate
atomic matrix snapshots into COM/stateblocks/Reset and select a bounded fixed VS
variant consuming world/view/projection and required lighting state. Validate
transformed pixels before advertising the corresponding DevCaps.

References: [MultiplyTransform order](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-multiplytransform),
[transform identifiers](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtransformstatetype).
