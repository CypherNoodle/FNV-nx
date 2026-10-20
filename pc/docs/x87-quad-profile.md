# Explicit bounded quad-math profile

The v2 profile makes the bounded ARM64 FPATAN/FYL2X/F2XM1 backend available to
real AOT generation. This is a mathematical approximation policy, not a claim
of bit-identical physical x87 results or C1. The v1 profile remains strict and
does not emit these calls. No existing private generation or NRO is migrated
automatically.

```json
{
  "schema": "fnv-x87-protected32-v2",
  "contract_status": "experimental",
  "code_selector": 35,
  "data_selector": 43,
  "stack_selector": 43,
  "opcode_compatibility": true,
  "data_pointer_on_exception_only": false,
  "transcendental_model": "arm64-quad-bounded-math-v1"
}
```

The selectors above are an example guest policy, not inferred values for every
EXE. All fields are required. A v1 profile with the extra model key, a v2
profile without it, duplicate keys, and unknown models are rejected.

Build the matching core with `build_x87_extended.py`, `--switch-abi`,
`--transcendental-model arm64-quad-bounded-math-v1` and the actual devkitA64
compiler. It contains 100 objects, compared with 99 for the strict core.
The selected backend requires ARM64 binary128, `-frounding-math`,
`-fno-builtin`, no FP contraction, and the explicit
`FNV_CPU_X87_QUAD_MATH=1` define. Selecting the model on x86 fails compilation.
The normal host fail-closed candidate remains available without that define.

Generation pins model/profile/source/header identities and emits a guard
requiring the quad define. Strict generated headers reject that define.
`build_cpu_runtime.py` rejects a missing/mixed core, missing backend object or
required flags. Only this model permits `__multf3`, `atan2l`, `expm1l` and
`log2l` as platform dependencies, resolved by the final libm/libgcc link.
A relocatable link resolves the backend from the real core objects, and
generated code is checked for soft TLS without direct hardware thread-pointer
access. Per-context x87 state and the normal environment overlay remain in use:
FIP, FCS and opcode updates occur after a successful translated operation.

The synthetic integration test compiles a real generated unit with Switch ABI
flags, preserves strict gaps, verifies environment updates and exercises 18
negative configuration/compilation checks. Differential tests execute the real
profile emitter and backend object from the 100-object Switch core. The expanded
set has 11,544 native comparisons including 1,080 extreme cases up to exponents
+/-1000, plus 45 atomic negative cases and four host fail-closed checks.
Results differ by at most one ext80 ULP from this native x87. Exception and
stack checks pass. Native C1 differences are retained, not called bit-identical.

An independent reference at 210/310 decimal digits agrees exactly with payload
and mathematical C1 for 6,912 ordinary cases. That reference does **not** cover
the extreme sweep or prove correct rounding for all binary128 library inputs.
C1 in the selected model reflects rounding of the target kernel approximation.
For extreme ratios the kernel may already have rounded away a mathematical tail;
do not infer exact mathematical C1 over the full +/-1000 input domain.

Supported operands and rejection boundaries are described in
[the candidate specification](x87-transcendental-candidate.md). Raw ext80
denormals, sNaNs, unmasked exceptions, pending ES, reserved PC, missing stack
operands, nontrivial finite operands outside the bound, and F2XM1 outside its
defined range still stop before publishing guest state.

The identity-checked static census with this profile has 33 unsupported sites:
27 FSINCOS, two FSIN, two FCOS and two FPTAN. The strict census still has 42.
This closes nine *opcode-form* gaps for the optional model; it is not evidence
of reachability, complete operand coverage, full game compilation or engine
startup. No hardware test or new NRO is implied.

The separate [true-pi trigonometric model](x87-trig-profile.md) covers these
remaining forms with a 101-object core. Selecting the first model documented
here does not implicitly enable that model or its different numerical policy.
