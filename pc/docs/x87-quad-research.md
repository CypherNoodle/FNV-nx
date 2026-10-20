# Target quad-math research

The remaining 42 selected instruction sites use FSINCOS, FSIN, FCOS, FPTAN,
FPATAN, FYL2X and F2XM1. These are not yet enabled in the extended translator.
They need mathematical results, argument ranges, exception/status behavior and
stack changes, including C2 and conditional pushes for the trigonometric forms.

`tools/probe_x87_quad.py` compares native x87 results with a separately linked
AArch64 binary using the installed target libm. Its target harness requires a
16-byte `long double` with 113 significand bits and the extended exponent range.
Inputs map exactly from their 80-bit payload to binary128; the Python observer
rounds the returned payload back to 64 significand bits using nearest-even.
No binary64 projection is used for the input or final payload comparison.

The Windows native oracle and ARM64 harness use 52 synthetic cases. The tool
records source/compiler/library hashes, numeric payloads, absolute error, ULP
distance, errno, FPSR and emulator faults in a fresh output directory. It exits
unsuccessfully if a target execution faults. It is a research tool, not an
accuracy acceptance test or a translation backend.

```powershell
python pc/tools/probe_x87_quad.py --cc C:/devkitPro/devkitA64/bin/aarch64-none-elf-gcc.exe --python-deps /path/to/python-deps --output /private/fresh-quad-research --host-cc /path/to/zig.exe cc
```

The first reproducible run matched the selected 6 atan2, 4 log2/multiply and
6 expm1/ln2 cases exactly after rounding. This small sample does not establish
coverage for FPATAN, FYL2X or F2XM1. The sine, cosine and tangent samples differed
for some arguments, and two large arguments per function triggered unmapped
reads inside the target range-reduction helper. Caller/callee argument passing
and the reduction tables need investigation before that library path can be
used. These observations come from the isolated linked ARM64 harness, not a
Switch hardware run.

Large ULP distances near a zero must be interpreted alongside absolute error.
The mathematical library and native x87 can use different approximations of
pi during range reduction; Intel discusses that limitation in its
[x87 architecture description](https://cdrdv2-public.intel.com/843827/253665-sdm-vol-1-dec-24.pdf).
Library availability alone cannot justify enabling the remaining instructions.
Their current explicit unsupported paths remain active while this work proceeds.

## Bounded reduction ABI experiment

The installed ARM64 disassembly confirms that the ld128 trig callers set
X0..X4 before calling `__kernel_rem_pio2`, while that helper consumes X5 as
the reduction-table pointer. The upstream
[ld128 reduction header](https://github.com/devkitPro/newlib/blob/master/newlib/libm/ld128/e_rem_pio2l.h)
also shows a five-argument call. This is evidence about the inspected library
and isolated ELF, not a claim that every SDK release has the same problem.

`--repair-reducer-abi` links `tests/x87_quad_reducer_probe.c` through GNU ld's
`--wrap`. The adapter supplies a 32-limb 2/pi table calculated independently
with integer Machin-series arithmetic at two guard precisions. It accepts only
the bounded argument/exponent contract exercised by this probe and traps on
other calls. It does not modify the installed SDK, game runtime, or translator.
The tool records the helper's six incoming arguments and return address,
hashes the generated table, and resets integer registers between input cases.

The original 52 cases reproduce six invalid accesses without the adapter;
the adapted run completes all 52. A seeded sweep of 256 additional inputs per
operation completes 1,588 cases without target faults. With nearest rounding:

| Operation | Cases | Exact ext80 payloads | Largest payload distance |
| --- | ---: | ---: | ---: |
| atan2 | 262 | 259 | 1 ULP |
| log2(x) times y | 260 | 256 | 1 ULP |
| 2^x minus 1 | 262 | 256 | 1 ULP |

The trigonometric sweep also completes, but differs substantially from native
x87 for some inputs; the adapter fixes memory safety, not numerical equivalence.
These are selected normal operands, not exhaustive coverage of signed zeros,
NaNs, infinities, subnormals, rounding controls, precision flags or stack effects.
All 42 pending instruction sites remain unsupported. No performance or hardware
claim follows from this experiment.

```powershell
python pc/tools/probe_x87_quad.py --cc C:/devkitPro/devkitA64/bin/aarch64-none-elf-gcc.exe --python-deps /path/to/python-deps --output /private/fresh-reducer-sweep --repair-reducer-abi --sweep 256 --host-cc /path/to/zig.exe cc
```
