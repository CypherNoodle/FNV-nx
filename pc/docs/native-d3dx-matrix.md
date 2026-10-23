# Native D3DX matrix multiply, transpose and profiler options

The same four-export provider also supplies [legacy DDS metadata](native-dds-image-info.md).
[Texture creation](native-dds-texture.md) is supplied separately by the COM module
for supported power-of-two 2D DDS chains.

`windows_d3dx.c` supplies an original x86 stdcall bridge for
`d3dx9_38.dll!D3DXMatrixMultiply` and `D3DXMatrixMultiplyTranspose`.
They compute row-major `left * right`, optionally transpose the completed result,
and return
the guest output pointer and removes 12 argument bytes. All input matrices are
snapshotted before committing output, supporting output aliasing either input
or partially overlapping it. Flat guest bounds and optional VM permissions are
checked before accessing data. The bridge does not load a Windows DLL at runtime.

The bounded arithmetic profile is binary32, round-to-nearest, finite normal
values and signed zero, with normal or zero intermediate products/sums.
NaN, infinity, subnormals, non-nearest rounding and overflow stop explicitly
without publishing a partial result. This is not complete IEEE exception/MXCSR
emulation. Compile with FP contraction disabled, without unsafe reassociation.
The implementation matches the installed x86 SSE oracle's reduction: rows 0/3
use `(p0+p1)+(p2+p3)`, rows 1/2 use `((p0+p1)+p2)+p3`. Desktop-x64 behavior
alone was insufficient to establish that rounding contract.

`d3d9.dll!D3DPERF_SetOptions` handles options 0 and 1 as external-profiler policy
with no render effect, because this provider has no external PIX attachment.
Option 1 forbids external PIX profiling; unknown options stop explicitly.
The function has no meaningful return value and preserves guest LastError.
It does not disable the project's independent diagnostics.

## Verification

`test_windows_d3dx.py` binds four synthetic IAT callers and verifies:

- 10,000 small integer matrix cases against the installed Windows x64 DLL;
- 10,000 wider fractional matrix cases, bit for bit, against the installed
  original x86 DLL through SysWOW64 PowerShell;
- distinct output, output equal to either input, and partial overlap;
- stdcall stack balance, returned pointer and unchanged LastError;
- seven explicit stops: NULL, inaccessible output, NaN, subnormal, overflow,
  non-nearest rounding and unsupported profiler flags. Matrix rejection leaves
  the output sentinel unchanged.

The x86 oracle uses the system DLL, freezes its identity and writes private
input/output records. `d3dx_matrix_x86_oracle.ps1` requires a 32-bit process and
refuses to overwrite output. It does not change system execution policy; the
test's PowerShell invocation uses a process-local policy override. An initial
single-reduction implementation matched x64 fractional cases but failed x86;
the final implementation passes the x86 oracle. Early fixture builds also
needed the existing 8 MiB diagnostic stack budget. Strict ARM64/libnx compilation
passes; matrix execution on Switch has not been measured.

Enable `FNV_STARTUP_D3DX` and link `native/windows_d3dx.c` when building the
captured-startup diagnostic. The host then registers these exports before IAT
binding. Other D3DX exports remain unresolved explicitly; this does not provide
the entire D3DX DLL or expand shader caps.

In the private startup experiment with capability injection, matrix multiply
now completes and execution advances from 205,184 to 205,523 translated calls,
where it requests D3DPERF_SetOptions. That request motivated the bounded profiler
bridge above. This remains a captured-code replay with exact x87 replay input,
not a cold EXE boot, engine frame or Switch hardware run.

With the profiler bridge connected, the diagnostic advances to a new target-only
trigonometric request at 205,682 calls. Evaluating that state on the existing
verified ARM64 core and extending the private replay cache advances to 209,617
calls, where an unregistered guest indirect target stops execution. Four workers
stop with their parent and none report independent failures. The final replay
reports 1,086 exact math hits and zero misses. Only the new request was evaluated
in this continuation; the existing 1,080 seed records were reused by identity.
There are still no GPU draws or submissions from the engine in this experiment,
and its capability injection remains separate from ordinary backend caps.

Only original sources/tests are published. Windows DLLs, oracle output, captured
game code, shader binaries and generated code remain private.

References: [D3DXMatrixMultiply](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxmatrixmultiply),
[D3DPERF_SetOptions](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3d9/nf-d3d9-d3dperf_setoptions).

## Multiply-transpose verification

The shared arithmetic helper commits either normal or transposed indices only
after snapshotting both inputs and checking every intermediate. No intermediate
result is written to guest output. The original x86 D3DX transpose result matched
the transposed x86 multiply result in 10,000 wider fractional cases; the translated
stdcall bridge then passed an independent 10,000-case original-x86 transpose
oracle plus 10,000 Windows-x64 transpose cases. Both retain four alias modes,
returned pointer, stack and LastError checks. An additional inaccessible-output
transpose case checks unchanged output on rejection (eight strict stops total).
Existing bounded arithmetic limitations apply to both exports.

In the private shader-admission startup experiment, this export advances the
explicit stop from matrix transpose to `_BinkSetSoundSystem@8`. That is a Bink
bridge gap, not evidence of a rendered frame. The ARM64/libnx translation unit
compiles with strict warnings; no new Switch test is requested for this change.
