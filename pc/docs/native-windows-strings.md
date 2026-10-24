# Native Windows byte strings and FILETIME comparison

`windows_strings.c` exports `kernel32.dll!lstrcpyA` and `lstrcatA` as a
stateless optional provider. Append its catalog to the process import catalog;
no host Windows DLL, initialization or teardown is required. Both calls return
the original guest destination and consume two x86 stdcall arguments. Bytes,
including values above 127, are copied without character conversion.

Scans proceed in page-bounded chunks and include the terminating NUL. Each
string has a 1 MiB diagnostic profile limit; exceeding it stops explicitly.
The full destination write span is checked before any copy, including managed
VM commitment and write permission. Null or inaccessible buffers return zero
and preserve LastError. This limited behavior does not implement general SEH.
Unlike Windows, which may leave partial data after an invalid-buffer fault,
the provider prevalidates the span. Overlapping buffers stop explicitly because
the native API does not define their result.

`windows_filetime.c` separately exports `kernel32.dll!CompareFileTime`. It reads
two eight-byte little-endian values without requiring native alignment, compares
them as unsigned 64-bit timestamps, returns -1/0/1 and preserves LastError.
It does not sample a clock or convert a timezone. Unavailable guest input stops
explicitly, including readable arena backing that is uncommitted in the VM.
See the [Microsoft contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-comparefiletime).

`test_windows_strings.py` translates four synthetic x86 callers and compares
3,000 copies and 3,000 appends with native Windows, including guards, opaque
bytes, return values, stack cleanup and LastError. It also checks null pointers,
NUL at a page boundary, an unterminated source, read-only destinations, overlap
and the scan budget. `test_windows_filetime.py` compares 5,049 timestamp pairs
with Windows, including equality, high-bit values, low-word carries and
unaligned guest addresses, plus null/uncommitted-input negative cases.

Both tests use the common `--sdk`, `--python-deps`, `--output` and `--cc`
arguments. Run the native differential tests on Windows with a fresh private
output directory. Reports freeze source hashes and explicitly distinguish
synthetic callers from game execution. Passing them or compiling the providers
with devkitA64 does not establish engine startup or Switch hardware execution.
