# Native shader packages and startup requirements

The native SDP reader loads a user's original shader package directly. It
provides immutable bytecode entries for `fnv_d3d9_com_config.catalog`; COM create
copies them, so the reader can be destroyed immediately afterwards. No files
are extracted or written into the user's game installation.

The reader validates the container version, exact payload length, bounded
record count, printable terminated names, bytecode alignment, stage/version
markers and final END tokens. Input plus object and descriptor storage must fit
the caller's budget. Packages are limited to 64 MiB and 8192 entries, matching
the COM catalog count limit. Duplicate bytecodes remain separate records.
This is structural validation, not an instruction decoder or shader compiler.
The reader accepts caller-selected trusted paths; path sandboxing belongs to
the caller. A package loaded successfully can still exceed COM's separate
per-shader bytecode limit.

`fnv_sdp_parse` copies input, and `fnv_sdp_load` reads an exact-sized file in
binary mode. Failure leaves the output NULL and frees temporary storage. The
caller must supply a new output slot and destroy any previous package first.
The empty package owns no descriptor allocation.

For the desktop captured-startup diagnostic, compile the host with both
`FNV_STARTUP_VULKAN` and `FNV_STARTUP_SDP` and link `native/d3d9_sdp.c`.
`FNV_STARTUP_SDP_PATH` selects one original package; it is loaded with a 16 MiB
budget before COM creation. Unset preserves the previous diagnostic behavior.
Loading bytecode does not prepare Vulkan programs or change reported caps.

## Validation

```powershell
python pc/tests/test_sdp_native.py --output local/sdp-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

Optionally add `--shaders` with the user's original `Data/Shaders` directory.
The test compares every bytecode's size and checksum against the independent
Python parser, checks lifetime after source-buffer poisoning, two synthetic
containers and 14 malformed-input/budget cases. It records only metadata in
its private report. All 16 installed packages passed (15,791 records). Strict
ARM64/libnx compilation of the reader also passed; execution on Switch has
not been tested.

The refreshed desktop captured-startup replay loaded 983 records into COM,
then stopped at the existing Hardware T&L gate with zero draws/submissions.
It uses captured startup code and an exact x87 result replay; it is not a
cold EXE boot, engine rendering or a hardware performance measurement.

The read-only `audit_d3d9_startup_requirements.py` recognizes a bounded x86
rejection pattern using caller-supplied exact EXE/capture hashes and an error
literal. Its synthetic test covers recognition and eight negative inputs.
It is not a general control-flow proof. Static thresholds may change at
runtime; a private observation of this startup also confirmed VS 1.1 and
PS 1.3 minimum thresholds, with advertised versions still zero. Hardware T&L
remains disabled. The next requirement is preparing the game's actual shaders
as linked SPIR-V programs with compatible layouts and runtime resources;
the successful fixed-pipeline fixtures do not establish that coverage.
