# Native pull-MP3 DirectShow profile

HW09 established a required dependency: the PC engine calls CoCreateInstance
for FilterGraph/IFilterGraph while opening MainTitle.mp3, then dereferences its
output even after an activation failure. Original x86 bytes reproduce that
null read. Returning CLASSNOTREG is correct for an unavailable class but cannot
satisfy this engine dependency.

## Implemented path

The own native provider registers only FilterGraph for an in-process activation.
It supplies real reference-counted guest interfaces for IFilterGraph and
IGraphBuilder, IBasicAudio, IMediaControl, IMediaPosition and IMediaEvent.
IUnknown queries return one canonical identity. Unknown IIDs return E_NOINTERFACE
with zero output; retired guest identities are not reused.

AddFilter retains the guest source and calls its JoinFilterGraph. Render queries
the supplied pin for IAsyncReader. Length and SyncRead callbacks feed the existing
minimp3 decoder through bounded guest staging storage. No path is guessed from
the game stack; no Windows codec DLL or host COM graph is used by the provider.
The pull profile does not implement a general DirectShow filter registry or an
arbitrary connected filter topology. FilterMapper, video, automation dispatch,
blocking GetEvent and event handles remain outside this profile.

Callbacks run inside the creating guest thread's execute boundary, with a checked
stack frame and full runtime state restoration. Cross-thread graph access and
unimplemented methods stop explicitly. Graph slot publication is locked; the
lock is released before guest callbacks. A final Release reserves its slot until
decoder, mixer stream and guest source/reader references have been cleaned up.
Eight graphs can be live; a session can issue 512 distinct identities.

Each rendered graph owns a separate bounded stereo s16/48-kHz music ring in the
existing DirectSound mixer. Music, Bink and sound effects mix independently.
Gain and balance apply at consumption; pause and lost focus retain queued music.
A full output queue commits neither music cursors nor other mixer cursors.
Audio workers read PCM only and never execute guest callbacks.

Run primes the music ring. Position and event polling refill it on the guest
controller thread. The observed engine polls roughly every 100 ms; the ring holds
one second. A controller that stops polling for longer can underrun. This is not
a general background DirectShow scheduler. Position counts frames committed to
output, not frames already heard by the device. Queued host audio cannot be
retracted after a stop or seek.

Duration is measured by a bounded decode pass, followed by rewind. Seeking uses
rewind plus bounded decode/discard; playback rate 1.0 is supported. These paths
favor correctness over startup/seek speed. Completion is generated only after
EOF and ring drainage; GetEvent consumes its notification, WaitForCompletion
observes the completed state. Unsupported operations do not return a dummy success.

## Verification and limits

`test_directshow_graph.py` creates a synthetic PE with translated x86 factory and
COM callers. Its IAsyncReader fixture supplies real MainTitle.mp3 bytes through
native guest-callable callback tokens; it is not the actual game's source object.
It compares 131,072 PCM samples (125,156 nonzero) to a separate file-backed decoder, verifies
source/reader references, duration, pause, reset, mute, seek to EOF, event consumption,
stdcall cleanup and unknown-IID rejection. Five requested interfaces are also
queried on the real Windows FilterGraph as a narrow availability oracle; this
does not establish full DirectShow semantic equivalence or a Windows codec oracle.

The mixer regression checks existing PCM/ABI behavior plus independent music
voices, saturation, pause, mute, backpressure, reset, stale-generation tokens,
simultaneous Bink/music output and retention of music across a Bink reset.
The combined runtime cross-compiles for ARM64/libnx. The resulting hw10 NRO
still requires a Switch test with the game's actual source/pin callbacks.
No successful menu or gameplay claim follows from these synthetic tests.

All original MP3s, captured code, recovery reports, generated game C and NROs
remain private. The sources, tests and tools in pc/ are own reusable code.
