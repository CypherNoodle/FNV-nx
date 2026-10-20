# Optional true-pi trigonometric profile

The explicit `fnv-x87-protected32-v2` profile can select
`arm64-quad-true-pi-trig-v2`. It extends `arm64-quad-bounded-math-v1` with FSIN,
FCOS, FPTAN and FSINCOS. The strict default remains unchanged. The selected
numeric-emitter census has zero unsupported x87 opcode forms under this model.
The corrected production-profile census also applies the environment/addressing
overlay and retains one rejected FADD with an ES override in an inline
jump-table region. That site remains an explicit trap; its runtime reachability
is not established. Neither census proves support for every operand, engine
startup or performance on Switch.

The separate [v3 flat ES alias contract](x87-es-profile.md) resolves that
addressing rejection when ES is explicitly the same selector as DS. The earlier
profiles remain unchanged. The ordinary arithmetic C1 gap found by its native
oracle is documented separately; zero opcode-form gaps is not full flag fidelity.

## Numerical policy

This model computes with true-pi reduction rather than reproducing the physical
x87's finite internal pi approximation. Large-argument outputs can therefore
differ substantially from physical x87. C1 describes narrowing of the kernel
approximation; it is not guaranteed to reproduce physical x87 or the exact
mathematical C1. The model must remain opt-in.

An own integer Machin-series generator computes floor(2/pi * 2^256), checking
agreement at two guard precisions. A 320-bit integer product reduces the ext80
significand to a quadrant and a signed small remainder. Binary128 libm receives
only a small reduced argument. A remainder-margin guard rejects cases where
the truncated coefficient cannot provide sufficient relative precision. No
installed libm or SDK source is modified, and no table is loaded from SD.

Masked canonical inputs are required. Raw extended denormals, signaling NaNs,
pending exceptions, unmasked exception controls and unsupported stack states
fail atomically. Ordinary finite inputs with exponent below -1000 are outside
the model. Finite magnitudes at least 2^63 set C2 without pushing, subject to
stack preconditions. Full-stack FPTAN/FSINCOS fails before this range handling.
Ordinary FPTAN pushes one; special NaN/infinity results instead push NaN, as
observed in the native synthetic oracle. Host FPCR/FPSR is preserved.

## Build contract

The strict, first mathematical and trigonometric cores contain 99, 100 and 101
objects respectively. Profile, source hashes, compiler flags, generated table
and core inventory must agree. Trigonometric builds require both
`FNV_CPU_X87_QUAD_MATH=1` and `FNV_CPU_X87_QUAD_TRIG=1`, plus
`-frounding-math -fno-builtin -ffp-contract=off`. ARM64 binary128 support is
required; the host fallback does not silently emulate this model.

The table is packaged in the core object's read-only data. The CPU builder
accepts only the platform symbols appropriate to the selected numerical model.
No existing private generation or NRO is migrated automatically.

## Verification and limits

The synthetic suite executes real SDK-decoded/profile-emitted instructions
against the actual Switch-ABI core. Current checks include 10,584 native state
comparisons and 9,888 independent true-pi value comparisons at 210 and 310
decimal digits, including quadrant boundaries and neighboring ext80 values.
The largest observed numeric difference is one ext80 ULP; the acceptance bound
is two. This sampled bound is not a full-domain accuracy proof. There are 198
selected C1 differences from the mathematical reference, retained explicitly.

The suite also checks 39 atomic rejection cases, six non-ARM fail-closed cases,
host FP state, unselected registers and two real corrupted-table builds. A zero
coefficient must fail atomically; a phase-corrupted coefficient must be caught
by the independent numeric reference. Earlier mathematical instructions retain
their separate regression suite. None of these checks runs the game engine or
measures performance on hardware.

The complete paired private generations now finished without failed functions.
The code/data structural comparison preserved every retained function body;
the selected generation has 85,885 bodies and 648 unresolved call destinations.
Other non-x87 unsupported instructions remain. Actual full Switch CPU
compilation is a separate step and is not implied by a structural PASS.
