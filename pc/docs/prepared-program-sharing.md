# Sharing prepared shader stages between programs

The startup preparation helper previously registered both stages for every
pair. Registering a second pair with an already installed VS failed, even when
its original bytecode, linked SPIR-V and uniform plan were identical. A game
catalog reuses stages, so preparing each pair independently could not scale.

The exact-reuse APIs are `fnv_vk_backend_ensure_shader` and
`fnv_vk_backend_ensure_program`. They register missing entries and reuse only
exact matches. A stage must match its original code, SPIR-V, plan size and each
plan entry's kind, first register, count, offset and stride. A pair must match
its stages, ordered semantic/location map and sampler mask. Existing objects
and catalog storage are never replaced. Both operations hold the backend lock.

The older `register_shader` and `link_program` APIs still reject duplicates;
their existing callers and negative tests keep that contract. All bytecodes
must still belong to the supplied original catalog before the helper mutates
anything. Preparation remains an exclusive initialization operation. A later
failure can leave earlier registrations installed: destroy the backend on a
failed overall initialization rather than retrying an incomplete catalog.

This is exact reuse, not automatic compatibility matching. If linking another
PS requires different VS SPIR-V or a different plan for the same original VS,
the exact-reuse API fails explicitly. The preparation helper now also supports
[per-pair variants](prepared-program-variants.md); it selects the pair's own
payload rather than silently using the earlier module. No caps change.

## Validation

`test_prepared_program_sharing.py` accepts two passing private pixel fixtures,
checks original/module identities and revalidates their UBO and interface data
with the existing private-header emitter. It requires an identical VS module
and plan, two distinct PS programs and the explicit POSITION/TEXCOORD mapping
used by the bounded pixel fixture. It does not infer an engine pairing.

Two original pairs now alternate their PS on the same Vulkan device. Each
native and translated-COM route checks 6,144 pixels against Windows D3D9.
The native test repeats preparations 64 times, observes only three unique
stages, rejects missing catalog membership, conflicting module/plan/map/mask,
and confirms the old strict registration APIs still reject duplicates. The
COM route creates both PS objects from original bytecode and alternates them
through 40 SDK-translated synthetic x86 callers, checking binding getters and
retained constant snapshots. A wrong-PS control must fail on pixels, along with
the existing UV/texture/constant controls. Native testing also checks removal
of the pixel-center correction.

The full backend regression and ARM64/libnx compilation cover the new API.
These comparisons run on desktop GPUs; they are not new Switch execution,
an engine frame, a performance measurement or a successful cold EXE boot.
Original bytecode, SPIR-V, generated headers and callers remain private.

```powershell
python pc/tests/test_prepared_program_sharing.py --first local/first-pair --second local/second-pair --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe --sdk ../sh2-nx/third_party/xboxrecomp --python-deps ../python-deps --output local/sharing-new
```

Both inputs must be accepted pixel fixtures. The optional SDK/dependency pair
enables the COM route; without them the test executes native Vulkan only.
