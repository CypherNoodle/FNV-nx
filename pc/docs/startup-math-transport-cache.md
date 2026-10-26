# Exact reply memoization in desktop diagnostics

The desktop startup diagnostic delegates unsupported host transcendental
operations to the verified ARM64 evaluator. Previously it sent every cache
miss through stdin/stdout, including requests that had already received an
exact answer earlier in the same session. The session's request budget could
therefore expire while repeating known calculations.

The diagnostic pipe now keeps 4,096 direct-mapped reply slots. Each lookup
compares the complete 88-byte request: operation family, operation, all eight
extended registers, control word, status, TOP and occupied mask. The hash only
selects a slot. A collision causes a new evaluator request and replacement
after its reply passes all existing checks. It cannot select another request's
answer. Cache hits restore the same validated result and full output state.

The pipe mutex protects the cache and transport together. Concurrent requests
cannot read a partially published answer. A failed channel remains disabled,
including for requests that were previously cached. Close/reopen clears valid
slots. Memory stays bounded at 733,184 bytes of packet and valid-byte arrays;
the offline replay-file ceiling is 16,384 records. The helper still defaults to
4,096 requests and requires an explicit larger budget, up to 16,384, for longer
sessions. The file schema and exact reply validation remain unchanged.

`test_startup_math_pipe.py` applies 14 distinct synthetic operations 1,792 times
from two host threads and checks every result against the exact ARM64 evaluator.
Only 14 transport requests are expected for that fixture. Additional cases force
a hash collision, change the control word, reacquire an evicted key, reopen the
pipe, and fail a channel after caching a good reply. Malformed replies continue
to leave guest state unchanged.

This code is a desktop diagnostic fixture, not the production Switch CPU
runtime. It changes neither arithmetic nor game instruction translation. A
longer diagnostic trace is not a measurement of Switch game performance or
evidence of complete game startup. Diagnostic binaries, caches and game traces
remain local.

The Python codec and native replay loader both accept 4,097 and 16,384 unique
structural records and reject an over-limit header. Those parser fixtures do
not claim numerical validation of their padding rows. A separate synthetic
protocol client verifies 4,200 exact replies under an explicit larger budget;
the same client is cut off at 4,096 with the default budget. Zero and oversized
budgets are rejected before spawning a child.
