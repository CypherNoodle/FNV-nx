# HW12 menu stop and slow startup

Both user sessions use the PC experiment; the Xbox route remains paused.
HW12 reaches the real menu, plays music and starts the opening movie. This is
hardware evidence of those paths, not evidence that gameplay loads.

## Confirmed stop

The first session ends at about 430 seconds with an explicit unimplemented
`KERNEL32.dll!WritePrivateProfileStringA` import, not a captured native exception.
HW13 binds that import to the existing native profile provider. ANSI writes
replace/create a key or delete a key/section. Existing unrelated lines survive.
Writes must use the existing virtual user-profile path, mapped beneath
`UserData/FNV` inside the port folder. Game-root assets remain read-only.
The file provider reserves the canonical path exclusively during read/modify/write;
no file-table or kernel mutex is held across filesystem IO. Failures return FALSE
and an error; no success stub, fsync per setting, or host Windows-directory access.

The implementation supports ASCII strings, at most 1 MiB of input and bounded
lines. Unsupported encodings are rejected before rewriting existing content.
Updates use checked seek/write/flush/truncate/close; they are not transactional
against power loss. All-NULL cache invalidation returns zero, as observed on
Windows; there is no delayed profile cache here.

## Performance evidence and changes

In the first session, cumulative reads take about 9.8 seconds over 430 seconds,
with about 4 milliseconds of file-lock wait and no IO errors. SD time alone does
not explain several minutes of startup. The main guest thread reaches roughly
84 million native dispatches, repeatedly including thread identity/refcount calls.

Reserved native tokens now use the manual dispatch hook before the generated
function-table search. Ordinary game addresses keep the same lookup path. Valid
native calls still pass the kernel peer-fault check and native observer; invalid
or unbound tokens keep their original failure path. The generated code and SDK
are unchanged. This removes redundant searches, not the native API operation.

Graphics pipelines remain transient per draw, but now share one `VkPipelineCache`
per backend/device. It survives draws and Reset and is destroyed with the device.
The renderer mutex provides cache external synchronization. This is an in-memory
compiler cache, not a disk cache or a cache of live pipeline objects. It cannot
remove all pipeline/layout allocation or GPU rendering cost.

HW13 logs cumulative `Pipelines calls=... host_ms=... max_host_ms=... cache=device`.
These measure host time inside pipeline creation, not GPU timestamps. Snapshot
acquisition uses the renderer mutex and can itself wait behind a long driver call.
Do not infer GPU idle time or deadlock solely from missing Progress lines.

The second session slows to about eight presents per second during the movie,
then has a long gap before its last progress line around 458 seconds. Neither
crash file captures a native exception. The reason gameplay never completes after
skipping the movie remains unproven. Three busy application cores do not identify
a hot function; the fourth core is normally reserved for system work on Switch.

## Verification and next hardware run

- Synthetic translated x86 native calls: 1,000 concurrent calls, 12 fault cases,
  peer-stop behavior, manual-token validation and ordinary-address fallback pass.
- Real Windows profile oracle: 4,800 integer and 51 string-edge comparisons;
  seven writes/deletions, 105 subsequent queries, create/no-final-newline/flush,
  malformed-file preservation and confined-path negatives pass.
- Shared renderer probe: ten groups, eleven draws, 11,264 checked pixels, WSI
  presentation and wrong-render-source negative control pass on desktop Vulkan.
  Cache creation failure/rollback and handle lifetimes are injected and checked;
  every proxied graphics pipeline call requires a non-null cache. Metrics are
  nonzero and bounded by total host time. These are not Switch performance results.
- The older monolithic Vulkan fixture still expects a NULL sampled texture to be
  rejected. Current renderer intentionally supports NULL textures, so that fixture
  stops at its stale expectation. It is not counted as a passing full suite.

Install only the HW13 NRO, keeping EXE, Data and UserData. First retry the menu
option that previously stopped while saving preferences. Then start a new game
and skip the opening movie. Return both normal/crash logs and approximate menu
startup time. Keep clocks comparable to HW12. No FPS improvement is claimed
until that hardware comparison; gameplay loading is still an open investigation.
