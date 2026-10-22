# Draw backpressure with multiple dirty textures

A two-sampler original shader exposed a real queue-pressure failure. Each dirty
texture schedules an upload and occupies a frame slot until its fence is retired.
With two slots and two uploads, the following draw had no slot and returned
WASSTILLDRAWING/BUSY. The transfers were valid; this was missing draw-side
backpressure handling rather than a shader translation failure.

`fnv_vk_backend_draw_wait` enables a finite watchdog before the first submission.
Zero preserves the old nonblocking behavior. With a nonzero timeout, a draw
waits for any pending frame fence only when all slots are occupied, retires
observed completions, and proceeds with uploads, queued clears and its draw.
It shares one deadline from its first pressure wait, using the host monotonic
clock. There is no sleep loop, per-draw idle, CPU texture readback or change to
guest game timers. Buffered, fixed and UP draws share this path.

Expiry or a clock failure marks the device lost. Pending GPU resources stay
retained until normal safe teardown, never recycled on the assumption that a
timeout implies completion. UINT64_MAX is rejected and the setting cannot be
changed after submissions start. The captured-startup host enables ten seconds;
the backend default remains nonblocking for existing diagnostic callers.
`fnv_vk_stats.draw_waits` counts pressure waits so later hardware profiles can
distinguish them from ordinary draws. This is not a measured Switch FPS gain.

The existing WSI presenter now uses the same internal wait-for-frame helper;
its waiting policy is otherwise unchanged. Draw waiting is an initialization
choice, not an INI or runtime optimization toggle yet.

## Two-sampler validation

The pixel fixture's `--two-textures --constants` mode installs an explicit pair
with sampler0 and sampler1, different BGRA textures, wrap versus clamp addressing
and UVs extending beyond the unit interval. Both textures change each frame.
The own HLSL program and an original fog-compositing PS paired with an original
matrix VS each pass 6,144 pixel comparisons against Windows D3D9. Neither this
pairing nor the synthetic resources are claimed to be the game's actual draw.

Swapped textures and an incorrect sampler1 address mode are detected. Further
controls exercise nonblocking BUSY plus retry, and force VK_TIMEOUT specifically
in the draw's wait-any fence call. The timeout test observes zero successful
draws and both GPU uploads still pending, then performs safe teardown. Normal
frames observe at least six pressure waits over the six-frame test.

The same original pair passes 6,144 pixels through 42 SDK-translated synthetic
x86 COM callers. Resources, locks, both texture bindings, sampler Set/Get,
constants and draws traverse COM. Sampler1 is unbound after draw submission,
before readback; the submitted draw retains its texture. Native calls are used
only to initialize the provider and for explicit diagnostic waits/readback.

The full regression retains nonblocking BUSY tests, injected failures and WSI
presentation checks. ARM64/libnx compilation also covers the host clock and
new wait path. This adds no engine frame, cold EXE boot, NRO or Switch execution.

```powershell
python pc/tests/test_catalog_pair_gpu.py --two-textures --constants --output local/two-textures-new --linker local/shader-describe-build01/link_d3d9_spirv.exe --headers ../rexglue-upstream/thirdparty/vulkan-headers/include --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

For the original pair supply the existing exact package/VS/PS hashes. Feed its
passing fixture to `test_catalog_pair_com.py` for the translated route. Two
textures and mip-clamp fixture modes are deliberately separate. All original
bytecode, SPIR-V and generated callers remain in private local output.
