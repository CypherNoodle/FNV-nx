# Bink decoder and initial sound-selection ABI

Components now exist for an own FFmpeg adapter that returns RGBA frames and
stereo 48 kHz signed-16 PCM, an own translated Bink sound-selection bridge,
and a bounded movie PCM input on the existing DirectSound mixer.
They are not yet a complete movie player in the game. The remaining work is the
default audio transport, seven movie exports, the guest handle, pacing and frame
copying into the renderer's locked texture. No new NRO is delivered by this work.

## External FFmpeg build

`tools/build_bink_ffmpeg.py` builds external sources out of tree and records the
tracked source hashes, commit, compiler, configuration and library hashes. The
tested official FFmpeg commit is `3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587`
(release 7.1.5). FFmpeg sources are not incorporated into this repository.
The profile enables only the Bink demuxer/video/audio decoders and supporting
libraries, including swscale/swresample. Network access, protocols, applications,
GPL/nonfree components and runtime source downloads are disabled. The resulting
external build reports LGPL 2.1-or-later; its license/source obligations remain
applicable when distributing a linked binary. This commit distributes own source
adapters, not FFmpeg libraries, the game DLL, media or a linked NRO.

The first profile uses portable C with ASM and FFmpeg threading disabled. All
FFmpeg access must therefore remain on one serialized decoder thread, including
open/close; it is not a general concurrent FFmpeg service. This is a correctness
baseline, not the final A57 optimization profile or a hardware speed claim.

Both Windows static libraries and actual Switch static libraries build without
modifying the external sources. The Switch compiler and linker must both receive
the libnx software-TLS/PIE options (`-mtp=soft -fPIE`); compiler-only flags are
insufficient. The build process supplies DEVKITPRO only to its child processes.
`--resume` requires exactly the same recorded configuration before reusing it.

Example, with an existing external checkout and a fresh private output folder:

```powershell
python pc/tools/build_bink_ffmpeg.py --source PATH_TO_FFMPEG --output local/bink-build-new --git PATH_TO_GIT --bash C:/devkitPro/msys2/usr/bin/bash.exe --devkitpro C:/devkitPro --target switch --jobs 4
```

## Own decoder adapter

`native/bink_decoder.c` uses custom read/seek callbacks; it never interprets a
guest FILE pointer or opens arbitrary paths. Caller-owned I/O remains alive until
close. The adapter accepts Bink video up to 1920x1080, a bounded frame count and
the first mono/stereo audio track. It uses one codec thread, bounded PCM scratch,
RGBA conversion and explicit decoder/resampler draining at EOF. Errors poison
that decoder instead of returning a fabricated frame. Frame storage belongs to
the decoder and expires on the next call or close. Audio is returned to the caller,
not automatically played or sent to DirectSound/audout.

The actual 1280x720 introduction produced 120 RGBA frames and 226,560 stereo
48 kHz PCM frames in a desktop read-only test. The source contains an initial
audio prebuffer: an independent packet-size walk confirms that sample count;
it is not an inferred four-second audio duration. Four negative tests cover
invalid input, immediate/read-stage I/O errors and dimensions exceeding the
adapter profile. A private two-frame container preserves original compressed
packets to exercise EOF/draining: two video frames and 38,400 PCM frames, matching
the declared packet lengths. Repeated EOF is checked. No decoded asset is exported.

`tools/build_bink_decoder_switch.py` verifies the external proof, compiles the
adapter/probe and links a private ARM64/libnx smoke ELF. Final linking has no
unresolved strong symbols or TEXTREL. It does not run the decoder on Switch,
package an NRO, display frames or establish game performance.

## Movie PCM mixer input

`fnv_dsound8_movie_write` copies stereo S16/48 kHz PCM into a dedicated,
48,000-frame ring owned by the existing DirectSound provider. Writes are atomic:
FULL preserves all input for retry. Mixing adds movie and game voices before
saturation. The output queue accepting a block is the only operation that
consumes movie frames; output FULL rolls back game cursors and retains the movie
cursor. No allocation or guest calls occur on the audio producer thread.

Pause and lack of application focus retain movie samples. Reset clears pending
movie data and the committed-frame counter, but cannot retract audio already in
the device output queue. `played` measures committed frames, not audible device
position; a future movie clock must account for output/device latency. Movie
control is serialized by its host owner, and audio workers stop before provider
destruction. One host movie stream is supported, independently of DirectSound
buffer volume/pan. The Bink controller has not yet connected decoder events to it.

The combined DirectSound test compares 204,606 PCM samples, plus simultaneous
movie/game saturation, a full output queue, atomic movie FULL, pause/focus,
two ring traversals including split writes, underrun and reset. Existing native
Windows ABI/listener checks and six explicit invalid-operation stops still pass.
The mixer compiles with the actual ARM64/libnx extended-x87 ABI. These are host
tests and cross-compilation, not Switch playback or performance measurements.

## Sound-selection bridge

`native/bink_sound.c` supplies `_BinkSetSoundSystem@8` and
`_BinkOpenDirectSound@4`. The former invokes the guest stdcall factory immediately,
using a validated callback stack with no host lock held. A nonzero returned
entry replaces the selected system; zero retains the previous successful
selection. The return reflects whether a successful selection remains. A NULL
factory clears it and returns zero. The original DLL confirms this behavior.

OpenDirectSound calls a configured host transport factory and returns its guest
entry, never a host pointer. A missing transport stops explicitly. The callback
result is not advertised as successful decoding/playback. Concurrent or recursive
selection is outside the first profile and stops explicitly. Setup occurs before
attach; native registration enforces destruction only after all threads detach.

The translated test covers 2,064 callback selections, state retention, parameter
forwarding, LastError/stack behavior, unavailable transport, invalid callbacks,
short stack, recursion and incorrect stdcall cleanup. An optional read-only x86
DLL oracle checks 2,004 native selections. The bridge also compiles against the
actual extended-x87 ARM64/libnx ABI.

In the private captured-code startup, both exports now bind and the actual
game callback reaches the explicit `Bink DirectSound transport unavailable` stop.
There are still zero game draws/submissions. The decoder has been validated
separately; connecting its PCM events to the mixer input, timing movie playback
and validating the seven movie calls is the next integration step. The main
startup still uses captured PC code; this is not a cold EXE or Switch boot.

References: [FFmpeg Bink demuxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/bink.c),
[video decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/bink.c),
[audio decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/binkaudio.c).
