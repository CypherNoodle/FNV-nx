# NULL depth and missing shader pairs

The backend now draws with a NULL depth-stencil surface even when the stored
D3D9 ZENABLE state is true. Its effective Vulkan depth test and writes are off
for that draw; the guest state is preserved. A nonzero stale surface still
rejects. This follows [Microsoft's SetDepthStencilSurface contract](https://learn.microsoft.com/en-us/windows/desktop/api/d3d9/nf-d3d9-idirect3ddevice9-setdepthstencilsurface).

`d3d9_null_depth_oracle.c` tests Windows D3D9 with a hidden window: a red quad
passes with NULL depth and is occluded by the reattached zero-depth surface,
while ZENABLE stays true. The Vulkan COM fixture separately checks the rendered
pixels with NULL depth and retained guest state. Both passed on desktop;
Switch behavior and missing world geometry still require hardware validation.

Program-selection failures now retain at most 32 diagnostic identities:
original VS/PS token FNV-1a64, byte lengths, fixed-function constant size,
reason and cumulative hits. Reason 1 means no prepared original pair; reason 2
means a fixed variant identity mismatch. Excess distinct records increment
overflow; already retained records continue counting. Hashes are calculated
once per encountered catalog entry, under the backend lock. These fingerprints
never select or authorize a shader program. Snapshots are logged at the existing
progress interval, with no per-draw filesystem writes.

`match_missing_shader_pairs.py --log LOG --catalog PRIVATE.sdp --output REPORT`
matches stage, byte length and fingerprint against the validated private
catalog. It emits all candidate indexes and SHA-256 digests, including duplicate
candidates. It does not emit shader bytes or create linked programs. Exact
original bytes, SPIR-V interfaces and uniform contracts must still be verified
when preparing a missing pair offline. Counts are the latest observed values,
not sums of repeated cumulative snapshots; logs containing multiple sessions
should be split when comparing session totals.

Synthetic tests cover standard hash vectors, repeated hits, bounded capacity,
overflow, identity fields and offline stage filtering. The actual Vulkan fixture
rejects a pair twice before linkage, verifies its census entry, then prepares
the trusted pair and renders normally. Census hits plus overflow must equal
program-stage draw rejections. This adds diagnosis and corrects a target-state
bug; it establishes no frame-rate or startup-time improvement.
