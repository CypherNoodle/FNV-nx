# Virtual current directory

`GetCurrentDirectoryA/W` returns the immutable guest directory `C:\FNV-nx`.
It represents `file_root`, normally `sdmc:/switch/FNV-nx`; it is not a host
Windows directory and does not reveal the desktop installation path. The file
provider must be configured. There is no `SetCurrentDirectory` implementation
or process-wide host `chdir` side effect.

The Win32 buffer contract follows [Microsoft's reference](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getcurrentdirectory):

- Sufficient capacity returns the length excluding NUL and writes the complete
  string. W counts UTF-16 units and marshals UTF-16LE explicitly.
- Zero or insufficient capacity returns required capacity including NUL, without
  touching the guest buffer. Success/size queries preserve guest LastError.
- Sufficient capacity validates the actual written range, with strict failure
  for an inaccessible output. It does not trust a native pointer or marshal a
  host `wchar_t` layout.

The file-path mapper recognizes this exact prefix case-insensitively, accepting
both slash forms and requiring a component boundary. It strips the virtual
prefix before the existing relative path normalization and sandbox checks.
`C:\FNV-nx2`, other drives and UNC paths remain unsupported; `..` cannot escape
the mapped root. The root itself can be queried with `GetFileAttributes`.

Absolute guest-root aliases and relative game paths share the same normalized
sharing key. They remain read-only. An alias under `C:\FNV-nx\UserData` does not
acquire profile-write privileges. Only the existing `C:\Users\FNV\` mapping
enables that bounded write path. Host filesystem links/aliases are still outside
the existing lexical path sandbox's guarantees.

## Verification and current stop

`pc/tests/test_windows_current_directory.py` uses translated synthetic x86 IAT
callers. It compares A/W return lengths and LastError against native Windows
with corresponding zero, short, exact and oversized capacities. Guard bytes
verify no writes on short buffers or past the terminating NUL. Tests also read
the same fixture through relative, absolute and normalized aliases, reject five
sandbox/write cases, query the mapped root and strictly reject an output that
crosses the guest arena boundary. Synthetic callers and the runtime translation
unit compile for ARM64/libnx.

The private eight-worker startup diagnostic passes the current-directory call
and stops on the unimplemented `FileTimeToLocalFileTime` API. It has read
262,144 bytes of the user's BSA, with zero engine draws/submissions. This does
not demonstrate complete BSA parsing, XML loading, engine boot or a game frame.
Production graphics caps remain unchanged; no new hardware test is requested.
