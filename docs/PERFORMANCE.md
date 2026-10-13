# Performance investigation

The video-mode-only 960x540 trial did not establish a smaller internal workload.
The captured profile still has most draws in the 1280–1599 surface-pitch bucket.
In the original renderer bootstrap, `sub_8230F290` calls XGetVideoMode and then
forces 1280x720 when widescreen is set. Alpha11 changes the audited creation
arguments before resources are allocated. No dynamic resolution is implemented.

## Alpha10 findings and alpha11 optimization

The hardware run contains 67 non-startup reporting intervals. The guest/host
elapsed-time ratio stays between 0.999965 and 1.000144: it does not support a
clock-scaling explanation for the sustained low FPS. Vblank callbacks continue.
Recorded protection failures remain zero. The 791 repeated reservation errors
occur in a four-millisecond startup burst, not during the later save wait.
Their original diagnostic lacks ranges/callers; alpha11 adds those fields while
keeping the failure result.

In a slow gameplay interval, host pipeline configuration costs about 1 ms/s,
textures 6 ms/s, shared ranges 21 ms/s and memory protection 1 ms/s. Submission
cost is about 760 ms/s, with frequent native queue pressure, and the guest GPU
wait about 815 ms/s. These inclusive rows overlap; they identify the submission
and GPU dependency path as the first target, not additive CPU work to remove.
Timer overhead and scene differences prevent treating this run as a benchmark.

`config/switch-render-540p.toml` enables `fnv_fixed_render_mode`. Only the creation
call with LR `0x8230F3F0`, original 1280x720 arguments and matching globals can
change to aligned 960x540. The hook updates globals `0x82FFF304/308`, passes the
new arguments to original `sub_827B5D98`, and restores those globals if creation
returns zero. `tools/audit_render_mode.py` checks the original image identity,
the forwarding to `sub_827AF0F8` and stores to presentation parameters width/height.
The guard rejects unrelated calls, invalid aspect ratios or alignment, mismatched
globals and out-of-range requests. This is a fixed boot experiment, not a per-frame
change or a fake video-mode response. The 720p preset disables the hook and keeps
the same renderer, clocks, caches and diagnostic settings.

960x540 has 43.75% fewer pixels than 1280x720. That is a requested reduction for
the resized surfaces, not a verified 43.75% reduction in all GPU work or a promised
FPS gain. Check actual surface-pitch buckets, image/HUD/menus and comparable frame
times on hardware. Other targets or fixed-size post-processing may remain large.

The save stall has two final zero-frame intervals, main CPU near zero and the
GPU command thread idle. Audio and vblank continue. The last recorded operation
opens an existing save; no subsequent write or close is recorded. That is evidence
of a distinct stopped path, not proof of SD failure or a particular deadlock.
Alpha11 traces original container/stream-wait entry and return and Savegame.dat
read entry/return. After two zero-frame intervals with idle main and command
threads, it captures stacks for 30 seconds once per launch. It never signals the
guest wait or manufactures a successful result. Existing call timers only report
after a call returns; the new trace can locate a call that remains blocked.

The alpha10 per-core API readings were zero even for threads with nonzero total
CPU time. They are inconsistent and must not be used to infer idle cores. Alpha11
marks that case unavailable and retains total thread CPU/affinity evidence.
GPU timestamps per pass, native FBO fidelity and renderer-native deko3d integration
remain further work after this measured submission/pixel-cost experiment.

## Alpha10 call and dependency profile

Use `config/switch-call-profile.toml` with the alpha10 diagnostic NRO. It retains
alpha09's FSI renderer, 720p, caches and scoped polling backoff. It changes
measurement, not the synchronization contract. Replacing it with the alpha09
recovery config disables these timers on restart.

`fnv_call_profile` records inclusive host wall time with the ARM system counter:

- Whole draw, resolve/copy, begin/end submission and PM4 buffer processing.
- Blocking submission fence waits separately from nonblocking fence queries.
- Texture requests, shared-memory residency/upload work, pipeline configuration
  and render-target updates.
- Sampler reuse, resolve readback and both memexport readback paths.
- PM4 register/memory waits, interrupt packet processing and callback execution.
- GPU command thread waiting for new ring work, guest GPU-wait function
  `0x8224AC98`, actual polling-backoff sleep and memory protection calls.
- Swap and vblank callbacks, guest file reads/writes, and execution of each
  registered thread on each core where Horizon exposes the per-core counters.

Per-core reports are actual thread execution counters, not the allowed affinity
mask. Unsupported queries print their error rather than zero usage. A thread's
first report establishes its baseline. The bounded tracking table can fill;
it reports that condition. Handles reused during a long session may establish
an imperfect baseline; do not treat that single interval as an affinity result.
Existing audio telemetry includes output/nonzero buffers, requests without data,
clipping, peaks and XMA lock/decode timings. Existing filesystem telemetry
separates request time, bytes, worst reads, RAM windows and range-cache activity.
The new whole guest-file-call timer includes kernel preparation and completion,
not just physical SD access; compare both before attributing a delay to the card.

FSI and FBO require equivalent visible scenes for a useful comparison. FBO
previously omitted the world; its lower work cannot count as a performance gain.
Keep FSI in the diagnostic run and record the actual selected path. The vblank
counter and callback counts help detect pacing dependencies without changing
the current time-driven vblank policy.

The guest wrapper calls the original `__imp__sub_8224AC98` and preserves its
results. SDK preparation inserts timer scopes and reports only; removing those
insertions must recover every previous statement. The preparation checks this
round trip and is idempotent. It preserves existing waits, deadlines, barriers,
callbacks, protection ranges and read-pointer writebacks.

The report includes counts, calls/frame, ms/frame, ms/second, mean and maximum.
Nested rows overlap and parallel threads overlap. Do not add them into a frame
budget. Counters exchanged at report boundaries are approximate, and calls that
span a boundary are attributed when they return. These are **not GPU pass
timestamps**. A long fence wait can indicate GPU work, dependency serialization
or queued work; it does not identify the expensive shader or pass by itself.

`fnv_profile_lowfps=true` also enables one bounded 60-second stack-sampling
interval if FPS is below 15 and the main thread exceeds 60% of one core. Sampling
temporarily suspends threads and changes timing; this run is for diagnosis, not
an FPS benchmark. Symbolize PCs against the ELF from this exact build.

The previous profile has no samples. Its startup report has a near-zero time
divisor and absurd per-second counters; exclude it. In later intervals fence
wait totals approach a second per second, while presenter and driver submission
CPU costs remain small. This narrows the investigation but does not establish
missing interrupts, a memory leak or a single GPU bottleneck. The process-wide
3185 MB accounting includes mappings; it is not proof of exhausted live assets.
The observed process core mask is `0x7`: three available cores, not four freely
assignable application cores. Main and GPU command threads already permit all
three. Do not force exclusive affinity or raise every worker's priority.

## Optimizations selected by evidence

The runtime sets the guest timebase to 50 MHz, while the Horizon clock source
uses `armGetSystemTick()` paired with `armGetSystemTickFreq()`. Kernel relative
delays and system time use 100 ns units, a separate unit from the timebase.
Alpha10 reports observed guest elapsed seconds divided by host elapsed seconds,
the configured scalar and clock flags. A ratio near one supports correct scaling
for that interval; it does not prove every game's timer or pacing loop correct.
The default vblank path is time-driven; `vblank_per_swap` defaults false.

The exception handler uses explicit resume paths rather than C++ scope exits;
do not place a destructor-based timer around it and assume it will run.
The latest log has zero protection failures and only a small number of recorded
handler calls per second. This does not rule out rare faults, but it does not
support blaming that handler for the sustained low FPS. Keep original fatal
diagnostics and the exact one-fault store authorization from alpha07.

- High ring-idle time plus guest polling: inspect producer/consumer progress and
  notification timing. The SDK already publishes read-pointer progress while
  consuming the ring. Preserve the original progress and timeout semantics.
- High texture/shared-memory CPU time: inspect dirty-range tracking, redundant
  uploads, endian/format conversion and reads from uncached upload memory.
- High pipeline configuration time: distinguish first compilation from cache
  lookup and check actual cache compatibility before changing cache policy.
- High fence/readback time: inspect the precise caller and dependency. Reduce
  avoidable full waits only when completion and resource lifetimes remain proven.
- Low host preparation cost with long GPU waits: add supported GPU timestamps
  around ordered passes to separate FSI rasterization, resolves and copies.
- Hot guest CPU functions: select original pure operations for native rewrites,
  then compare results and memory effects against the original implementation.

Reuse is valid for unchanged descriptors, pipelines and immutable resources;
it is not valid for arbitrary repeated calls with memory or synchronization
effects. Never replace a poll with a fabricated completion.

## References supplied during this investigation

[Futari's notes](https://github.com/ChanseyIsTheBest/futari-nx/blob/main/docs/futari-notes.md)
describe a matching XDK GPU-wait layout (`device+10896`, `state+8`) and a native
ring-progress notification. They also describe cached scratch memory for vertex
conversion, removal of repeated diagnostic formatting, and verified native
rewrites. These are candidate mechanisms, not measurements of FNV. The FNV
original poll uses the same offsets; its renderer and callers still need their
own checks. No Futari code or tuning is copied here.

[Halo CE](https://github.com/thelinkin3000/halo-ce-universal) uses a decompiled
original-Xbox D3D8 game and a Switch deko3d renderer. It provides renderer design
references, not a Xenos replacement. [MarathonRecomp-NX](https://github.com/NaGaa95/MarathonRecomp-NX)
has Switch-specific pre-generated PPC/shader build paths; its root README still
describes the upstream PC project. The name alone is not evidence of FNV support
or a performance result. [Mesa's Horizon backend](https://github.com/danfromtico/mesa-switch)
centralizes submission, cache policies and native fences. Changing its revision
should follow a measured driver issue, not replace the current controlled run.

XenosRecomp, ReXGlue and nfsmw-nx remain existing architecture references. Their
presence does not establish identical XDK functions or identical memory layouts.
