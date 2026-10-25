# Stop native polling after a peer fault

A shared CPU-session fault already stopped managed waits and newly entered
callbacks. A guest routine that was already running could still repeatedly
look up and call native imports, such as TLS queries, after a different thread
failed. The initial failure remained recorded, but the polling thread could
consume a core indefinitely and prevent orderly diagnostic teardown.

Native kernel-token lookup now checks the shared fault flag while inside a CPU
execution boundary, before entering the native provider. It unwinds that thread
as a peer fault. Peer stops do not invoke the primary diagnostic callback, so
they do not replace the original failure. Lookup outside execution retains its
existing behavior. No successful return or guest state is fabricated.

The check happens before provider acquisition of its locks. It is not inserted
into argument reads or return helpers, where a provider might hold a lock. This
is a cooperative boundary, not preemption of arbitrary guest loops or blocked
host calls that never reach a runtime boundary.

The regression uses an actual SDK-translated x86 loop with CALL [IAT]. Another
host thread faults the session. Before the fix, the loop entered the native
provider over one thousand additional times; its bounded emergency stop made
the test fail without hanging. After the fix, at most a single call racing with
fault publication may enter, and the next lookup stops as a peer fault. The
original fault and thread-local execution state remain independently checked.

The motivating private desktop loading diagnostic had an earlier DirectSound
identity failure while the main guest thread kept polling TLS. Fixing peer
propagation exposes that original problem and avoids the secondary busy loop;
it does not repair DirectSound lifetime or establish engine startup/performance
on Nintendo Switch.
