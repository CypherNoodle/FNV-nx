# Read-only custom multimedia I/O

The provider implements the x86 MMIOINFO72 and MMCKINFO20 layouts, independent
of native host pointer sizes. It supports mmioOpenA, mmioClose, mmioRead,
mmioSeek, mmioGetInfo, mmioSetInfo, mmioAdvance, mmioDescend and mmioAscend.
Opening currently requires a supplied guest I/O procedure, MMIO_READ with
MMIO_ALLOCBUF and the default8192byte buffer. The procedure handles open3,
read0,seek2 and close4 using the actual translated guest callback and checked
stdcall frame. It can retain its own resource state in adwInfo. No filename
is interpreted as a host path by this provider, and no callback result is
invented. Thus resource archive access stays in the engine's own file layer.

The creator supplies at least0x11000 guest RW bytes disjoint from other provider
arenas, retained through destroy. Eight slots own independent info and buffers.
Native generation-bearing HMMIO tokens are separate from Win32 file handles;
stale tokens cannot select a recycled stream. One shared table mutex covers
reserve/acquire/release only. No native mutex is held during guest callbacks.
Concurrent or recursive access to the same stream fails explicitly. Distinct
streams can run on distinct workers. A faulted callback leaves its slot busy;
process failure does not pretend to close a guest-owned resource safely.

Reads refill a real buffer through the guest procedure and serve partial/EOF
reads. Seeking inside cached bytes avoids an extra callback. SEEK_SET and
SEEK_CUR support signed displacements; SEEK_END currently supports displacement
zero only. Nonzero SEEK_END remains explicit: the desktop WinMM oracle showed
inconsistent cached-end behavior after seeks/reads past EOF. Early attempts to
infer its signed-offset behavior failed, so no guessed compatibility rule was
kept. GetInfo copies state; SetInfo allows only the read cursor to move within
its current valid buffer. Advance refills the read buffer. Chunk traversal
handles RIFF/LIST type words, odd-byte padding, parent bounds and overflow;
malformed/truncated searches return chunk-not-found.

Not implemented: DOS/default file handles, memory-file mode, installed I/O
procedure catalogs, variable/user-owned buffers, write/create/delete, dirty
chunks, async file I/O or a multimedia codec. This is a stream/RIFF layer,
not an MP3/WAV decoder or proof of audible playback. Existing decoder and
DirectSound providers remain responsible for decoding/output.

Validation test_windows_mmio.py translates synthetic import callers and a guest
stdcall forwarding callback. Its native test I/O backend serves only synthetic
bytes. It compares3011 read/seek/stream operations against actual WinMM,
checks RIFF padding and parent offsets, GetInfo/SetInfo/Advance, failed open,
EOF, eight-handle exhaustion and generation reuse. Five negative modes cover
unsupported open shape, bad output memory, wrong callback cleanup, altered info
and nonzero SEEK_END. The native WinMM callback uses64bit host pointers; guest
ABI offsets and pointers are verified separately. It does not access real game
assets in the public test. Actual private startup uses the engine's callback.

DirectSound now accepts DSBCAPS_GETCURRENTPOSITION2. Its play cursor already
uses frames actually consumed by the mixer rather than a legacy lagging cursor.
The capability remains reported in buffer caps. Existing DirectSound tests run
with this flag and validate204606 PCM samples plus Windows ABI and cursor rules.
No spatial secondary-buffer support is inferred from this flag.

References:
https://learn.microsoft.com/en-us/windows/win32/api/mmiscapi/nf-mmiscapi-mmioopena
https://learn.microsoft.com/en-us/previous-versions/dd757322(v=vs.85)
https://learn.microsoft.com/en-us/windows/win32/api/mmiscapi/nf-mmiscapi-mmioseek
