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

`tools/aot_x87_extended.py` is an **opt-in experiment**. Install it after the
mandatory frame overlay and provide `x87_stack.h` plus a context named
`fnv_x87_state`. It emits calls for the implemented memory and arithmetic forms.
All other x87 forms stop explicitly, so the new stack cannot silently mix with
the SDK's existing double-backed stack. The game's normal generation does not
enable this overlay, and its CPU state ABI has not been migrated yet.

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

The native oracle currently requires an x86-64 Windows host and GCC-compatible
inline assembly. It uses binary pipes, not text-mode standard streams.

`build_x87_extended.py --switch-abi` compiles with the same Switch CPU flags
as `build_cpu_runtime.py`, recording that tool and its supporting inputs too.
This is a compilation check, separate from the bare ARM64 emulation run.

## Remaining boundaries

This is not yet a complete x87 implementation or an engine-boot result.
Arithmetic on extended-denormal operands, noncanonical encodings, extended
signaling-NaN loads, C1, architectural tag/environment instructions, stack
fault delivery, unmasked exceptions and transcendental instructions remain
outside the implemented stack contract. Such unsupported stack inputs are
rejected rather than approximated. Unmasked controls are rejected without
modifying the stack; no guest exception delivery is claimed.

The low-level arithmetic API returns raised flags independently of masking;
it does not publish a guest instruction's result or deliver an exception.
Only the restricted masked stack contract is exercised by emitted sequences.
The existing game's runtime still uses its original state representation and
temporary ten-byte guard until a matching, fully audited generation is ready.
No Switch hardware result or NRO delivery follows from these tests.
