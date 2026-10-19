# Native memory-status query

The native memory catalog includes `GlobalMemoryStatusEx`. Its translated x86
callers use the 64-byte, little-endian `MEMORYSTATUSEX` layout and stdcall cleanup.
Invalid structure lengths fail with error 87 without modifying the buffer;
success preserves guest LastError. Tests compare these contracts with Windows.
See Microsoft's [function reference](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-globalmemorystatusex)
and [structure reference](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex).

Physical-memory values come from a native query. Windows uses its actual memory
status. Horizon uses the total and used memory assigned to the application,
queried through libnx SVCs; this is not a hardcoded console-wide DRAM figure.
Linux has a conservative physical-memory provider, not yet exercised by this
Windows test suite. Unsupported or failed providers return failure.

Commit fields describe the guest VM's real configured limit and remaining
commit budget. Total virtual space is the configured CPU arena; available
virtual space counts unreserved VM pages. Other arenas are already assigned to
code, data, stacks, TLS and heaps. Extended virtual availability is zero. A
larger reported allocation budget requires a genuinely larger configured pool;
the query must not manufacture capacity to pass an application's checks.

The query validates writable output before changing it, supports unaligned
guest structures and rejects a poisoned VM. VM counters are read under their
existing lock. Physical availability remains a volatile host snapshot.

`tests/test_windows_memory.py` covers real IAT callers, seven invalid lengths,
64-bit physical totals, stack cleanup, guest LastError, buffer guards, an
unaligned result, reserve/commit/release accounting and readonly-output failure.
The existing actual access-violation tests remain enabled. The memory provider
and thread bridge also compile with the Switch ABI. None of this proves a game
frame, memory fit or execution on Switch.
