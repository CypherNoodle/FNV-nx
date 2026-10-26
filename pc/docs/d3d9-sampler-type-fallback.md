# Null and mismatched texture dimensions

The Windows D3D9 HAL oracle uses own shaders in a hidden 16x16 window, without
audio or game data. It tests both sampler2D and samplerCUBE against a valid 2D
texture, a valid cube texture and NULL. Correctly typed controls must produce
the expected solid colors before the observations are accepted.

On the tested Windows driver, both mismatched combinations accepted SetTexture
and DrawPrimitiveUP and sampled `(0,0,0,0)`. NULL sampled `(0,0,0,1)`. Rejecting
the entire draw on a dimension mismatch therefore differed from these measured
results.
This is a measured HAL behavior, not a claim about every D3D9 driver or mode.

The Vulkan backend retains the original bound texture identity and selects an
internal image with the dimension required by that program. Its texels are
transparent black for a mismatch and opaque black for NULL. The images are
created lazily, reused across draws, and released with the backend. Invalid
nonzero resource identities still fail. The fallback does not expose a guest
resource or modify the texture the game bound.

`test_d3d9_sampler_type_oracle.py` records the six real Windows cases and their
pixels. `test_vulkan_cube.py` checks 2,816 pixels across eleven Vulkan draws:
six cube axes, opaque NULL in both dimensions, transparent mismatches in both
directions, and a correctly bound 2D control after using fallbacks. It also
retains the sampler-shape parser's negative cases and resource capacity probe.
These desktop checks do not establish rendering or performance on Switch.
