# Western NLS services and CRT initialization

The virtual process now provides GetStringTypeW, MultiByteToWideChar and
LCMapStringW alongside the [existing conversion services](native-windows-charset.md).
These are bounded implementations; they do not claim full Windows NLS coverage.

GetStringTypeW implements CT_CTYPE1 for Unicode U+0000..U+00FF and the additional
characters in the Windows-1252 repertoire. It returns the observed Windows bit
combinations, including control/space distinctions, ordinal letters and superscript
digits. Positive counts include embedded NULs; negative counts include the final
NUL. Unsupported classification levels or characters produce explicit diagnostics,
not zero-filled results. Bad flags and parameters return the corresponding Win32
errors. Guest bounds, VM access and unaligned word output are checked.

MultiByteToWideChar handles CP_ACP/1252 with flags 0, MB_PRECOMPOSED,
MB_ERR_INVALID_CHARS, or their combination, and UTF-8 with flags 0 or
MB_ERR_INVALID_CHARS. It handles complete Windows-1252 byte values, valid UTF-8,
supplementary pairs and malformed/truncated UTF-8 using measured current Windows
replacement boundaries. Size queries do not use the destination buffer. Small
outputs preserve partial writes, including a high surrogate when only one UTF-16
slot fits. Insufficient buffer takes precedence over strict invalid-Unicode error
1113; strict conversion may write replacement data before failing. Composition,
glyph transformations and other code pages remain explicit diagnostics.

LCMapStringW supports legacy LCMAP_LOWERCASE/UPPERCASE for the same western
repertoire plus U+0191. The virtual default locale is English (0x409); neutral,
user-default and system-default aliases select that virtual locale. Invariant
locale is also supported for this repertoire. Host locale is not inherited.
Case mapping is one-to-one here; Windows legacy behavior keeps sharp-s and micro
sign unchanged. Unsupported locales, linguistic/sort-key flags and other Unicode
input are not approximated. Queries, in-place mapping, NUL handling and no-write
insufficient-buffer behavior are checked against Windows.

Uppercasing U+0192 produces U+0191. The existing WideCharToMultiByte now includes
the narrowly verified Windows-1252 best-fit of U+0191 back to byte 0x83, with no
used-default flag. WC_NO_BEST_FIT_CHARS still substitutes normally. General
best-fit tables remain pending.

Tests use translated x86 import callers against actual Windows APIs: 6,736
classification, 147,732 byte-to-wide and 8,474 case-mapping comparisons, including
all two-byte UTF-8 inputs, all Windows-1252 bytes, malformed input, partial outputs,
in-place/query modes, guards, LastError and preserved registers. Six unsupported
or invalid-memory controls remain explicit faults. ARM64 fixture compilation is
separate from Switch execution.

The exact F3 C3 REP RET encoding encountered in a CRT security-cookie check is
handled by the original frame/return overlay without editing the external SDK.
It is a CFG terminator and uses the normal near-return stack behavior. Other
prefixed/operand-size/far returns are not broadened by this change. Seven synthetic
functions cover 7,000 x86/ARM64 emulator comparisons with zero/nonzero counters,
branch targets, nested calls and frames; an overlay-disabled control retains the
unsupported operation. This is emulator evidence, not a hardware benchmark.

`tools/audit_initializer_table.py` audits an explicitly supplied, aligned table
in backed PE data, up to 1 MiB. It checks every nonzero pointer against the existing
function database and records input/table hashes. It does not infer the table's
purpose, register unknown targets, prove ABI or execute code. Reports contain
title-specific addresses and remain private. The independently identified C++
initializer range contains 51,143 slots with 17,728 unique nonzero function pointers;
all are already known analysis entries. A bounded CRT probe stops at the first
uncompiled constructor; preparing a full generation does not establish engine boot.

References: [GetStringTypeW](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-getstringtypew),
[MultiByteToWideChar](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar),
[LCMapStringW](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringw).
