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

The renderer currently allocates constant buffers, descriptor objects, layouts,
samplers and a pipeline object for each draw, and submits very small batches.
Reuse must preserve resource versions and fence ownership. Constant-buffer
reuse per completed frame is the next candidate because console samples reached
GPU virtual-address allocation. Draw-rejection reasons need aggregated counters
to diagnose incomplete geometry without flooding the SD log. Pipeline/layout
caching and batching follow with pixel and lifetime regression checks.

Lower internal resolution remains pending; output and rendering dimensions must
be separated, rather than simply reporting a smaller mode to the game. Low GPU
utilization alone does not identify why it is idle. CPU preparation, rejected
draws and renderer allocation are plausible causes requiring separate measures.

The character-name prompt still needs a native Switch keyboard bridge into the
game's text-input path. A settings launcher that saves INI values is deferred
until rendering and performance are usable, as requested.

## Build storage

Keep stable `pc-engine-current-build`, `pc-engine-active` and `pc-engine-baseline`
directories. The active NRO/ELF pair must survive a failed replacement build.
`--replace-output` may clear only the dedicated current-build directory and
first verifies the preserved active NRO and ELF hashes. It does not operate on
the game installation or arbitrary output paths. Old duplicate build directories
and ZIPs were removed. Continue using the same NRO/log names; hashes identify
the exact binary. The startup log reports `SoftFloat state=thread-local` for
the optimized variant, making it distinguishable without revision proliferation.
