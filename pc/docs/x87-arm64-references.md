# ARM64 x87 reference review

This review records architectural references, not an imported Wine/FEX backend
or a measured game performance improvement. FNV's active CPU route remains
static x86-to-ARM64 translation.

GameNative's Proton/Wine fork was inspected at
`5353d9acc967207a40c11cb1b2c1aa06ec197269`. Its
[ARM64EC signal layer](https://github.com/GameNative/proton-wine/blob/5353d9acc967207a40c11cb1b2c1aa06ec197269/dlls/ntdll/signal_arm64ec.c)
interfaces with an external CPU emulator. This file is not an implementation
of x87 arithmetic. The WoW64 CPU source is also useful for reviewing Windows
FPU context layouts and tag conversions.

GameNative's FEX fork was inspected at
`555e8bd2d1f07fc7455e21324c9f9c7303c7ee7d`. Useful references include:

- [SoftFloat wrapper](https://github.com/GameNative/FEX/blob/555e8bd2d1f07fc7455e21324c9f9c7303c7ee7d/FEXCore/Source/Common/SoftFloat.h): explicit extended payloads, operation wrappers and wider software paths for transcendental operations. These paths still need their own compatibility tests; their existence does not establish exact x87 transcendental results.
- [Context-aware SoftFloat addition](https://github.com/GameNative/FEX/blob/555e8bd2d1f07fc7455e21324c9f9c7303c7ee7d/External/SoftFloat-3e/src/extF80_add.c): passes a state pointer through the operation, avoiding a shared rounding/exception environment. This is relevant to replacing the experimental core's global serialization with independently controlled contexts.
- [F64 dispatcher](https://github.com/GameNative/FEX/blob/555e8bd2d1f07fc7455e21324c9f9c7303c7ee7d/FEXCore/Source/Interface/Core/OpcodeDispatcher/X87F64.cpp): explicitly ignores the x87 precision control in that path. It must not be treated as an exact replacement for extended precision.
- [Stack optimization pass](https://github.com/GameNative/FEX/blob/555e8bd2d1f07fc7455e21324c9f9c7303c7ee7d/FEXCore/Source/Interface/IR/Passes/x87StackOptimizationPass.cpp): caches stack values/TOP and falls back when its assumptions are invalid. Similar ideas may reduce redundant state traffic in AOT blocks after the state ABI is integrated.

The current FNV core still uses the official SoftFloat 3e distribution and its
serialized wrapper. No source from these references has been copied into that
core. A future context-based adaptation needs license/provenance review,
differential tests, and measurements on Switch. A faster binary64 route needs
a proven compatible subset and correct rounding, range and exception behavior;
changing every extended value to `double` would violate existing tests.
