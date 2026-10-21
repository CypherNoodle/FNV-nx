# Empty virtual registry

The portable runtime provides predefined registry roots but installs no subkeys.
It never accesses the host Windows registry or writes registration into Windows.
`RegOpenKeyExA` supports the five ordinary predefined roots, options zero, ASCII
names terminated within 1,024 bytes, ordinary registry access masks and either
WOW64 view. Both WOW64 flags together, unsupported rights/options, invalid
handles, inaccessible output/name memory and unsupported encodings stop
explicitly. Other registry APIs and populated keys remain unimplemented.

For a supported nonempty name it returns `ERROR_FILE_NOT_FOUND` (2) and clears
the output handle. A null/empty name returns the predefined root itself and
success. `RegCloseKey` succeeds for those predefined roots without disposing
anything. These exports use stdcall, preserve guest LastError and validate VM
access before touching guest memory. Unsupported cases are explicit faults,
not claims to reproduce every Windows error or registry ACL.

The original PC EXE imports six `ADVAPI32.dll` functions: `RegCreateKeyExA`,
`RegCloseKey`, `RegQueryValueExA`, `RegOpenKeyExA`, `RegSetValueExA` and
`GetUserNameA`. This provider implements only `RegOpenKeyExA` and
`RegCloseKey`. Queries against a returned predefined root are therefore still
unsupported, as are value writes and key creation. Returning the predefined
root for an empty subkey matches the open-key contract; it does not imply that
the root has values or a host registry backing. Keep the query path explicit
until its arguments, result types and game call site are observed and tested.

Microsoft documents the [open-key contract](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexa)
and [close-key API](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regclosekey).
The virtual roots have no host backing handles or predefined-handle cache.
The synthetic test translates calls through the IAT and compares 1,000 absent
HKCU queries against Windows using read/all-access and both WOW64 views. It
checks result 2, cleared output, LastError and stack. Twenty virtual-root checks
cover null/empty names and closing each supported root. Seven separate negative
runs cover unsupported roots/options/access, invalid input/output, non-ASCII
and unterminated names. The provider also compiles for ARM64/libnx with warnings
as errors. This is not a populated registry or a game-rendering test.

The private captured startup progressed beyond `SteamAPI_Init(false)` to an
installation-key query. The desktop diagnostic includes this empty provider in
both the static import and dynamic module catalogs, allowing the unchanged
guest code to take its missing-key path. There is no forced installation path
and no game-specific key in the provider. The Switch CRT adapter is not wired
to these new registry exports yet.

The fresh private platform baseline rebuilt 13 native units while preserving
459 prior objects. With the separately audited copy-unit replacements and
rechecked ARM64 math cache, the captured startup passed the missing-key query.
It then reached a controlled unresolved indirect target in a copy routine;
this is not an engine frame or cold executable boot.
