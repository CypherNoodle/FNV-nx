# Reusing the exact ARM64 diagnostic evaluator

`x87_reuse_evaluator.ReusingEvaluator` executes the same hash-verified standalone
ARM64 image as `x87_target_evaluator.Evaluator`. It keeps one Unicorn machine
and its translated code instead of mapping and translating a new machine for
every request. The original fresh-machine evaluator remains available as the
differential reference.

Before each call, the reusable evaluator restores its initial CPU context,
restores all allocated writable ELF sections from their pristine image, clears
the entire 64 KiB input/output/stack arena, and writes the request and output
canaries. A lock serializes callers. Writable ranges must come from the verified
ELF64 ARM64 image, remain inside the code/data arena, use supported section
types, and not overlap. The retained translation cache is not guest state.

The instruction limit, expected return PC, FPCR/FPSR preservation, result range,
output guards and unchanged-state requirement on rejection match the original
evaluator. No host libm or approximate answer is substituted. The new helper's
source hash is included in session provenance without modifying the frozen
target image's numerical-model identity.

`test_x87_reuse_evaluator.py` compares 756 synthetic requests against fresh ARM64
machines across all four rounding modes and precision controls 24/53/64,
including NaN and rejected extended denormals. It repeats them through four
callers, poisons writable memory/stack and registers between calls, and tests
malformed input and ELF bounds. Measured
times describe this diagnostic helper on the host; they say nothing about game
FPS or the Switch's production CPU runtime.
