# Bink decoder, frame copy and guest movie bridge

Components now exist for an own FFmpeg adapter that returns RGBA frames and
stereo 48 kHz signed-16 PCM, an own translated Bink sound-selection bridge,
and a bounded movie PCM input on the existing DirectSound mixer. The initial
guest movie provider now implements the seven movie exports, private sound-open
transport, a header prefix and basic pacing. They are not yet a complete movie
player in the game: actual engine callers, threading and locked-texture lifetime
still need verification. No new NRO is delivered by this work.

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

## Packed frame copy

`native/bink_copy.c` copies decoder RGBA rectangles into positive-pitch Bink
surface32 (BGRX) and32R (RGBX) destinations, with X cleared to zero. It supports
COPYALL and explicit NOSCALING, or the default unscaled profile. Alpha/scaling
formats and negative pitch remain explicit unsupported/invalid inputs. The
caller supplies actual source/destination capacities; the helper checks the
entire declared backing, coordinates and overlap before any write. It does not
interpret guest addresses or upload a Vulkan texture.

The captured PC caller requests surface32, optionally COPYALL, with destination
origin zero, a locked-buffer pitch and source rectangle parameters. The new
helper covers that observed format; it is not yet a guest export. A read-only
x86 original-DLL oracle decodes frame90 with sound disabled and compares full
copies against translated rectangles in four native32-bit formats: 2,768,896
byte comparisons and unchanged allocation guards/row padding. It also checks
32/32R channel reversal on a non-gray frame. The source has no alpha; its zero
alpha results do not establish alpha-video support. Channel sums compared with
FFmpeg RGBA support BGRX orientation, but the decoders differ slightly in color
conversion; this is not a claim of bit-identical native/FFmpeg output.

`tests/test_bink_copy.py` additionally exercises own colored RGBA fixtures,
source/destination offsets, padding, supported flag combinations, overflow-size
coordinates, truncated capacities, overlap and unsupported modes. Rejected
cases leave all output untouched. An optional `--devkitpro` cross-compiles the
helper for ARM64; `--original-bink` and `--video` run the native oracle without
exporting decoded media. Both DLL and video identities are frozen in the private
proof. No Switch execution is implied.

The real-video decoder probe also passes every returned RGBA frame through this
helper into a padded BGRX buffer and checks channel order, X and untouched row
padding. The 120-frame introduction test and the two-frame EOF fixture pass;
RGBA/PCM hashes remain unchanged from the decoder-only baseline. The combined
decoder/copy/probe links for ARM64/libnx with no strong undefined symbols or
TEXTREL. No Vulkan texture upload or guest movie ABI is claimed by these tests.

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
buffer volume/pan. The host stream controller below now connects decoder events
to it; the game's guest sound/movie wrappers do not yet use that controller.

The combined DirectSound test compares 204,606 PCM samples, plus simultaneous
movie/game saturation, a full output queue, atomic movie FULL, pause/focus,
two ring traversals including split writes, underrun and reset. Existing native
Windows ABI/listener checks and six explicit invalid-operation stops still pass.
The mixer compiles with the actual ARM64/libnx extended-x87 ABI. These are host
tests and cross-compilation, not Switch playback or performance measurements.

## Decoder-to-mixer stream

`native/bink_stream.c` owns a decoder and borrows the live DirectSound provider.
It delivers one decoder event or at most512 PCM frames per step. When the movie
ring is full, it retains the frame and offset and returns WAIT; it never asks the
decoder for the next event until that PCM block is fully submitted. This keeps
the decoder-owned PCM pointer valid and bounds work per controller invocation.
The audio worker only pumps the mixer; it never invokes the decoder or guest.

There is one active stream, with all open/step/pause/close calls serialized on
the decoder thread. Its owner must have exclusive use of the provider's movie
input, and both provider and input I/O must outlive it. Duplicate open is rejected
without touching the current stream. Open resets the movie input only after
successful decoder initialization. Pause retains decoder progress and pauses
the movie mixer input; errors poison the stream. Close frees the decoder and
resets pending movie input. Already queued device/output audio cannot be retracted.
EOF remains stable, but means decoding ended, not that all submitted sound has
been heard. The stream does not implement a playback clock, guest BINK/BINKSND
layout or texture upload.

`tests/test_bink_mixer.py` runs the real FFmpeg decoder, stream, DirectSound mixer
and output PCM queue together in the extended-x87 host runtime. For120 real intro
frames, the mixer output contains226,560 stereo frames and matches both RGBA and
PCM hashes from the independent decoder-only baseline. It forces349 full-ring
waits, repeated retries without additional decoder reads, pause with queued audio,
output-queue FULL with no cursor consumption, duplicate-open rejection and I/O
failure. A private two-frame container reaches repeated EOF and delivers38,400
PCM frames with the same independent hashes. The later-I/O fault case is skipped
for that short fully buffered input, and is covered on the full intro instead.
The stream also compiles for ARM64/libnx with the extended-x87 ABI. These tests
do not play audio on a device, invoke game Bink exports or boot the game.

## Guest movie provider

`native/bink_movie.c` supplies Open, Close, Wait, Pause, DoFrame, NextFrame and
CopyToBufferRect with their x86 stdcall argument sizes. A dedicated guest arena
contains retired, non-reused64-byte handle slots. The verified public prefix is
seven uint32 fields: dimensions, frame count/current/last and rate numerator/
denominator. Guest changes to these fields are rejected. No completeness of the
remaining native BINK structure is claimed.

The provider's OpenDirectSound factory returns a registered guest-callable
private opener. Open invokes that opener through a six-argument stdcall callback;
it actually initializes the decoder-to-mixer stream. Both sides belong to this
provider: this is not an implementation of an external DLL's BINKSND callbacks
or layout. Custom sound systems and nonzero DirectSound device arguments remain
outside this profile. Configure the sound-selection provider with the movie
factory/context, then bind selection before attaching guest threads. Destroy
selection before movie provider, then mixer; all guest threads detach first.

All guest movie calls must stay on the first controller thread, enforced by an
atomic owner check. The audio worker may pump the mixer separately. I/O callbacks
must resolve paths inside the game sandbox and open read-only; the provider never
converts guest pointers into host FILE pointers. Only observed open flags0,
PRELOADALL and NOTHREADEDIO combinations are admitted. PRELOADALL is currently
treated as a performance hint and the adapter continues bounded streaming; it
does not promise native preloading/memory behavior. Concurrent movies, skipping
undecoded frames, advancing/decoding while paused, alpha/scaled/negative-pitch
copies and foreign sound callbacks stop explicitly.

Wait uses the rational frame rate and a monotonic host clock. Pause shifts the
epoch on resume. Repeated DoFrame is idempotent; NextFrame advances the public
header, and after the final frame rewinds/reopens the same read-only I/O and
starts at frame1, retaining the previous last-frame field until decode. DoFrame
delivers pending audio until a video frame is available. On backpressure the
host callback must return promptly and yield/progress audio without guest calls;
the bridge bounds retries and time, and stops if the device cannot progress.
This pacing is not yet synchronized to measured device latency.

The guest test drives all nine Bink imports through translated x86 CALL[IAT]
wrappers. It performs724 calls over120 real intro frames, compares recovered RGBA
and mixed PCM hashes with the decoder-only baseline, checks stack cleanup,
header updates, duplicate decode, Wait boundaries, a two-second pause and padded
frame copies. A two-frame fixture verifies last-frame looping against behavior
observed with the original DLL. Seven strict stops cover flags, stale/wrong
handle, header corruption, skipping decode, reversed clock, invalid copy memory
and a different controller thread. Both tests use the extended-x87 ABI; the
provider also compiles for ARM64/libnx. They do not establish Switch playback or
execution of the game's own movie caller.

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

In the private captured-code startup, all nine exports bind and the actual game
sound-selection callback now succeeds. The next explicit stop is a thread-name
notification for MoviePlayer whose target is not the currently attached guest
thread. There are still zero game draws/submissions, and no game movie Open has
been observed in that run. The diagnostic's audio sink pumps/discards PCM; it is
not real device playback. The main startup still uses captured PC code; this is
not a cold EXE or Switch boot. General SEH remains unimplemented.

References: [FFmpeg Bink demuxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/bink.c),
[video decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/bink.c),
[audio decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/binkaudio.c).
