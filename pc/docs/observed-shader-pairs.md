# Preparing observed shader pairs

The package catalog permits creation of shader objects. Drawing additionally
requires a prepared Vulkan program for the exact vertex/pixel pair. Loading a
catalog alone does not supply those linked programs. A missing pair remains an
explicit rejected draw; it must not select an unrelated shader as a substitute.

`tools/prepare_observed_shader_pairs.py` consumes a private diagnostic directory
containing up to 64 observed triples: `world-pair-NN-vs.bin`, `-ps.bin`, and
`-decl.bin`. Capture bytecode and the actual declaration used together in a draw.
This is evidence of the pairing, not proof of visual correctness.

Pass `--capture`, a fresh `--output`, `--linker` and its `--linker-build` report.
The tool verifies the external linker binary and own adapter identities. It
checks a bounded D3D9 vertex declaration, reads explicit vertex DCL semantics,
links with the existing centroid-preserving adapter, and associates reflected
SPIR-V vertex locations with those original input registers. Sampler bindings
come from the linked fragment module. Unsupported formats or mappings are
reported per pair without inventing replacements. VS 2/3 use encoded instruction
lengths. A bounded VS 1.1 family also accepts explicit DCL, NOP, MOV, ADD, MAD
and DEF instructions with fixed operand counts. Legacy shaders without explicit
input semantics or with other instruction shapes remain rejected.

The output includes per-pair headers, a manifest for `emit_prepared_bundle.py`,
and a report with input hashes and explicit rejected-pair reasons. A partial
batch is labeled `PARTIAL`; only successful pairs enter its manifest. All these
artifacts contain private game data and stay outside public commits. Neither a
successful link nor a generated header establishes semantic correctness,
execution on Switch, or hardware performance.

`tests/test_observed_shader_pairs.py` verifies register/location association and
rejects duplicate semantics, missing terminators, unsupported packed formats,
truncated shader instructions and reflected inputs without explicit evidence.
