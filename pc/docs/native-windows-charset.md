# Native Windows character conversion

The virtual process has an explicit Windows ANSI code page, 1252. `GetACP`
returns 1252 regardless of the development computer's locale. `IsValidCodePage`
advertises only 1252 and 65001 (UTF-8), preserving LastError. It does not claim
that other code pages installed on a desktop are available on Switch. `GetCPInfo`
supports those two pages and CP_ACP (0), which selects virtual 1252. It writes
the 18 meaningful CPINFO32 bytes, leaving the two trailing padding bytes intact;
MaxCharSize is respectively 1 or 4, DefaultChar is '?', and lead-byte ranges are
zero. Invalid/unavailable pages or NULL output return false/error 87.

`WideCharToMultiByte` supports:

- CP_ACP/1252 directly representable Unicode characters with flags 0, including
  the complete 256-byte repertoire. Best-fit substitutions for other characters
  are pending and explicitly fault instead of guessing equivalent letters.
- CP_ACP/1252 with WC_NO_BEST_FIT_CHARS, including default-character substitution
  per UTF-16 code unit and the optional used-default BOOL. A caller-supplied
  replacement byte is supported, including NUL.
- UTF-8 with flags 0 or WC_ERR_INVALID_CHARS: supplementary surrogate pairs,
  replacement of unpaired surrogates, and strict error 1113. This models current
  Windows behavior, not historical pre-Vista surrogate encoding.

Explicit positive input lengths include embedded NULs without adding a new
terminator. Negative lengths scan through the first terminator and include it
in the output. Output capacity zero performs a size query without using the
output pointer. Small buffers preserve Windows' observed partial output and
error 122, never splitting a UTF-8 sequence. Strict invalid-surrogate conversion
can write replacement bytes before returning error 1113, matching the oracle.
Successful conversions preserve LastError. Parameter errors return 0/error 87.

Current Windows also accepts optional default/used-default pointers with UTF-8,
although the documented contract asks callers to pass NULL. The bridge matches
the measured extension: the default byte is ignored; the used-default flag records
surrogate replacement after full conversion and remains untouched on insufficient
buffer. The CRT experiment uses NULL for both pointers.

Input, output and optional BOOL may be unaligned guest addresses. Bounds and
managed-VM access are checked before host access; invalid guest memory faults
explicitly. Same input/output pointer with a nonzero capacity returns error 87.
Other overlapping buffers, unsupported code pages, flags and best-fit/composition
mapping fault explicitly. This is a supported subset, not a full NLS replacement.
MultiByteToWideChar, GetStringTypeW and general locale services remain pending.

`tests/test_windows_charset.py` translates actual x86 import callers and compares
results, LastError, output guards, partial writes, input preservation and optional
BOOL against the Windows API. It covers all 65,536 individual UTF-16 units for
1252 with no-best-fit and UTF-8, 12,000 randomized strings, surrogate-pair extremes,
query/size/alias errors, and metadata. Total: 143,574 conversion comparisons and
24 code-page queries; four explicit unsupported/memory diagnostic controls.
The CP_ACP oracle uses explicit 1252, since the host's ACP may be UTF-8. This is
an intentional virtual locale, not passthrough. ARM64 fixture and runtime compile
checks do not establish execution on Switch.

The captured CRT experiment now passes environment conversion and code-page
queries, then stops at GetStringTypeW. No engine entry, cold EXE boot or game
frame is established.

References: [WideCharToMultiByte](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte),
[IsValidCodePage](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isvalidcodepage),
[GetCPInfo](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-getcpinfo).
