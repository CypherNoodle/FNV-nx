# Desktop engine presentation checkpoint

The translated PC engine has produced a final Vulkan image of the doctor's
room with the character-name dialog. This is more than an intermediate render
target: scene composition and the UI reached the final presentation surface.
This result was obtained in the private desktop startup harness, using the
base game without DLC plugins. It does not establish cold startup from the
on-disk EXE, execution of the engine on Switch, or playable performance.

## Why the earlier image was black

Readback traced the first black surface to the cinematic effect. Its incoming
brightness multiplier was zero before the D3D9 constant-upload call. The final
copy shader was copying an already black texture.

Further tracing identified an active black-screen modifier used by the
introduction. The introduction quest applies it and later removes it. In the
base-game run, the quest advanced through stages 90, 95 and 100; the brightness
returned to its base value and the room became visible. No brightness override
or shader bypass was introduced.

Earlier runs with DLC still had a package notification open in late captures.
They therefore did not demonstrate an unobstructed introduction reaching its
normal visible state. Automated dismissal and game-time advancement must be
verified before treating a black frame as a rendering failure.

## Isolating the final copy

`tests/test_screen_copy_gpu.py` accepts a locally prepared shader-pair fixture.
The fixture must implement the explicit XYZ/UV screen-copy contract: two
vertex inputs, geometry offset in vertex constant zero, UV offset in constant
one, one 2D sampler, and no pixel uniform block.

The test renders a synthetic four-color image and compares all 1,024 pixels
after Vulkan readback. A second run deliberately changes the UV offset and
must fail that comparison. Both controls passed on desktop. The test contains
no game shaders or assets; the external prepared fixture and generated header
remain private. This is not a Windows D3D9 differential oracle or a Switch test.

## Remaining limits

- Validate this engine path on Switch/NVK and connect normal presentation.
- Validate startup from the supported EXE identity rather than a captured state.
- Continue beyond character naming and verify rendering, input and audio.
- Investigate the intermittent native access violation seen in a separate run.
- Complete process cleanup: the diagnostic still reports cleanup result 2.

Desktop draw counts and these diagnostic wall times are not Switch FPS data.
