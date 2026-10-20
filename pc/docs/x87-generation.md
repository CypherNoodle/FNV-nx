# Experimental extended x87 generation and build contract

The normal generation and build remain in the legacy CPU ABI. Extended mode
requires a separate named generation, the frame overlay, and an explicit JSON
profile. Its schema is `fnv-x87-protected32-v1`, with `contract_status` fixed to
`experimental`, integer `code_selector`/`data_selector`/`stack_selector`, and
boolean `opcode_compatibility`/`data_pointer_on_exception_only`. No selector or
CPU policy is inferred from the host, the EXE, or an installed Windows session.
These profile values still need to match a future audited guest CPU contract.

`run_aot_analysis.py --x87-profile /private/profile.json --generation-name NAME
--frame-restore` uses the independent `aot_x87_codegen.py` driver. Existing SIMD,
multiply and audited normal-frame EH3 options remain available. The driver
installs frame support first, then extended x87/environment and the SAHF bridge.
It neither edits the SDK nor changes a previous generation. Generated headers
require the extended CPU compile definition; reports pin the profile, overlays,
CPU/core sources and actual remaining references to old FP globals.
Unused SDK macro definitions are excluded from that reference count.

`build_cpu_runtime.py` accepts `--x87-profile` and `--x87-core-validation` for
this variant. It verifies the exact generation profile and source identities,
actual Switch ABI core flags/compiler, source/object digests and required core
inventory. Old FP global references are rejected before compilation. Every CPU
and generated unit receives `FNV_CPU_EXTENDED_X87=1`, and partial linking includes
the exact separately compiled core objects. Extended mode rejects legacy object
reuse and the validator-only recheck path. It still produces private objects,
not a runnable game or NRO; successful compilation is not evidence of startup.

## SAHF flag bridge

`aot_sahf_flags.py` captures AH at SAHF into a function-local snapshot. Supported
CF/PF/ZF/SF consumers read those captured bits even if EAX later changes. ADC/SBB
receive the captured carry too. Preserved OF is not represented by this SDK
path: consumers that require it issue an explicit stop, recorded as a codegen
gap. Full EFLAGS across calls and AF reconstruction through the SDK's LAHF path
remain outside this work. No floating comparison scalar is substituted for AH.

The bridge also handles FCOMI/FUCOMI and their pop variants using an independent
CF/PF/ZF snapshot, with cleared OF/SF/AF. All sixteen predicates and ADC/SBB
carry consumers are checked against native instructions. The existing SAHF
stop for consumers of its untracked preserved OF is retained.

`test_sahf_flags.py` compares every AH value with native SAHF: 2,560 SETcc
results and 256 each for Jcc, CMOVcc, ADC and SBB. It also checks explicit OF
failure and compiles the generated functions for Switch. `test_x87_codegen_contract.py`
checks invalid profiles/setups, preserved gaps and the default ten-byte guard.
`test_x87_build_contract.py` checks actual current core object acceptance plus
13 rejection cases for missing/mismatched/stale inputs. `test_cpu_x87_context.py`
continues to test runtime concurrency and real managed workers separately.

## Static coverage and remaining work

`audit_x87_coverage.py` reads only an identity-checked private analysis selection.
It records relevant instructions and unresolved forms without registering new
entries or assuming runtime reachability. Its lighter Capstone census matched
every relevant address, mnemonic and operand width from the earlier full SDK
decode. Detailed reports and addresses must stay private.

The current captured-code census spans 85,808 selected ranges. It found 42
unsupported x87 instructions and 30 SAHF sites. All 16 conditional consumers
identified after SAHF use conditions covered by the new bridge. Remaining forms
include trigonometric/logarithmic functions. These are instruction
sites, not 42 distinct opcode types. FSCALE, FPREM/FPREM1, FBSTP and FCOMI cover 19
sites from the preceding census of 61. A supported opcode category does not establish support for all
raw operands, rounding C1, stack faults or guest exception delivery.

The first complete captured-code generation attempt was interrupted during SDK
setup, before a report or usable build, while correcting the reference validator
and introducing the flag bridge. It is not a completed whole-game generation.
Current pipeline checks use synthetic PEs; existing lower-level correctness and
runtime checks remain separate. A fresh complete generation, remaining form/flag
migration, startup verification and an audited guest CPU profile are still
required. Original EXE cold entry validation and hardware execution are unchanged.
