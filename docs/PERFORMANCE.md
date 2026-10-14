# Performance investigation

## Alpha13 hardware: conservative settings restored

The user reports the game feels slower. The app log appends alpha12 and alpha13
sessions, while the supplied profile is the alpha13 run. Only the last app
session (08:46:31 to 08:51:00 on October 14) is included in these observations.
25 non-startup intervals; no recorded fatal or Protect failures. Late FSI was
enabled and selected for some draws. Notifications still have progress=0 and
timeouts=0 with 3,463,680 fallback sleeps despite the changed physical address.
The software gate's unit tests do not prove that this hardware publisher matches
the guest wait. Leave the notification off until that contract is established;
do not broaden matches or manufacture progress.

The exact-image audit explains the mismatch: at 8279C468..8279C484 the game
configures CP_RB_RPTR writeback at device[10896]+60 bytes, while the audited
wait reads device[10896]+0. Function 8223FC48 emits EVENT_WRITE_SHD (opcode58)
with the completion sequence at the base word. Thus the notifier was attached
to the ring read index, not the waited completion word. Correcting the E/F
physical offset could not fix that semantic mismatch. A future notification
must follow the matching original completion store and retain the original
poll; waking is not a host GPU fence or permission to skip waits.

The run begins at CPU/GPU/RAM 1020/307.2/1331.2 MHz, then changes to
1785/960/1862.4 MHz. Light 13-draw intervals at the lower clocks sustain about
12 FPS. Later intervals at higher clocks range from 2.8 to 10.4 FPS in varied
gameplay, with one >800-draw interval at 7.6 FPS. This is not a matched-scene
comparison to alpha12's 23 heavy intervals at median 2.9 FPS: neither a steady
FPS gain nor a regression caused by late FSI is established.

There are 138 pipeline creation records and windows with zero or near-zero
frames. The cache identity and shader variants changed, so compilation can
contribute to loading stalls. Sparse GPU groups cannot explain an entire stall
by themselves. The worst sampled draw group is 66.4 ms, resolves 2.6 ms;
different workloads/clocks prevent comparing that maximum directly to alpha12.

Restore conservative INI defaults: late FSI=false, notification=false, sparse
GPU queries=false. This uses the existing alpha13 NRO, avoids extra diagnostic
query dependencies and keeps original polling/backoff, fences and barriers.
It is a reversible configuration change, not a verified FPS fix. Keep caches
and saves. FBO still needs color/depth/resolve diagnosis before adoption; do
not substitute FBO as the default or restart the paused PC route on this evidence.

Do not derive a fixed 30 ms frame floor from two different scenes. Their draw
counts, pass topology, clocks, shader compilation and waits differ. A linear
fit is a hypothesis for a matched workload, not proof that 30 FPS is impossible.
Summing one-in-16 submission samples is not a full-frame GPU busy percentage
without associating measured spans with frames. Fine pass timings, target
transitions and barrier counts are the next diagnostic evidence.

## Alpha12 hardware and alpha13 experiment

Alpha12 does not demonstrate a meaningful outdoor FPS improvement. Its latest
59 non-startup intervals include 23 with over 800 draws/frame: median 2.9 FPS.
One heavy interval is 2.6 FPS, 1,597 draws, 15 resolves and 3.3 submissions/frame.
There are no recorded fatal or Protect failures in this run. Saving outside
works according to the user; face/depth and save-thumbnail artifacts remain.

The notified wait failed to match the actual ring alias FFCA3000. The pinned SDK
maps its E/F heap with an extra physical 0x1000, so the canonical address is
1FCA4000, not 1FCA3000. Alpha12 recorded zero notified waits and 8,124,659
fallback sleeps. Alpha13 corrects this mapping and validates heap bounds,
wrap/reset/prepublished progress. Original PPC polls and the recovery timeout
are retained. Hardware must confirm notifications and CPU savings.

Sparse completed GPU samples show draw groups around 154-157 ms and resolves
around 2.7-3.0 ms in the heaviest sampled submissions. Draw groups include
uploads/barriers and ordering dependencies; these are not isolated pixel shader
times. Together with submission pressure this prioritizes FSI work/serialization
over rewriting matrix math or removing synchronization without evidence.
Depth observations had requested=active and alias_disabled=0: this excludes that
particular suppression in this run, but does not establish correct visibility.

Alpha13 adds fsi_interlock_tardio. For guarded opaque shaders, run shading before
entering the SDK's existing late-depth ordered interlock. Depth comparison,
depth/color writes, blending, fences and barriers remain. Reject shaders with
kill/depth export/memexport, vertex memexport, stencil or alpha-dependent coverage.
The shader modifier has a distinct value and cache identity version13. This can
reduce lock contention, but loses early depth rejection and may worsen overdraw.
It is an experiment, not a measured FPS or graphics fix.

Offline translation of 38 captured PS variants validates both deferred-kill and
native-demote FSI paths: 38 originals and 29 eligible late variants per path;
9 unsafe variants rejected. Validation also found missing alpha/coverage SSA
merge values in the SDK deferred-kill path. The preparation fixes the generator
with conditional Phi merges, preserving original statements and roundtripping;
no binary shader patching. NVK advertises demote: this deferred-path bug is not
evidence for the reported live face artifacts. GPU image equivalence is pending.

Compare the same saved outdoor camera/clocks/cache, first true then false, with
only fsi_interlock_tardio changed and the app restarted. Keep GPU diagnostic
queries identical in both runs, or disable tiempos_gpu in both for FPS-only
comparison. Cache version changes may cause initial compilation; measure once
the camera is stable. Keep copies of both logs directories before the next run.

## Alpha11 hardware and alpha12 changes

The user reports a modest interior improvement at 540p, but no useful outdoor
gain. The user subsequently confirms an outdoor save succeeds, and reports
face/texture artifacts and objects visible through walls. The former save
stall has no proven fix/cause; manual save success is a real hardware advance.
The graphics issues require checking FSI depth/stencil, render-target state
and the relevant texture/shader data, not attributing them to a particular
optimization without an image comparison. The 30 FPS menu used OC.
Alpha12 also counts requested/active/written depth states and the SDK's existing
depth-color alias suppression. It logs at most eight such state snapshots;
these observations do not change depth testing or prove the cause of the artifacts.
The creation guard accepted 960x540 and the
profile's dominant surface-pitch bucket moved below 1280, so this time the
creation size did change. It still does not reduce every target or every pass.

One outdoor interval reports 2.3 FPS, around 1,831 draws and 15.3 resolves per
frame. Inclusive submission time is about 385 ms/frame (885 ms/s), versus
textures 12 ms/s, shared ranges 45 ms/s, pipeline configuration 8 ms/s and
physical read requests below 1 ms/s. These are overlapping host measurements.
Native queue pressure reports nearly 2,000 outstanding entries even with one
submission in flight. This supports investigating queued GPU work/FSI and
backpressure; it does not identify a pure shader, driver or resolve cost.
The isolated 68 KiB sound read taking 10.4 ms can cause a hitch but does not
explain this interval's sustained 2–4 FPS.

Alpha12 replaces the scoped unchanged-progress backoff with an opt-in bounded
condition-variable wait. The SDK notifies after its two existing read-pointer
stores, both mid-buffer and at batch end. The hook only uses verified physical
alias addresses matching TranslatePhysical. Progress published before parking
is caught by the predicate under the same mutex; wrap uses inequality. Reset,
reconfiguration and shutdown invalidate the notification. A 250 µs timeout
or a notification only permits another original PPC poll: no guest state,
5000-unit recovery timeout or host GPU completion is manufactured. Unsupported
addresses retain the 50 µs fallback. Log counts are interval counts, not millions
of cumulative sleeps; requested and actual scheduling times may differ.

Optional primary-buffer batching keeps original submits at presentation,
readbacks and explicit synchronization, and flushes pending commands before
idle/WAIT_REG_MEM. All existing GPU barriers, fences and resource retirement
remain. It is off in the supplied INI until an equivalent-scene hardware test
checks its effect; bigger batches can also increase pressure rather than help.

GPU diagnostics sample one of every 16 submissions, using four fixed query
slots and at most 64 timestamp markers per sampled submission. Markers delimit
ordered draw groups, resolves and output. Only slots whose original SDK fence
has retired are queried, with no WAIT or partial-result flag; busy slots cause
sampling to be skipped. timestampValidBits, timestampPeriod and entry points
are checked. Disabled/unsupported measurement does not block rendering.
Spans include uploads, dependencies and barriers in each group, not just
fragment/vertex shader instructions. They sample a subset, so do not sum them
as if every submission/frame were measured. Markers themselves may affect timing.
This follows the [Vulkan timestamp contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp.html)
and [query result contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html).

The added primitive/index processing timer tests whether topology conversion,
index endian handling or cache work is a meaningful CPU cost before rewriting
it. Matrix/float/VMX/native memcpy work requires a hot function and original
result/memory-effect comparisons; global fast-math or arbitrary call-result
caching would change guest semantics. Generated PPC remains unchanged in alpha12.

Switch user settings now come only from [fnv-settings.ini](../config/fnv-settings.ini);
[SETTINGS.md](SETTINGS.md) documents FPS, renderer, resolution, host VSync,
notification/batching, affinity and diagnostics. Affinity defaults auto and does
not force priorities or core 3. The native FPS text uses the presenter's existing
color pass without registering a persistent ImGui drawer.

The latest [SA2 long-session change](https://github.com/ChanseyIsTheBest/sa2-nx/commit/3aff36cb663c1eaff10a2835e75a0f142da849b5)
retires unused resolved textures in its native renderer. That renderer is not
FNV's current Xenos FSI path, so the change was reviewed as a future resource
lifetime reference and not applied wholesale. The earlier CPU rounds are already
in the pinned source, but their SA2 codegen/profile data are not FNV codegen/PGO.

Alpha12 compiles and its host contract checks pass; its FPS improvement, native
counter appearance and grouped-submit fidelity still need Nintendo Switch.

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
