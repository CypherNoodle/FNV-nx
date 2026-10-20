# Private captured-CRT hardware diagnostic

This diagnostic is separate from the renderer tests. It executes compiled game
CRT code with native Windows bridges and deliberately stops before the engine.
It produces no game graphics or audio and establishes no cold entry from disk.

`build_switch_crt_probe.py` requires an accepted complete Switch CPU build, a
capture-consistent desktop experiment and a matching initializer observation.
The observation must cover every expected nonzero constructor, preserve caller
state and stack movement, and retain identical control flow. An early jump to
the final marker is rejected. A packaging result is not sufficient evidence of startup: an incomplete
traversal must prevent delivery even if compilation and linking succeeded.

The observation records every before/after call row. Packaging rereads the
identity-checked original table and verifies each exact slot and callback in
order, as well as caller state on return. Expected calls are the nonzero slot
count, not the number of unique targets: repeated constructors must execute
once for each listed slot. Counts and monotonically increasing cursors alone
cannot prove full traversal. Older observations without the complete trace
are rejected. Synthetic regression checks cover repeated targets, skipped or
substituted slots, wrong callbacks, state changes and mismatched table bytes.

The builder relinks the original verified CPU objects with a deliberate boundary
wrapper. It checks that the audited CRT caller refers to a separately defined
engine symbol through an external call relocation. Wrapping an already combined
relocatable object would not establish that interception. Native imports remain
real bridges or explicit faults. No guessed-success stubs are introduced.

The resulting NRO contains generated game code and a private ROMFS text capture
needed for coherent inline tables. **Do not publish generated NROs, ROMFS images,
captures or profiles.** Only the original reusable application/tool sources belong
in the repository. Builds pin input hashes, inspect final ELF symbols and TLS
dependencies, validate NRO segments/assets and compare the embedded ROMFS bytes.

After a validated build is available, the intended local setup is
`sdmc:/switch/FNV-nx/fnv-nx.nro` beside the user's matching `FalloutNV.exe`.
The capture is embedded privately; no separate assets directory is required for
this CRT diagnostic. A runs once, + exits, and the log is
`sdmc:/switch/FNV-nx/fnv-pc-crt-hw01.log`. The application validates both identities
before execution. A successful result means reaching the deliberate boundary,
not running the engine or a playable game.

For an extended-x87 CPU build, the packager also requires `--x87-profile` and
`--x87-core-validation`. Their model, flags and core-proof digest must match
the accepted complete CPU build. The separately compiled 99/100/101-object
core is included in the original-object stopped link; omitting it would leave
the extended backend unresolved. Legacy builds cannot select a core implicitly.
The same source/object identities are checked again before delivery.

`test_crt_x87_link_inputs.py` tests this selector with all three real
Switch-ABI cores, 23 incompatible-input cases, and an actual relocatable link
of a synthetic SDK-generated unit. A control link without the core reproduces
the missing backend symbols. This validates only object selection and linking:
it neither bypasses initializer observation nor proves a final NRO or engine
startup.
