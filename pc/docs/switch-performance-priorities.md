# Switch performance work after reaching the tutorial

The console reaches the character-name prompt. Its screenshot shows incomplete
geometry/materials and low GPU utilization. The logs also contain rejected
DrawIndexedPrimitive calls, so missing surfaces cannot all be attributed to
missing texture files. Loading completion does not establish usable performance.

## First implemented optimization: private SoftFloat state

Live GDB samples during the Ranger/menu wait reached engine work, TLS helpers,
and the shared x87 mutex. During the name prompt, samples also reached
`nouveau_horizon_va_create` through the GPU address-space allocation ioctl.
These are a small, manually timed set of samples, not percentages of CPU time.
They identify concrete work to investigate; they do not prove a single cause.

SoftFloat already supports a `THREAD_LOCAL` compile-time configuration. The
new whole-core option `build_x87_extended.py --thread-local-state` compiles all
SoftFloat objects and the own wrapper with native-thread state. It keeps the
extended arithmetic, precision controls and rounding behavior, and removes the
wrapper's shared lock only in that explicitly selected build. It does not edit
upstream SoftFloat or substitute binary32/binary64 arithmetic.

Validation checks 200,000 concurrent cases, distinct per-thread state and fresh
thread defaults, four real Horizon ELF TLS symbols, absence of the shared lock,
and rejection of a deliberately mixed global/TLS object. Existing host x87
oracles additionally pass 29,016 comparison/control cases, 12,000 arithmetic
cases, 11,088 integer cases, and 40,000 concurrent arithmetic calls. These are
correctness results on the host; console FPS improvement remains unmeasured.

The artifact admission path requires that validation, unchanged arithmetic
sources, and the same core compiler flags except the two private-state defines.
Every changed core object is replaced together. Do not mix old and new objects.

## Next measured targets

### Implemented after the private-state console run

The backend now uses a bounded-probe identity index instead of a 128-slot
direct-mapped cache. Hits still validate the live identity and resource kind;
deletion leaves a reusable tombstone, and cache saturation falls back to the
original array lookup. The index has twice the configured resource capacity.
For the 4,096-resource engine configuration it takes 256 KiB. Synthetic collision
traffic falls from 268,566,528 array steps in the previous algorithm to 2,098,176
steps after warm-up. This is a workload-specific algorithm check, not a console
FPS measurement. Randomized churn, wrong-kind and unknown-ID cases are checked
against a linear reference.

Optional per-frame uniform reuse retains mapped constant buffers after their
draw's fence completes. Only a nonpending frame with a sole owner reference can
be overwritten. Draws retain separate references, noncoherent writes are flushed,
and replacement buffers are installed only after successful submission. Build
failure frees new buffers; an unknown submission remains quarantined. Disable
returns BUSY during a scene or pending work, then frees retained owners when
safe. The engine enables this mode. The desktop Vulkan fixture compares the
same pixels with reuse enabled, injects a reused-buffer flush failure and checks
pending-disable rejection and complete teardown. GPU shaders and draw ordering
are unchanged; descriptors, layouts, samplers and pipeline objects are still
created per draw.

Inaudible PCM voices advance their cursor in bulk without fetching/interpolating
samples. End, loop, frequency, focus and output-queue rollback semantics remain.
The bulk advancement matches 200,000 randomized scalar cases. The DirectSound
oracle suite compares 204,958 PCM samples and checks spatial/listener state.

The worker policy already grants the process's allowed core mask before starting
each thread. Audio already blocks on audout events. No forced core-3 affinity,
priority boost, removed wait, or floating-point approximation is introduced.

### Measurement lines in the existing hw18 log

Every ten seconds the engine reports cumulative counters. Compare deltas between
actual timestamps, including when a renderer snapshot was busy:

| Line | Meaning |
| --- | --- |
| `Reuse` | Resource lookups/hits/array steps; new uniform versions and reused writes |
| `Draw` | Attempts, INVALIDCALL, NOTAVAILABLE and BUSY outcomes |
| `HostPhases` | Exclusive wall-clock brackets for draw preparation, geometry, texture, allocation, shader/pipeline, fences, submit, acquire and present |
| `Audio` | Pumps, committed frames, full returns, mutex wait, mix wall time, maximum pump time, budget overruns and skipped inaudible voice frames |
| `Audout` | Refills, missing source frames filled with silence, output-event timeouts |
| `CPU` | Guest-main thread ticks on each allowed core, affinity and query result |
| `IO` | Existing read bytes/time, I/O serialization waits and errors |

`HostPhases` measures instrumented host intervals, not GPU timestamps or CPU
utilization. Idle also includes time outside those brackets. A pump exceeding
its frames/48,000 time budget is not proof of an audible underrun; compare
`Audout`. Silent-voice counts include attempted pumps that later roll back.
CPU ticks describe the main native thread only, not the sum of every worker.
Failed kernel queries are logged; they must not be interpreted as 0% CPU.
`summarize_engine_log.py` preserves these samples and derives main-thread per-core
percentages only from successful, monotonic queries at matching tick rates.
Debugger pauses and scene changes still invalidate a simple performance A/B.

Native delta admission pins the old objects, original compile commands, all
visible header inputs and new object hashes. It replaces only three explicitly
listed own native units. The original recovered NRO remains the baseline; this
does not claim a fresh historical source audit. If admitted headers differ from
an old tail-unit receipt, the tail object must reproduce byte-for-byte with the
current headers before it can be reused.

The renderer previously allocated constant buffers, descriptor objects, layouts,
samplers and a pipeline object for each draw, and submits very small batches.
Reuse preserves resource versions and fence ownership. Constant-buffer reuse
per completed frame is now implemented because console samples reached GPU
virtual-address allocation. Draw outcomes now have aggregated counters; finer
rejection-site classification remains useful for incomplete geometry. Pipeline/layout
caching and batching follow with pixel and lifetime regression checks.

Lower internal resolution remains pending; output and rendering dimensions must
be separated, rather than simply reporting a smaller mode to the game. Low GPU
utilization alone does not identify why it is idle. CPU preparation, rejected
draws and renderer allocation are plausible causes requiring separate measures.

The character-name prompt still needs a native Switch keyboard bridge into the
game's text-input path. A settings launcher that saves INI values is deferred
until rendering and performance are usable, as requested.

### Related port references

The Alice thread supplied [Ketsui's synchronization notes](https://github.com/ChanseyIsTheBest/ketsui-nx/blob/main/docs/ketsui-notes.md)
and [Feeding Frenzy 2's function optimization workflow](https://github.com/ChanseyIsTheBest/feedingfrenzy2-nx/blob/main/docs/optimising-functions.md).
Ketsui's empty-kick fence completion is specific to its Xbox command/callback
queue. It is not permission to complete an FNV Vulkan fence early. The useful
general rule is to avoid redundant work only when ordering and completion are
proved. The CPU workflow similarly calls for console profiles, exact observable
semantics and subsequent measurement before selecting or keeping hot rewrites.
No PowerPC hook addresses or guest-register transformations from those ports
are applied to the PC x86 executable.

## Build storage

Keep stable `pc-engine-current-build`, `pc-engine-active` and `pc-engine-baseline`
directories. The active NRO/ELF pair must survive a failed replacement build.
`--replace-output` may clear only the dedicated current-build directory and
first verifies the preserved active NRO and ELF hashes. It does not operate on
the game installation or arbitrary output paths. Old duplicate build directories
and ZIPs were removed. Continue using the same NRO/log names; hashes identify
the exact binary. The startup log reports `SoftFloat state=thread-local` for
the optimized variant, making it distinguishable without revision proliferation.
