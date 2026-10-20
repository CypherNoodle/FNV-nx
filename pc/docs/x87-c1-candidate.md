# Isolated arithmetic C1 correction

The production arithmetic bridge intentionally reports IE/ZE/OE/UE/PE and
does not reconstruct rounded-up C1. Its public header still states this
limitation. These experiments prepare a correction without changing the
sources or objects used by the in-flight full CPU and CRT builds.

`tests/x87_c1_candidate.c` is a reference candidate. For an inexact nearest
result it repeats the operation with rounding toward zero and compares result
magnitudes. Directed rounding can determine the increment from the result
sign. It preserves the original payload and exception result.

`tests/x87_c1_integer_candidate.c` instead compares the rounded magnitude with
the exact mathematical magnitude using integer significands and exponents.
Multiplication uses a 128-bit significand product; division cross-multiplies
by the positive denominator magnitude; square root compares the squared
result. Addition/subtraction align the significands when they fit in 128
bits and handle a smaller operand below half an ext80 ULP separately. Zero,
overflow and subnormal results are covered by the selected native corpus.
This is not an implementation of denormal-operand signaling, unmasked
exceptions, or every x87 instruction that can modify C1.

The oracle captures status **before** the final FSTP, which otherwise replaces
the operation's C1. On 11,940 cases across the five arithmetic operations,
three precision modes and four rounding modes, both candidates match native
payloads and defined exception/C1 bits. The test preserves undefined C1 for
invalid-operation results outside its comparison claim. The original core has
3,800 C1 differences in that corpus. The counted reference candidate performs
2,503 extra core evaluations; the integer candidate performs none. This is an
operation-count observation, not a Switch performance measurement.

`test_x87_c1_stack_candidate.py` modifies an isolated copy of the project's
own stack source and links that copy with the verified core objects. It
redirects only the three basic arithmetic paths and replaces C1 rather than
OR-ing a previous C1. Native comparisons cover 11,940 pop/sqrt cases, 9,552
register cases and 720 memory cases. Twelve invalid-operation, precision,
unmasked-control and pending-exception cases preserve the complete prior state.
Unselected registers, stack occupancy, TOP, CW and output guards are checked.

Production integration remains pending. It requires updating the arithmetic
bridge and the three stack status updates together, adjusting the declared
flag contract, and building fresh matched core/generation proofs. The current
port binaries still have the documented C1 limitation. These experiments are
not an engine startup, NRO or hardware result.
