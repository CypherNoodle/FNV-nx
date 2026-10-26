# D3DX vector, plane and matrix operations

The native provider implements the eight additional math imports used by the
PC EXE: MatrixTranspose, MatrixInverse, Vec4Transform, Vec3TransformCoord,
Vec3TransformNormal, PlaneTransform, Vec3Normalize and PlaneNormalize. Original
row-vector conventions are used. PlaneTransform applies the supplied matrix;
it does not implicitly construct an inverse transpose.

Inputs are copied before any output is committed, supporting in-place and
partially overlapping vector/matrix buffers. Destination spans match the actual
3-, 4- or 16-component result. Return pointers, stdcall stack cleanup and guest
LastError are checked through translated callers.

MatrixTranspose permutes 32-bit words and preserves all encodings, including
NaNs, signed zeros and denormals, independently of host rounding mode. Other
operations currently accept finite normal-or-zero values under round-to-nearest.
Nonfinite/subnormal intermediates, overflow, zero homogeneous divisors and
nonzero planes with zero normal length stop before changing outputs. A zero
vector and an entirely zero plane normalize to zero. The Windows DLL's unusual
FLT_MAX output for some degenerate planes is outside this supported domain.

MatrixInverse uses pivoted double-precision elimination, then commits float
results. Nonfinite determinants, determinants that underflow to zero, unsupported
results and infinity-norm condition estimates above 1e12 stop explicitly.
The determinant output may be omitted; overlap of that scalar with either
matrix is unsupported. A singular input returns NULL with matrix and determinant
outputs intact, matching the installed **x86** D3DX DLL. The installed x64 DLL
instead writes zero to the determinant in that case; the tests record this
architecture difference rather than substituting x64 behavior for the guest.

Arithmetic is numerically compared, not claimed bit-identical to Microsoft's
SSE implementation. The own test checks 1,000 cases for each operation against
x64 and x86 Windows, with four alias modes, zero vectors, singular matrices,
projection and large translated/scaled transforms. The numerical tolerance is
2e-5 times (1 + absolute reference component). Transposes are compared bitwise,
including another 2,000 arbitrary-bit matrices per architecture. Ten negative
cases check output preservation and invalid memory/arithmetic domains.

These are API tests. They do not establish world rendering, cold EXE boot or
Switch performance. No Windows DLL implementation is incorporated in the port.
