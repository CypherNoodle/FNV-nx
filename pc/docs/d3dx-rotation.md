# D3DX yaw/pitch/roll rotation

The native `d3dx9_38.dll` export provider implements
`D3DXMatrixRotationYawPitchRoll` through the translated stdcall ABI: output
pointer and three float bit patterns, 16 argument bytes, returning the output
pointer without changing guest LastError. It constructs a row-vector matrix
for roll around Z, pitch around X, then yaw around Y, including homogeneous
identity components.

The initial supported domain is finite normal-or-zero angles within +/-64
radians and host round-to-nearest. Other angles, invalid destinations and
unsupported results stop explicitly before changing the output. All values
are calculated into local storage before committing the matrix.

Float libm and explicit float products/sums are used with FMA disabled. This
is a numerical implementation, not bit identity with Microsoft's trigonometric
approximation. Tests compare 5,000 matrices each against installed x64 and
x86 Windows D3DX, including zero, individual axes, mixed angles and the range
boundary, with a maximum absolute tolerance of 2e-6 per component. They also
check the translated return/stack ABI, LastError, output guards and six strict
rejections. These tests do not establish engine or hardware performance.
