# stb_dxt 1.12

Unmodified public header from [nothings/stb](https://github.com/nothings/stb),
commit `7023e273f1513b68b8d4086077c6faca555d50df`.
SHA-256: `807667ef98e0fd749cdb65cca0c2d980bc148109d2fed6f1873c81ae0f449933`.

Used under the MIT alternative license printed at the end of the header,
including Sean Barrett's copyright and permission notice. The full upstream
MIT and Unlicense alternatives remain intact. No game or Windows DLL source
is included in this dependency.

`python pc/tools/fetch_stb_dxt.py` verifies the identity without network access.
`--fetch` restores only a missing header, verifies its hash before writing and
refuses to replace a modified existing file. The provenance JSON records the
exact upstream URL and version. Normal builds require no network access.

The native BC mip helper uses the DXT color/BC3 compressors, with symbols
prefixed at inclusion to avoid collisions. Our decoder, filtering, cutout
selection, DDS construction and ownership code live outside this header.
