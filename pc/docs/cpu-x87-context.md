# Experimental extended x87 CPU context ABI

`FNV_CPU_EXTENDED_X87=1` selects a separate CPU runtime build. It is not enabled
by the normal engine generation or the current Switch application build.
The default state layout and SDK floating-point path remain the legacy model.
No hardware or engine-boot result follows from the tests below.

The extended state stores `fnv_x87_stack` and `fnv_x87_environment` directly,
including all eight significands/exponents, occupancy, TOP, control/status and
guest pointer metadata. No register is projected through `double`. The two
structures live in the SDK's `RECOMP_TLS` storage class, so the normal per-thread
runtime save/restore, attach/detach, state accessors and fault snapshots carry
them together with the integer and vector registers.

Call `fnv_cpu_state_init` before filling a context. Extended contexts carry
`FNV_CPU_X87_STATE_ABI`; attachment and replacement validate the marker, TOP
consistency, supported masked control settings, pointer opcode width and normal
stack/FS bounds. Invalid replacement leaves the live context unchanged. This
is validation of the staged core contract, not Windows exception delivery.
Raw physical register payloads are retained even when their tags are empty.

## Mixing protection

All public CPU functions acquire the `_x87e1` suffix in the extended build.
Every native bridge that calls the CPU API must be compiled with the same
mode. An old state consumer cannot resolve the corresponding CPU API symbols
against an extended runtime. This also prevents a bridge from passing a legacy
state structure to a function that expects the larger extended representation.
It does not create a binary plug-in compatibility layer.

The extended runtime defines no legacy `g_fp_stack`, TOP, comparison or control
symbols. Its force-included `fnv_sdk_runtime.h` maps old names to forbidden
identifiers. Unused SDK function-prelude macros can remain, but expanding an
old FP macro fails compilation. A precompiled old FP object fails linking.
This intentionally exposes any remaining SDK path that assumes the old
comparison model; such a path needs its own verified translation.
The external SDK is not modified.

## Verification

`tests/test_cpu_x87_context.py` uses only a synthetic PE and original tests.
It translates five functions, then executes them through the actual CPU runtime
on two real host threads. Checks include 2,000 push/pop pairs with low significand
bits that binary64 cannot retain, 20 detach/reattach cycles, state replacement,
a nested arithmetic call checked against native x87, and a fault snapshot after
a successful extended load. Invalid markers/settings and seven compile/link
mixing attempts must be rejected.

The test also reuses the managed `CreateThread`/`ExitThread` bridge suite with
all participating units compiled for the extended ABI: 285 managed threads,
explicit exits, nested workers, suspend/resume, TLS isolation, abandoned mutexes
and eight strict failure paths. Those managed guest worker programs contain
integer code; the independent two-thread test above exercises x87 payloads.

The same generated code and runtime compile with the real Switch CPU flags.
Disassembly must reference `__aarch64_read_tp` without direct hardware thread
pointer accesses. A partial link uses the separately verified Switch-ABI
SoftFloat/core objects and permits only the known platform symbol set.
This checks compilation and TLS relocations; it does not execute on Switch.

```sh
python pc/tests/test_cpu_x87_context.py --build /private/native/validation.json --arm-build /private/arm/validation.json --switch-build /private/switch/validation.json --sdk /path/to/xboxrecomp --python-deps /path/to/python-deps --output /private/fresh-context-test
```

The three supplied core builds must match current sources. Obtain the third
with `build_x87_extended.py --switch-abi`, retaining the dependency license.
`tests/test_cpu_runtime.py` separately checks the default legacy runtime,
including its existing concurrency and fail-stop behavior.

## Remaining integration

The opt-in generation/compiler contract is now available; see
[generation integration](x87-generation.md). The complete captured-code generation,
guest selector/CPU policy, required x87 forms and startup validation have not
been exercised as a combined game build. Do not enable the macro on an old NRO or
reuse old CPU/native objects. Normal build tools do not select this mode by default. Extended generation
requires its explicit profile and the matching Switch core proof. A matched generation, rebuilt bridges and audited dependency/link/TLS
checks are required before a hardware artifact can be delivered.
