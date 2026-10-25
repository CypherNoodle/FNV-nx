# Native Vorbisfile bridge

The bridge implements the six imported Windows32 cdecl exports:
`ov_open_callbacks`, `ov_clear`, `ov_pcm_total`, `ov_seekable`, `ov_read` and
`ov_info`. Xiph Ogg/Vorbis owns native decoding state; a bounded registry associates
it with the guest OggVorbis_File address. Native pointers are never written into
the guest object or passed to translated callbacks.

Read, seek, close and tell callbacks run as nested translated cdecl calls on a
validated guest stack. Seek passes its signed 64-bit offset as two consecutive
DWORDs. The CPU bridge restores caller registers, SIMD, x87 and stack state,
checks preserved registers and rejects stdcall/cdecl stack mismatches. Integer
64-bit results use EDX:EAX, including sign extension of Vorbis errors.

Each configured stream needs 0x11000 bytes of committed guest scratch, disjoint
from other providers. The first page contains up to eight stable 32-byte
Windows32 vorbis_info views; the remaining 64 KiB stages callback reads. Info
contains scalar fields only and a zero opaque codec_setup pointer; guest access
to codec_setup is unsupported. Registry capacity is 1..32 streams. Concurrent or
recursive use of the same stream aborts explicitly. Independent streams retain
separate state. Destroy returns BUSY while any stream is open or being processed.

The host decoder preserves stream ownership: failed open does not close the
source, a missing close callback leaves it open, and stream-only callbacks do
not claim seek support. Read permits signed/unsigned 8/16-bit PCM in either byte
order, with a 1 MiB per-call budget. Initial header bytes are also bounded at
1 MiB. More than eight logical info views is an explicit unsupported case.
Callbacks remain on the calling guest thread; this does not introduce a decoder
worker or a new sound output path. Decoded buffers are returned to the game.

`pc/tools/build_vorbis.py` builds external archives fixed to Xiph Vorbis 1.3.7
(commit 0657aee69dec8508a0011f47f3b69d7538e9d262) and Ogg 1.3.5
(commit e1774cd77f471443541596e09078e78fdc342e4f). It checks archive hashes and
safe extraction, records source/compiler identities and builds a native archive
for desktop or devkitA64. Third-party sources and outputs stay outside the
published sources. Their BSD-style COPYING files remain with extracted sources.
The archive includes the encoder for synthetic validation; production use only
links referenced decoder objects. CMake can enable the bridge with
FNV_VORBIS_INCLUDE_DIRS and FNV_VORBIS_LIBRARY alongside FNV_X86_SDK.

Validation uses a newly generated stereo Vorbis stream and translated x86
callers. Four PCM formats produce 134 matching chunks against direct native Xiph,
with total sample count, metadata, seekability, EOF, two simultaneous streams,
capacity, slot reuse, failed-open ownership, no-close and stream-only checks.
Negative cases reject bad callback ABI, inaccessible output and duplicate open.
Both legacy and extended-x87 CPU configurations pass. CPU and bridge compile
with strict warnings for Switch in both configurations; Xiph itself also builds
for Switch. These tests never open an audio device or play sound. They do not
establish hardware playback, arbitrary game compatibility or autonomous startup.

References: [Xiph callback contract](https://xiph.org/vorbis/doc/vorbisfile/ov_callbacks.html),
[opening a callback source](https://xiph.org/vorbis/doc/vorbisfile/ov_open_callbacks.html),
[PCM read semantics](https://xiph.org/vorbis/doc/vorbisfile/ov_read.html).
