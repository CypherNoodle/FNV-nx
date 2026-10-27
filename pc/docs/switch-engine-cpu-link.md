# PC CPU generation and Switch runtime integration

The integration run rebuilt all 359 generated CPU units for AArch64, with no
compiler failures. They combined with the current native runtime and two
explicit supplementary functions in one relocatable object. The archive
symbol inventory covered its remaining strong external references.

This is a CPU/runtime partial link, not a final executable or an engine NRO.
No execution on Switch is established by this result. Relocatable object size
is not the final NRO size or its memory requirement.

## Comparing the desktop and Switch generations

`tools/audit_generation_body_bridge.py` indexes generated function definitions,
including braces nested in code while excluding comments and string literals.
It compares normalized function text, not just function names. Candidate
supplemental sources are explicit inputs; duplicate definitions are rejected.

The comparison found 88,830 identical common bodies, 314 functions retained
only in the older complete generation, and two additional candidate bodies.
The retained functions still rely on the earlier complete-generation audit;
they are not covered by the desktop text comparison.

The only optional call-gate normalization requires the exact known tracing
macro definition. Text equality does not prove compiler equivalence, function
reachability, recovered entry boundaries, or execution correctness.

## Rebuilding and linking

`tools/build_switch_engine_generation.py` rebuilds the complete generated-unit
inventory from its prior proof, using the current runtime ABI. It rejects
changed generation dependencies, incompatible flags and incomplete inventories.
Old native CPU/PE objects are excluded; the current runtime supplies them.
Progress is saved in `progress.json` during compilation.

`tools/link_switch_engine_cpu.py` requires the body comparison, generated-object
proof, current runtime proof, and an explicit supplementary-entry manifest.
Each binding pins its source/body identity and separately supplied boundary
evidence. Entry boundaries must be independently reviewed: the linker does not
infer them from address alignment or from the fact that C compiled.

The linker verifies the existing sorted dispatcher inventory and refuses
overrides. An added resolver checks the explicit supplementary bindings before
delegating to the original dispatcher. Its collision guard also fails closed
at runtime. No unsuccessful instruction is replaced with a success result.

## Checks and remaining work

Own tests cover function-boundary extraction and comparison guards. A separate
three-unit synthetic fixture compiles with the real AArch64 toolchain. Negative
link controls reject a missing binding, changed body identity, failed object,
and an attempt to register an existing guest address, before creating outputs.

Private generated C, objects, manifests, captures and resulting artifacts stay
outside the public source set. The tools and synthetic test sources are reusable.

The next integration is the engine bootstrap: its guest memory layout, provider
initialization, private shader catalog, Horizon window and final libnx/Mesa
link. The older CRT diagnostic deliberately stops before engine entry. Archive
symbol availability remains a direct-coverage check rather than proof of final
archive closure or runnable packaging.
