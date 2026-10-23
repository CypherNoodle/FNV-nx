# Prepared startup program bundles

`emit_prepared_bundle.py` combines explicitly selected offline-linked fixtures
into one private build header. Relative fixture paths resolve against the
manifest. An example manifest is:

```json
{
  "schema": 1,
  "programs": [
    {"fixture": "fixture-a", "inputs": [[0, 0, 0], [5, 0, 1]], "sampler_mask": 1},
    {"fixture": "fixture-b", "inputs": [[0, 0, 0], [5, 0, 1]], "sampler_mask": 1}
  ]
}
```

Run `python pc/tools/emit_prepared_bundle.py --manifest manifest.json --output
startup_program.h`. The header defines `FNV_STARTUP_PROGRAM_COUNT` and
`fnv_startup_programs`. The captured-startup host accepts it through the existing
`FNV_STARTUP_PROGRAM_HEADER` build definition. Single-program headers remain
supported. This adds no runtime directories or user installation steps.

The emitter validates each fixture using the single-program emitter's identity,
reflection and descriptor checks. Repeated original VS/PS pairs are deduplicated
only if their linked payloads, uniform plans, input maps and sampler masks agree.
Conflicting definitions are rejected. Identical typed data arrays are shared
across programs. The tool rejects invalid manifests, changed inputs and existing
output paths; it writes the header only after validation completes. Limits are
1 MiB per manifest, 4,096 requested programs and 32 MiB of emitted declarations.
These checks do not establish shader semantic equivalence.

`fnv_vk_prepare_programs` preflights every program's basic shape and original
bytecode membership before installing any program. A missing stage therefore
leaves the backend unchanged. Later backend validation or allocation failures
can leave a partially installed batch: destroy that backend instead of using
or retrying it. The optional failed index identifies the rejected program and
is `SIZE_MAX` for success or invalid batch arguments. Installation is exclusive
initialization, creates no public shader objects and submits no GPU work.

## Verification and current boundary

The own-input bundle test emits two distinct pairs sharing one VS, checks nine
invalid manifests and overwrite rejection, then exercises native Vulkan batch
initialization. It verifies missing-membership rejection without mutation,
teardown after partial failure and 64 repeated batches without catalog growth.
It checks metadata installation rather than pixels. The complete renderer
regression separately passes 103 translated callers, 46 draws, pixel controls
and fault injection, with 251 headless and 256 WSI submissions. The backend,
batch helper and a private three-program header compile for ARM64/libnx.

A captured PC startup diagnostic installs three selected original pairs with
four shared stage payloads. It still stops at the existing hardware T&L/shader
capability requirement: zero draws and zero GPU
submissions. Four workers stop with their parent; none fail independently.
This is neither a cold EXE boot nor an engine frame or Switch hardware test.
No caps are enabled by registering these programs. More engine contract and
shader coverage is required before enabling them.

Generated headers, fixture binaries and original or derived shader data remain
private and must not be committed. Only the tools, native helper, synthetic
tests and documentation are publishable project sources.
