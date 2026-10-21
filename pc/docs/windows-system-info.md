# Guest SYSTEM_INFO32

windows_system.c exposes KERNEL32.GetSystemInfo through its own native catalog.
It writes the 36-byte x86 structure explicitly, without copying the native host
SYSTEM_INFO (which is a different structure on x64/ARM64).

The declared virtual CPU profile is i686, family 6/revision 0. Architecture is
PROCESSOR_ARCHITECTURE_INTEL (0), reserved word zero, legacy type 586. Processor
level is descriptive and does not claim that CPUID feature handling is complete.
The caller explicitly selects 1..32 virtual logical processors; the captured
PC and Switch CRT adapters currently select three. The active mask matches that
count. Worker capacity is not used to infer a processor count. This is a guest
configuration, not a measurement or guarantee of core affinity/performance.

The page size is 4096 and reservation granularity 65536, matching the existing
VM implementation. Application address bounds come from the current process's
immutable, actually backed flat arena, inclusive at the upper end. No 4 GiB
address space or host memory capacity is invented. Allocation is still limited
to the managed VM pool and its commit budget.

Output validation checks all 36 bytes before writing. Pointers outside the arena,
null, uncommitted and read-only managed VM pages stop explicitly; Windows SEH
access-violation delivery is not implemented. Unaligned valid pointers work.
LastError is unchanged and the void x86 stdcall argument is removed correctly.

The provider registers a native method before attach and unregisters it at
teardown. The CPU registry rejects teardown while any guest thread is attached,
so a worker cannot read freed provider state. Its single-process lifecycle is
consistent with windows_modules.c. The system provider is destroyed after the
module catalog and before the Windows process. The address-bound accessor adds
no fields to an old config structure and changes no old ABI header.

Verification:

- 3,000 calls through synthetic translated x86 imports across 1/3/32-processor
  profiles; exact field offsets, VM/non-VM unaligned outputs, 36-byte sentinel
  guards, preserved LastError and stdcall cleanup.
- Four isolated negative cases: null, arena overrun, read-only VM and uncommitted
  VM. Invalid creation counts, duplicate provider and teardown while attached
  are rejected.
- Native Windows x86 layout oracle separately compiles against Windows headers;
  sizeof and every member offset are checked, plus actual page/granule values.
- The extended-x87 runtime regression passes 285 managed threads/1,000 Windows
  comparisons, including nested/suspended workers, TLS and abandoned mutexes.
- devkitA64 compiles the new provider, updated Windows translation unit and CRT
  adapter with libnx and strict warnings. No new NRO/hardware run is asserted.

advance_startup_system.py rebuilds only the Windows bounds-accessor unit, catalog
adapter and new provider of a frozen private desktop diagnostic. All old shared
ABI headers, game bodies/objects and arithmetic objects must remain unchanged;
its dependency policy has four synthetic checks. Full builders also include the
provider, for both PC diagnostics and the Switch CRT path.

Captured startup now passes GetSystemInfo and reaches GetPrivateProfileIntA at
0x876B13, with 166,053 calls. The same exact target-math model/cache construction
is used. This is a private captured-text diagnostic, not cold EXE startup, an
engine frame, a performance measurement or a hardware proof. All generated code,
objects, captures, math records and reports stay in local/.

Reference: [Microsoft SYSTEM_INFO](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-system_info).
