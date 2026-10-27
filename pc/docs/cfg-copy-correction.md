# CFG decoding correction for the captured PC copy routine

The hw03 native fault record exposed a corrupted hash length and byte-shifted
callee-preserved registers. The hash routine was reading inaccessible guest
backing, but the corruption occurred earlier in a translated CRT copy routine.

Its old linear decode crossed embedded jump-table data. Resynchronizing at table
targets retained stale overlapping instructions, including a false `INC ESP`.
That path shifted the register restore by one byte. The original analysis
extent also ended before the backward-copy table and its terminal handlers.

The correction uses reachable control flow and original indexed-table targets,
with an explicitly reviewed owner limit at the next independently indexed
entry. Data bytes are excluded from the instruction stream. No SDK sources,
game data, VM permissions or unrelated function bindings were changed.

## Verification

The supplied original x86 bytes run in Unicorn's x86 interpreter. Both the
legacy and corrected C bodies compile to real AArch64 code, which runs in
Unicorn's ARM64 interpreter with matching memory and initial register state.

The corrected body passed 3,744 cases covering zero and small lengths, larger
lengths through 8,192 bytes, all four source/destination alignments, and separate
buffers plus both overlap directions. All eight integer registers, output
memory and the direction flag were compared. The tested profile starts with
DF clear and the original non-SSE feature setting. The separate SSE helper
remains unchanged and is not established by this test.

The legacy negative control failed 1,905 cases. Its register-restoration path
reproduced the byte shift observed in the hardware dump. These are emulated
instruction-level comparisons, not Switch execution or a full-engine proof.

Only the affected compilation unit was rebuilt. Its other 249 function bodies
remain text-identical, and the other 358 generated objects are reused. The
baseline body comparison is retained as baseline evidence; it must not be
described as proving that the intentionally corrected body is unchanged.

## Reusable own tools

- `tools/aot_cfg_decode.py` opts explicitly selected ranges into bounded CFG
  decoding using the unchanged SDK's reachable-edge decoder.
- `tests/compare_copy_translation.py` accepts private code/body inputs and
  compiles/runs the x86/ARM64 comparison, including the failing legacy control.
- `tools/build_cfg_corrected_unit.py` reproduces the verified translation,
  checks the reviewed extent and unchanged neighboring bodies, and compiles
  the one corrected unit with the accepted Switch CPU ABI.
- `tools/link_switch_engine_cpu.py --corrections ...` validates pinned proof,
  source and object identities before replacing that unit in the complete link.
  Missing/foreign units and changed corrected objects fail before output.

`build_switch_engine_probe.py --require-cfg-corrections` refuses a CPU link
without the selected corrections. The hw04 log and build proof report
`cfg_corrected_units=1`. The compiled NRO still needs console verification;
this repair does not establish successful menu rendering or gameplay.

Generated replacements, original code, differential binaries and resulting NROs
stay private. Only own tools, tests and documentation belong in publication.
