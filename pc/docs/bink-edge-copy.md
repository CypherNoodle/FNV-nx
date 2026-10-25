# Bink edge-tile copy compatibility

The game requests fixed-size video tiles, including tiles that extend beyond
its decoded image. `BinkCopyToBufferRect` clips the source rectangle to the
remaining decoded pixels. Rejecting that request prevents the intro from
progressing even though decoding and the destination format are supported.

`bink_copy.c` now clips width and height after checking that the source origin
lies inside the image. Destination pitch, accessible capacities, distinct
buffers and supported flags/formats remain validated before any write. Clipping
the source does not permit writes beyond the destination. Padding and the
unfilled portion of an edge tile remain untouched.

Verification uses the original x86 DLL with its sound system disabled. For
both packed 32-bit layouts, bottom and right edge requests are compared to
full-frame native output, including sentinel bytes in unfilled pixels and
padding. The fixture compares 1,048,576 additional edge bytes. Synthetic tests
exercise small pitched images and transactional rejection, and the implementation
compiles with devkitA64. The translated movie-import regression still produces
120 frames and identical video/PCM hashes to the independent decoder path.
No audio device is opened by these tests.

The private captured-code game diagnostic passes its former copy rejection and
continues presenting until its diagnostic deadline, with no worker failure.
Its final readback is still black; presenting and decoding are not evidence of
a visible cinematic or a completed engine boot. The rendering/skip transition
requires further investigation. No Switch performance or standalone NRO result
is implied by the desktop diagnostic.

With an actual Escape key snapshot through both DirectInput and User32, the
private game caller leaves the movie and reaches environment texture loading.
The next explicit stop is `D3DXCreateCubeTextureFromFileInMemory`. The current
COM/Vulkan implementation supports 2D textures, not six-face cube resources;
this requires real resource, upload, sampling and lifetime support. Returning a
2D placeholder or a success stub would not resolve that missing implementation.
