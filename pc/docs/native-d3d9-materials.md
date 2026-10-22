# Material state for fixed vertex processing

COM slots49/50 implement SetMaterial/GetMaterial. D3DMATERIAL9 is68 bytes:
diffuse, ambient, specular, emissive (four binary32 components each), then power.
Guest reads/writes explicitly serialize all17 little-endian words; no host
pointer or native Windows structure is retained. Invalid or partial guest ranges
return INVALIDCALL without changing state or touching output.

The single live material starts at zero. SetMaterial stores bit patterns without
clamping colors or power. While recording a stateblock it updates the recorded
material and leaves GetMaterial reading live state. Capture refreshes only a
recorded material; Apply changes it only after successful numeric/binding/constant
transactions. A successful Reset clears the live material; a failed Reset leaves
it unchanged. Stateblocks retain their recorded material across the native
provider's Reset, consistent with existing native matrix block policy. This
last lifetime rule is not claimed as Windows Reset compatibility.

Windows HAL observation: `local/material-oracle03/validation.json` passes1005
comparisons (initial zero,1000 finite material roundtrips including negative
power and10000,record/live isolation,Apply,Capture,Reset default). The oracle
releases its block and rebuilds presentation parameters before Reset. An earlier
attempt failed Reset with the old parameters and a live block; it did not prove
Reset behavior. An exploratory null-output GetMaterial crashed the Windows
retail path; that is not used as the oracle for our safe guest-pointer handling.

`local/vulkan-material-com01/validation.json` passes99 translated synthetic
callers, material recording/capture/apply/range checks and failed/successful Reset,
plus existing46 real GPU draws,251/256 submissions and pixel-negative controls.
ARM64 Cortex-A57/libnx strict compilation of the COM module passes.

These methods store CPU-side material state only. The existing unlit variant
does not consume material state. Lights, normal transformations, color sources
and material lighting shader inputs remain pending; Hardware T&L caps stay off.
No new game replay, Switch run, engine frame or NRO is implied.

Reproduce Windows oracle:

```powershell
python pc/tests/test_material_state.py --output local/material-new --cc C:/devkitPro/msys2/clang64/bin/clang.exe
```

Next light API semantics use official documentation for reference:
[SetLight](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setlight),
[LightEnable](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-lightenable).
Their documented defaults and limits still require tests against the supported
Windows path before extending the lit shader contract.

The bounded directional shader now consumes these materials; see [directional vertex rendering](directional-vertex-rendering.md) for its explicit limits and pixel evidence.
