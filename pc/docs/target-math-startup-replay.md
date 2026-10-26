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
identical state; duplicate keys and malformed records are rejected. The Python
codec and native diagnostic loader accept at most16,384 records. The incremental
offline replay command retains its narrower4,096-record work budget.

`replay_startup_math.py` starts from a frozen baseline. On a math miss it executes
only that exact request on the target model, writes a fresh cache, and restarts
the same diagnostic executable. No checkpoint is resumed after a poisoned CPU
session. A cache miss still traps; the code does not fabricate a result. Every
original source/object, target core/image, cache and observation is checked.
This slow offline repetition establishes limited control-flow evidence only.
It is not the CPU translation strategy or a performance estimate for Switch.

An optional diagnostic-only empty-register delta mode is configured before
opening a cache with `fnv_startup_math_replay_enable_empty_delta`. The default
still requires a complete88-byte exact key. The opt-in fallback is restricted to
successful FSIN/FCOS records: kind/operation, control/status, TOP, occupied mask
and every occupied register must match. The record must leave control, TOP,
mask and every physical register except ST0 unchanged. The replay then copies
only the verified ST0/status result into the actual caller state, preserving
all other register bytes, including empty-register contents. No host libm or
new numerical approximation is involved. Pushing operations never use this path.
Conflicting eligible answers for the same live key reject the cache.

The target-core test compares16 complete86-byte outcomes with randomized empty
register payloads against actual execution of the frozen ARM64 evaluator. It
also checks changed occupied registers, control/status/TOP/mask, pushing trig,
unsafe output changes, conflicting answers and the default-disabled path.
The original parser, explicit-miss and two-native-thread tests remain enabled;
there are19 native test cases, including extended cache-capacity boundaries.
`--cc` selects a compatible host linker/compiler
without replacing any hash-checked arithmetic objects. Inputs are frozen before
compilation and rechecked after execution. This mode only reduces unnecessary
diagnostic cache variants; previously unseen live operands still stop and need
an exact target answer. It does not change the production CPU runtime or justify
a claim about game/Switch performance.

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

## One-process target math sessions

Repeatedly restarting the entire diagnostic after one new math state is slow
and loses useful concurrent progress. An explicitly configured desktop build
can instead define `FNV_STARTUP_TARGET_PIPE`, link `startup_math_pipe.c`, and
call `fnv_startup_math_pipe_open()` before starting guest workers. It must own
stdin/stdout for the protocol. Existing exact cache lookups run first; misses
are sent to `run_target_math_session.py`, which executes the frozen production
ARM64 evaluator. Default builds still stop on unknown math states and have no
pipe dependency. This changes no production CPU arithmetic or Switch build.

Each request contains the operation and complete 86-byte x87 state. Replies
echo the entire request and carry a result code and complete state. A blocking
host mutex serializes simultaneous workers; none spins while Python evaluates.
Missing, malformed, mismatched, invalid-result or state-mutating failure replies
disable the channel and leave the caller state untouched. Fault handling occurs
after releasing the mutex. Close only after all guest workers have exited.

The Python driver bounds request count (default4,096, explicit maximum16,384),
line size and total session duration,
checks executable/evaluator hashes before and after execution, and writes a
private answer cache and observation. Exhausting a budget closes input or kills
the diagnostic; it never fabricates a numerical answer. Cache-miss counts include
successfully answered pipe requests, so they no longer mean unresolved math.
The answer cache contains only requests handled by that session, not the seed
cache supplied to its child. States, logs and generated executables stay private.

`test_startup_math_pipe.py` applies fourteen distinct requests1,792 times from
two threads across all seven supported operation selectors. It compares every
returned complete state with the ARM64 evaluator and checks seven malformed
replies plus a channel failure after caching a good reply. Transport memoization,
collision/control/reopen checks and larger/default budget checks are described
in `startup-math-transport-cache.md`. It accepts
`--cc`, `--core` (strict host arithmetic objects), `--evaluator`, `--python-deps`
and a fresh `--output`. The existing replay tests also run without the opt-in
define, checking that their previous hit/miss behavior is preserved.

An advancing one-process session is control-flow evidence only. It may reveal
new API gaps, incomplete switch tables or background worker failures; it does
not establish engine frames, native playback, cold EXE boot or console speed.
