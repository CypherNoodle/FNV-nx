# Isolated ARM64 transcendental candidate

`native/x87_transcendental_candidate.c` implements experimental F2XM1, FPATAN
and FYL2X state transitions around the target binary128 math library. The
[explicit v2 profile](x87-quad-profile.md) now selects it in real AOT generation.
The strict v1 profile still leaves all 42 selected sites unsupported; the v2
bounded mathematical policy closes nine opcode-form gaps, leaving 33.

Inputs retain their full ext80 payload and exponent when mapped to binary128.
Results are rounded explicitly to 64 significand bits with the requested RC;
PC does not narrow these operations. The candidate handles signed zero, quiet
NaN propagation, infinity, masked-invalid and divide-by-zero cases, and the
binary operations' pop. Ordinary finite operands are bounded to exponents
within +/-1000 until extreme-range behavior is verified. Raw extended denormals,
sNaNs, unmasked exceptions, reserved PC, pending ES, empty required registers,
and out-of-domain F2XM1 stop before guest state is changed.

The math kernel temporarily uses nearest host FP rounding and restores FPCR and
FPSR. Candidate test compilation uses `-frounding-math`, `-fno-builtin` and no
FP contraction. These flags are part of the experimental build contract, not
an implicit promise about other compiler flags or arbitrary SDK versions.

The differential test executes 10,464 native x87/ARM64 comparisons over the
three valid PCs and four RCs. Of these, 3,552 are strict special/selected-value
checks. Measured ordinary results differ by at most one ext80 ULP from native
x87. The test also checks exception flags, TOP/occupancy, pop, untouched physical
registers, preserved CW, host FP state, output bounds and atomic failures.
Twenty negative cases and four non-ARM64 fail-closed checks are included.
The initial test decoded all three opcodes through the external SDK and an
isolated hook; the later v2 integration test uses the real profile emitter.
The candidate additionally compiles with actual Switch ABI flags.

**Acceptance is not complete:** 84 C1 differences remain. Some occur even when
the final payload is identical, so replacing C1 with a function of the final
rounded value would hide a real difference. C1 reflects rounding within a
physical x87's approximation, not simply whether the mathematical result is
representable. Native tests also observe PE and RC-dependent results at F2XM1
endpoints; those explicit endpoint cases are retained. The validation status
is `CANDIDATE_WITH_C1_DIFFERENCES` and the test exits unsuccessfully (code 2)
while those findings remain. This is not a production correctness or game boot
result, nor a performance measurement.

An independent mathematical reference now checks the 6,912 ordinary cases
(2,304 per operation) at both 210 and 310 decimal digits. Decimal exp/ln and a
half-angle arctangent series are separate from native x87 and target libm.
Rounded ext80 payloads and mathematical C1 agree exactly with the candidate in
every selected case; all 84 native-C1 and 192 native-payload differences are
explained by that reference. Corrupting a payload bit or C1 is rejected.
`PASS_SELECTED_MATHEMATICAL_MODEL` does not mean bit-identical native x87:
the two machines may round different internal approximations. The native
differential test deliberately retains its separate non-passing status rather
than redefining native observations. The explicit v2 profile now selects the
mathematical approximation policy; native bit-identity is not its acceptance
criterion. Its specification describes the later 11,544-case Switch-core sweep
and remaining extreme-C1 limitations.

`tools/audit_x87_c1_liveness.py` conservatively walks selected instruction paths
after these sites. A call, return, status export, uncertain FP operation, cycle
or budget exhaustion prevents an all-path overwrite result. The synthetic CFG
suite includes positive branches and negative alternate paths, loops, calls,
status saves, indirect jumps and traps. In the current private selection, one
of nine sites immediately makes C1 dead on all inspected paths; eight remain
unproven. This does not establish that eight sites use C1. Detailed addresses
and outputs stay private, and no liveness result enables an opcode.

```powershell
python pc/tests/test_x87_transcendental_candidate.py --arm-build /private/core/validation.json --cc C:/devkitPro/devkitA64/bin/aarch64-none-elf-gcc.exe --python-deps /path/to/python-deps --sdk /path/to/xboxrecomp --output /private/fresh-trans-test --host-cc /path/to/zig.exe cc
python pc/tests/test_x87_transcendental_math.py --validation /private/fresh-trans-test/validation.json --output /private/fresh-math-reference
python pc/tests/test_x87_c1_liveness.py --python-deps /path/to/python-deps --output /private/fresh-cfg-test
python pc/tools/audit_x87_c1_liveness.py --analysis /private/analysis --census /private/census/validation.json --python-deps /path/to/python-deps --output /private/fresh-c1-audit
```

The next work is the numerical-policy integration and extreme-range/underflow tests,
followed by an explicit numerical policy for the remaining trigonometric forms.
See the [quad research](x87-quad-research.md) and Intel's
[instruction reference](https://cdrdv2-public.intel.com/812383/253666-sdm-vol-2a.pdf).
