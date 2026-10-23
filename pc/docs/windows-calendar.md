# FILETIME calendar conversions

The base export catalog includes `FileTimeToLocalFileTime` and
`FileTimeToSystemTime`. They operate on guest little-endian structures, preserve
LastError on success and snapshot the input before writing overlapping output.
Unaligned structures are accepted. Invalid guest ranges follow the existing
explicit runtime fault policy, rather than dereferencing native guest pointers.

`FileTimeToSystemTime` performs integer Gregorian calendar conversion starting
at 1601. It writes the eight 16-bit SYSTEMTIME fields, with Sunday weekday zero
and milliseconds truncated from 100 ns ticks. Values with bit 63 set fail with
ERROR_INVALID_PARAMETER (87) and leave output unchanged. The algorithm skips
400-year cycles and needs at most 399 year steps and 11 month steps; it does not
rely on host time_t range, floating point, locale or host structure packing.

`FileTimeToLocalFileTime` adds the CURRENT UTC offset, including current DST.
It intentionally does not apply historical DST rules for the input timestamp.
Unsigned wrap around the FILETIME epoch is retained, matching Windows. On
Windows the offset comes from GetTimeZoneInformation; on Horizon it comes from
timeGetCurrentTime and timeToCalendarTimeWithMyRule using the console's rule.
libnx initializes the time service at application startup. POSIX uses the
host's current localtime/gmtime difference. If the host cannot obtain the offset,
the runtime faults explicitly; it never silently pretends that local time is UTC.

References:
- [Microsoft FILETIME conversion and current DST behavior](https://learn.microsoft.com/en-us/windows/win32/sysinfo/file-times).
- [FileTimeToSystemTime range and ABI](https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-filetimetosystemtime).
- libnx `switch/services/time.h`, TimeCalendarAdditionalInfo.offset in seconds.

## Verification and remaining work

`test_windows_calendar.py` executes synthetic translated x86 IAT calls, comparing
60,072 results, errors and output bytes with the Windows APIs. Cases include
100 ns and millisecond boundaries, 1601, 1970, the full signed FILETIME range,
rejected high-bit values, unaligned input/output, in-place and partial overlap,
and output guard bytes. Another 50,112 month boundaries across years 1601..30827
are constructed with native Windows SystemTimeToFileTime and checked with its
inverse. Two separate cases fault on input/output beyond the guest arena.
The callers and actual runtime compile for ARM64/libnx; the timezone IPC path
has not been executed on Switch.

The private eight-worker startup diagnostic now passes these two imports and
stops on `GetTimeZoneInformation`. It reads 262,144 bytes of BSA data but still
emits zero engine draws/submissions. Full timezone rule marshaling for that next
API remains separate work: a current offset alone cannot describe annual DST
transition rules. No engine boot, full BSA parse, cold EXE boot or game frame is
claimed. Production graphics capabilities are unchanged.
