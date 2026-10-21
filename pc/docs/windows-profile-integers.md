# Read-only guest INI integer functions

windows_profiles.inc implements GetPrivateProfileIntA/W through an independent
native export catalog (windows_profiles.h). The original built-in Windows export
count and all old public config/ABI headers stay unchanged. The complete desktop
startup and Switch CRT adapters bind these exports; the owning Windows unit
includes the implementation after its existing file provider.

Guest names use bounded ASCII queries (A or UTF-16 ASCII code units for W).
The existing game-root normalizer accepts relative paths, dot components and
both separators; it rejects traversal above the root and absolute/drive paths.
The guest profile environment searches this authorized game root, without any
host Windows-directory or registry lookup. Files are opened read-only; no game
INI, EXE, asset or host user configuration is created, copied or modified.

Actual first original-game request, established by the bounded private fault
observer before implementing it: .\Custom.ini, section General, key
bUseMyGamesDirectory, fallback 1. That file is absent in the authorized Steam
installation, so the implemented missing-file semantics return the requested
fallback and Windows file-not-found error. This is an actual failed file lookup,
not unconditional defaults for every setting.

Section/key comparisons ignore ASCII case and surrounding whitespace. The first
matching section/key wins; later duplicate sections do not merge. Quotes, empty
values, signed integers, lowercase 0x/0o/0b prefixes and unsigned overflow follow
the observed native Windows integer behavior. Malformed numeric suffixes stop
conversion; an existing nonnumeric value is zero. Missing/empty values return
the supplied fallback. Successful queries set LastError to zero; missing files
return their file error. Null section/key behavior is verified separately.

The current subset handles byte-oriented ANSI file contents. UTF-16 file BOMs,
embedded NULs, files over 1 MiB and encountered lines over 4096 bytes stop
explicitly. W query support does not claim UTF-16 file decoding. Non-ASCII query
names, default Windows INI filenames, registry mappings, profile strings,
enumeration and writes are separate unsupported features. No partial or guessed
successful result is substituted for unsupported encodings or IO failures.

Every call reads a bounded file snapshot and closes/frees its resources before
returning or trapping. No process/kernel/file-table lock is held during IO.
There is no persistent parsed-file cache yet; performance is not asserted.

Verification:
- 4,800 translated x86 A/W comparisons with actual Windows profile functions,
  including numeric formats, casing, whitespace, duplicate keys/sections,
  quoting, missing file/key, null queries, stdcall cleanup and LastError.
- Separate rejection processes for root traversal, drive/absolute paths,
  UTF-16 file encoding, inaccessible guest query and overlong lines.
- Existing file ABI regression: 2,000 Windows differential operations, 128
  shared-cursor chunks, large 64-bit offsets and permission/path failures.
- devkitA64/libnx compiles the Windows provider and updated Switch CRT adapter
  with strict warnings. This is not a new NRO or hardware execution.

observe_startup_profile.py refreshes only the safe desktop argument observer.
advance_startup_profiles.py rebuilds the owning Windows unit and catalog adapter
against a frozen complete private diagnostic, preserving game objects/math and
old shared interfaces. Its dependency guard has five synthetic checks. Neither
tool modifies generated game functions or the EXE. Observations, fixtures,
generated code, objects and replay records remain private local/ outputs.

Captured startup requests both SHELL32.SHGetFolderPathA LocalAppData (CSIDL
0x1c) and Documents (CSIDL 5). Both observed calls have null window/token and
flags 0. The provider returns `C:\Users\FNV\AppData\Local` or
`C:\Users\FNV\Documents`, backed by `UserData/FNV/AppData/Local` and
`UserData/FNV/Documents` below the port root. Guest paths do not expose the
host PC's profile. Other folder identifiers, tokens and flags remain explicit
diagnostic boundaries. On Switch the root is `sdmc:/switch/FNV-nx`.
The guest file provider now permits synchronous CreateFile/WriteFile only below
the virtual `C:\Users\FNV\` profile, mapped inside `UserData/FNV` under the port
root. Writes to relative game paths such as `Data\...` remain rejected. A
synthetic translated x86 test covers file creation/truncation, byte writes, cursor-
based end-of-file truncation, flush, write-access enforcement and this path boundary.
`WritePrivateProfileString` and actual game save support remain pending. `CreateDirectoryA` is limited to subdirectories of the virtual profile.
No engine frame, cold EXE launch, or Switch hardware result is claimed.

References: [Microsoft GetPrivateProfileInt](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprivateprofileint),
[Microsoft profile string syntax](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprivateprofilestring),
[Microsoft CSIDL list](https://learn.microsoft.com/en-us/windows/win32/shell/csidl).


Revision de prefijos numericos contra Windows local:0x10->16,0o12->10,
0b101->5,010->10; las variantes0X10/0O12/0B101 devuelven0. La suite A/W
vuelve a pasar4800comparaciones, incluyendo esos casos, signos, overflow,
comillas y sufijos. Por tanto no se sustituyo el parser por uno solo decimal.
Estos resultados describen el Windows usado como oraculo, no una deduccion
basada en strtol ni una promesa sobre toda version historica de Windows.
