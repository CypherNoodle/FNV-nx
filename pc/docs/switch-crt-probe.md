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
