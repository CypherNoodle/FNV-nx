# Composing an indexed-table selection

`aot_table_selection.py` derives a new analysis directory from a preserved
selection. It first replays the parent's original code/data evidence. It then
reruns the indexed-dispatch pointer-table census against the parent's bytes and
function records, rather than trusting a list of addresses from an earlier report.

Only the independently evidenced orphan seed records are removed from
`functions.json`; the derived summary is recomputed. Every retained record,
all other database files, the executable analysis image, the profile and the
archived scan wrapper remain unchanged. Earlier exclusions and table-tail
trims are inherited through the preserved parent. This tool supports one
additional layer, avoiding recursive or self-referential selection chains.

Use `--analysis <parent> --output <fresh-directory> --python-deps <dependencies>`.
The new `indexed_table_filter` declaration records the parent scan hash, tool
hashes, evidence hash, inherited proof hash and exact additional exclusions.
`verify_derivation` reconstructs the candidates and expected child files from
the parent. Rehashing modified child files or replacing the evidence list does
not make them valid. Empty, null, unknown and contradictory declarations fail.

The generation driver and CPU build route selection checks through this wrapper;
older code/data selections still use the unchanged original verifier. A named,
full generation is required. The subsequent `verify_aot_code_data.py` comparison
checks each retained generated body, all dispatch addresses, unresolved symbol
uses and unsupported-instruction coverage. An unresolved target may disappear
only when every prior generated caller belonged to an excluded data seed.

The comparison requires identical arithmetic, translator and helper inputs.
The orchestration runner may change to add selection checks; it is recorded by
hash in each generation, while retained body equality is still mandatory.
Profile, mode, x87 translator and numerical runtime changes are rejected.

`test_indexed_table_selection.py` uses a constructed PE32 fixture with an earlier
guarded-table exclusion and a separate indexed table. It checks preservation of
the parent and retained records and rejects altered or rehashed evidence,
modified parent data and malformed declarations. Detailed derived files remain
private under `local/`; none of these checks establishes engine execution.

Private startup subsets use the same compositional verifier through
`generated_subset.py`; they must not fall back to the original verifier when
an `indexed_table_filter` declaration is present. The subset proof test first
loads a valid complete generation and checks its body/reference/dispatch sets,
then rejects changed report hashes, body proofs, selections, SDK identity and
frozen source inputs. A passing compilation or subset proof is not engine boot.
