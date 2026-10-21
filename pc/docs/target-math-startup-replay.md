# Exact target math during the desktop CRT diagnostic

The full desktop CRT experiment deliberately stops before ARM64-only numerical
code. The target evaluator links the exact, hash-checked production arithmetic
objects and target libm/libgcc, then executes that standalone ARM64 image with
Unicorn. It checks instruction budget, FPCR/FPSR preservation, output canaries
and unchanged state on rejected requests. It never substitutes desktop libm.

The wire format records all eight physical ext80 registers (10 bytes each),
control/status (two little-endian bytes each), top and occupied mask: 86 bytes.
The request prefixes operation family and operation index, giving 88 bytes.
An immutable cache starts with `FNVX87R1`, a little-endian 32-bit record count
and four reserved zero bytes. Each record contains the exact request, a 32-bit
result code and the full 86-byte resulting state. Failed operations must retain
identical state; duplicate keys and malformed records are rejected.

`replay_startup_math.py` starts from a frozen baseline. On a math miss it executes
only that exact request on the target model, writes a fresh cache, and restarts
the same diagnostic executable. No checkpoint is resumed after a poisoned CPU
session. A cache miss still traps; the code does not fabricate a result. Every
original source/object, target core/image, cache and observation is checked.
This slow offline repetition establishes limited control-flow evidence only.
It is not the CPU translation strategy or a performance estimate for Switch.

`audit_crt_math_table.py` checks original descriptor setup, an indexed route to
the selector dispatcher, its exact addressing sequence and selector domain.
This catches descriptor tables whose pointer slots are not four-byte aligned.
Bounded CFG closure recovers table roots and directly referenced terminal tails;
new calls, indirect transfers, overlapping instructions, unreachable body holes
and mid-instruction interior entries reject. The route is existence evidence,
not an all-path register-liveness proof or a claim that every handler was run.

`probe_crt_table_entries.py` compiles eligible handlers into a separate diagnostic
and extends its sorted lookup without overriding any frozen original entry.
The original generated objects are retained. Its observation explicitly records
supplementary entries and `generation_matches_all_bodies=false`. The Switch CRT
packager rejects this evidence. Production integration requires adding the
verified entries to a fresh scan/generation and repeating its full validation.

Detailed game requests, bytes, translated handlers, executables, caches and
observations remain private. Public sources contain the reusable tools and
synthetic checks only. None of these tools establishes cold EXE startup, a
running engine, audio/video, rendering, Switch execution or frame rate.

## Managed-worker diagnostics

The desktop probe can opt into four managed guest workers with `--workers`.
Each slot has a separate TEB, TLS expansion page, static-TLS/vector budget,
guest stack and native host stack. The backed worker arena is disjoint from
the image, main-thread stack/TLS, process heap and managed VM pool. Capacity
and active/finished/created/reaped/failure counters are included in observations.
The fixed Switch diagnostic does not share this configuration; packaging rejects
worker-enabled desktop observations until a matching console layout is validated.

The last indirect-call site and selected math operation are thread-local.
Shared math hit/miss counters are atomic; the replay table is immutable after
initialization. It is freed only after workers have left guest execution.
A serialized first-fault callback copies the originating CPU state, call site,
math operation and native thread id before other threads report peer failure.
The observer waits a bounded interval for workers to stop. If cleanup cannot
complete, retained process resources survive until this isolated host process
exits; they are not freed while a worker could still use them.

The concurrent cache test exercises two actual native threads performing 2,000
exact target-answer lookups, distinct TLS records and atomic counts. This checks
the diagnostic observer, not scheduling efficiency or Switch CPU performance.
