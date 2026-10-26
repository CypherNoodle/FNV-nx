# Virtual full paths

`GetFullPathNameA` and `GetFullPathNameW` resolve lexical paths relative to the
virtual current directory `C:\FNV-nx`. Absolute paths in that game root and in
`C:\Users\FNV\` are accepted through the existing file sandbox normalizer.
The latter retains its guest profile identity rather than exposing the internal
`UserData/FNV` mapping. No disk lookup or file existence check is performed.

The result uses DOS separators. Dot components and repeated separators are
normalized, and a final separator is retained. Root prefixes have the canonical
virtual spelling. Output may alias input because the entire name is consumed
before writing. The optional file-part pointer is a 32-bit guest address;
directory results ending in a separator return a null file-part pointer.

Success returns the length excluding the terminator; an insufficient buffer
returns the required length including it without changing either output.
LastError is preserved in both cases. Capacity is measured in characters,
including for UTF-16, and only the actual result span needs to be writable.
Both output spans are validated before either is written. Overlapping the
file-part slot with the output string is explicitly unsupported.

This is a bounded sandbox API: ASCII names of at most 511 characters, relative
paths contained in the game root, and the recognized absolute virtual roots.
UNC paths, other drives, drive-relative paths, non-ASCII characters, wildcard
names and parent traversal outside the selected root are not implemented.
It does not claim all Windows path parsing semantics.

`test_windows_full_path.py` invokes the API through translated synthetic x86
callers. It compares A/W output bytes, return sizes and file-part offsets with
the real Windows API on equivalent absolute names, including nonexistent movie
paths, in-place output, exact and insufficient capacities, and optional outputs.
Separate invalid-pointer and overlap cases require a diagnostic stop before
any output changes. Compile checks for ARM64 do not establish execution on Switch.
