# Experimental extended x87 core

`native/x87_extended.c` stores an explicit 64-bit significand and 16-bit
sign/exponent. No intermediate binary64 projection is used for the implemented
arithmetic. `native/x87_stack.c` keeps eight such registers, occupancy, TOP,
control and sticky exception bits in an independent guest context.

The standalone core supports add, subtract, multiply, divide and square root,
four rounding modes and the three defined precision controls. Binary32/64
loads and stores convert through the extended representation. Ten-byte guest
memory is accessed bytewise, independent of C structure padding. Arithmetic
and conversions use Berkeley SoftFloat 3e with its 8086 specialization, rather
than the host's `long double` ABI. The SoftFloat process-global environment is
serialized inside this experimental wrapper; this is a correctness baseline,
not a measured performance optimization.

Register copies, exchanges, stores with and without pop, discard, and the exact
constants zero/one are also implemented. Tests include the `FSTP ST(0); FLD1`
pattern, plus independent copied contexts, an eight-register stack and rejected
operations that must not modify their destination or context.

The experimental overlay also covers register `FCOM`/`FUCOM` variants and
their pop forms, `FLDCW`, `FNSTCW`, `FNSTSW` (memory and AX), `FNCLEX`, and
`FNINIT`. Comparisons update C0/C2/C3 without projecting their operands to
binary64. The supported control-word subset has masked exceptions and a
defined precision encoding; reserved and unmasked settings are rejected.

Signed integer loads support 16, 32 and 64 bits. `FIST`/`FISTP` convert under
the guest rounding mode; `FISTTP` truncates independently of it. `FRNDINT`
rounds within the extended representation. The 16-bit conversion checks its
own range, returns the integer indefinite value on masked invalid conversion,
and does not incorrectly retain an inexact flag from an intermediate result.

General register arithmetic now includes every ST(i) destination, normal and
reverse subtraction/division, and pop/non-pop variants. Memory arithmetic
supports binary32/64 and signed integer16/32 operands. Memory comparisons,
`FTST`, `FCHS` and `FABS` are implemented too. Memory arithmetic preserves a
signaling NaN's identity during payload selection and handles NaN-before-DE
exception priority instead of treating the operand as a separate `FLD`.

`tools/aot_x87_extended.py` is an **opt-in experiment**. Install it after the
mandatory frame overlay and provide `x87_stack.h` plus a context named
`fnv_x87_state`. It emits calls for the implemented memory and arithmetic forms.
All other x87 forms stop explicitly, so the new stack cannot silently mix with
the SDK's existing double-backed stack. The game's normal generation does not
enable this overlay. A separate experimental runtime ABI now preserves its
extended state per thread; see [CPU context integration](cpu-x87-context.md).
The whole-engine generation has not been migrated.

The constant-load family includes PI, log2(e), log2(10), log10(2) and ln(2).
Their extended payloads follow the guest rounding mode; precision control does
not narrow these constant loads. `FXAM` classifies raw representations, including
empty registers, and reports the physical payload sign in C1 even when empty.
`FFREE` changes occupancy without erasing the payload. Architectural `FNINIT`
resets control, status and tags while retaining physical register values; fresh
context construction is a separate operation.

Ten-byte memory loads/stores preserve all raw payloads, including signaling
NaNs, denormals, pseudo-denormals and unsupported encodings. This transport
support does not imply arithmetic support for every encoding. Numeric consumers
reject unimplemented inputs without changing the destination. `WAIT` and `FNOP`
check pending exception state rather than silently discarding it; guest exception
delivery remains unsupported.

The independent environment API implements protected-mode 32-bit images:
28 bytes for environment save/restore and 108 bytes including all registers.
`fnv_x87_stack_save32` writes logical ST(0)..ST(7) register payloads while the
tag word describes physical R0..R7. Environment-only saves mask exceptions;
full saves reset the controls/tags and pointer metadata without erasing physical
payloads. Restores use the tag word for occupancy, then classify nonempty raw
payloads when saving again. Rejected lengths, control settings and pending old
exceptions leave the context and metadata unchanged.

Instruction/data pointers, selectors and the last opcode are explicit caller
metadata. They must come from audited guest state, never host addresses. The separate `install_environment` overlay now translates those four
instructions and tracks guest pointers for successful supported operations.
It requires explicit guest CS/DS/SS selectors, FOP compatibility policy and FDP
exception-only policy. It never infers this contract from host CPUID. WAIT and
control instructions preserve pointer metadata; FNINIT clears it. Instruction
addresses include prefixes. Register-only operations retain the otherwise
undefined data pointer. The engine's normal generation still does not enable
this overlay, and runtime context integration remains required. Real-mode and 16-bit images,
FXSAVE/SSE state, and unmasked exception delivery are not implemented here.

The opt-in generation/compiler contract and SAHF flag bridge are described in
[generation integration](x87-generation.md). Remaining comparison-to-EFLAGS and
math forms still require migration before a complete game generation can pass.

## Reproducing the checks

Obtain the [official SoftFloat 3e distribution](https://www.jhauser.us/arithmetic/SoftFloat.html).
The ZIP used during development has SHA-256
`21130ce885d35c1fe73fc1e1bf2244178167e05c6747cad5f450cc991714c746`.
Keep its sources and `COPYING.txt` intact. The build tool performs no download;
each build records its exact compiler, source/header identities and objects.
SoftFloat's license must also accompany any future redistributed binaries.

```sh
python pc/tools/build_x87_extended.py --softfloat /private/SoftFloat-3e --output /private/native-build --cc /path/to/zig cc
python pc/tools/build_x87_extended.py --softfloat /private/SoftFloat-3e --output /private/arm-build --cc /path/to/aarch64-none-elf-gcc
python pc/tests/test_x87_extended.py --build /private/native-build/validation.json --arm-build /private/arm-build/validation.json --sdk /path/to/xboxrecomp --python-deps /path/to/python-deps --output /private/fresh-test
```

The native oracle runs actual x86 x87 instructions, including ten-byte loads
and stores, and compares result bytes and the supported exception flags. The
ARM test compiles the emitted instruction sequences and executes them under
Unicorn against those oracle bytes. Tests include extended precision that
binary64 loses, overflow/underflow, halfway rounding, signed zeros, infinities,
quiet/signaling NaN conversions and binary32/64 denormal-load flags. The native
test also exercises four concurrent rounding controls and invalid calls.

`tests/test_x87_control.py` adds native and emitted-ARM64 comparisons, NaN
ordered/quiet behavior, TOP/pop cleanup, AX high-half preservation, control
round trips, clear-exception behavior and initialization. It also tests the
legacy infinity-control bit without assuming it affects modern arithmetic.
`tests/test_x87_integer.py` tests exact integer loads beyond binary64's
precision, integer boundaries, half-way rounding, non-pop stores, truncation,
NaN/overflow integer-indefinite results and extended round-to-integer.
Both scripts accept `--build`, `--arm-build`, `--sdk`, `--python-deps` and a
fresh `--output`, using the same completed builds shown above.

`tests/test_x87_arithmetic_forms.py` covers 168 register/memory encodings,
checking all eight stack registers so an incorrect destination or pop is
visible. Native execution uses the original operation opcode bytes; emitted
ARM64 executes the same decoded form. The control suite additionally checks
memory comparisons and sign operations, excluding undefined sign-op condition
bits and starting the native FPU in an explicit known state.

`tests/test_x87_special.py` checks the constant family, raw ten-byte transport,
`FXAM`, `FFREE`, `FNINIT`, `WAIT` and `FNOP` against native x87 and emitted ARM64.
It includes 56 raw patterns, precision/rounding controls, sticky flags and failure
checks that require unchanged state and destination bytes. FXAM's C1 is compared
even for empty registers, following the
[Intel instruction reference](https://cdrdv2-public.intel.com/812383/253666-sdm-vol-2a.pdf).
It accepts the same build and output arguments as the other suites.

`tests/test_x87_environment.py` compares the environment API directly with
native `FRSTOR`, `FLDENV`, `FNSTENV` and `FNSAVE`, then executes the same API
under ARM64 emulation. It covers 8,192 cases over all TOP positions, partial
occupancy, normal/special raw values, pointer metadata and physical-register
preservation after reset. A separate native check covers all 64 exception-mask
combinations during environment save. These are API checks, not translated
instruction or engine-boot checks. Run with `--build`, `--arm-build`,
`--python-deps` and a fresh `--output`.

`tests/test_x87_environment_translation.py` runs those native image vectors
through decoded x86 instructions and emitted ARM64, under four explicit FOP/FDP
profiles: 32,768 image comparisons. It separately checks 32 ARM64 metadata
sequences and 28 structural control/reset cases. The native helper verifies FIP
for ordinary/prefixed loads, FNOP, preserving controls and FNINIT directly.
Those FIP observations use actual native labels; the legacy opcode policy and
FDP feature variants are architectural checks, not host-feature equivalence.

The SDK decoder reports full-state operands as four bytes and keeps a 28-byte
environment width even with an operand-size prefix. This overlay inspects the
encoded prefixes and uses explicit 28/108-byte sizes. It rejects 16-bit images,
16-bit addressing and FS/GS operands rather than silently losing segment state.
It currently models selectors without the newer deprecated-FCS/FDS feature.
The chosen CPU contract must respect that limit. No guest profile is selected
or changed by these tests. The architectural rules follow
[Intel's x87 state description](https://cdrdv2-public.intel.com/843827/253665-sdm-vol-1-dec-24.pdf).

Two development oracle runs observed pointer metadata cleared unexpectedly;
a diagnostic repeat matched the software result. Their failure logs remain
preserved. The helper now keeps each instruction sequence in one volatile asm
block and prints repeat diagnostics on a mismatch, but never accepts a retry
inside a failed run. A later complete run passed. The cause of the intermittent
host observation is unresolved; an engine or hardware result is not inferred.

The native oracle currently requires an x86-64 Windows host and GCC-compatible
inline assembly. It uses binary pipes, not text-mode standard streams.

`build_x87_extended.py --switch-abi` compiles with the same Switch CPU flags
as `build_cpu_runtime.py`, recording that tool and its supporting inputs too.
This is a compilation check, separate from the bare ARM64 emulation run.

## Remaining boundaries

This is not yet a complete x87 implementation or an engine-boot result.
Arithmetic on extended-denormal operands, noncanonical encodings and register
signaling NaNs remains restricted. Numeric rounding C1, engine-integrated architectural
environment instructions, stack-fault delivery, unmasked arithmetic exceptions
and transcendental instructions remain outside the implemented contract.
Unsupported numeric inputs are rejected rather than approximated. Arithmetic
with unmasked controls is rejected without modifying the stack; the pending-state
check used by WAIT does not itself deliver a guest exception.

FCOMI integer-EFLAGS integration and transcendental operations are still missing.
Control/status support above is a subset, not a full environment implementation.

The low-level arithmetic API returns raised flags independently of masking;
it does not publish a guest instruction's result or deliver an exception.
Only the restricted masked stack contract is exercised by emitted sequences.
The default game build still uses its original state representation and
temporary ten-byte guard until a matching, fully audited generation is ready.
No Switch hardware result or NRO delivery follows from these tests.
