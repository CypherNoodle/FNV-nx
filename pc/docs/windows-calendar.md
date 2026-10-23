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

## GetTimeZoneInformation

The catalog now has 103 base exports. `GetTimeZoneInformation` marshals the
172-byte TIME_ZONE_INFORMATION layout explicitly: Bias at 0, StandardName at 4,
StandardDate at 68, StandardBias at 84, DaylightName at 88, DaylightDate at 152,
and DaylightBias at 168. Success preserves guest LastError. Host failure returns
TIME_ZONE_ID_INVALID, reports its error and leaves guest output untouched.
Inaccessible output follows the runtime's explicit guest-memory fault policy.

Windows uses the native timezone API, preserving its localized UTF-16 names and
relative transition dates. Horizon loads the device location's 0x4000-byte rule
with libnx; an own bounded decoder reads its explicit transition table. This
uses the documented [Horizon TimeZoneRule layout](https://switchbrew.org/wiki/PSC_services#TimeZoneRule),
not a copied timezone implementation. For the current local year it emits actual
absolute-year SYSTEMTIME transitions, expressed in the wall time BEFORE each
clock change. The standard and daylight offsets determine Bias/DaylightBias;
there is no assumption that DST is positive, exactly an hour, or northern.
Current standard/daylight status is selected using UTC transition instants.
Names use the rule's ASCII abbreviations rather than Windows display names.

Supported rule shapes are a fixed standard zone or a year with exactly two
standard/daylight changes and consistent offsets. The decoder checks counts,
type indices, ordered timestamps, offset range, name bounds and terminators,
minute representability and transition coverage on both sides of the year.
Malformed rules, incomplete explicit coverage, one or more than two changes,
same-DST political offset changes, non-minute offsets, overlong/non-ASCII names,
and unsupported years fail without publishing partial output. It does not
extrapolate the rule's 400-year repeat flags. POSIX currently returns error 50:
a current-offset-only fallback cannot supply real annual rules. Horizon failure
also returns error 50, or 8 on allocation failure. This bounded support is not a
complete mapping of every IANA timezone or historical Windows timezone behavior.

The test adds 1,479 timezone checks: native translated Windows output/return/error
and guard-byte comparisons, synthetic north/south transition rules, negative and
half-hour DST and fractional-hour standard offsets, exact transition edges,
malformed-table rejection without output mutation, and UTC without DST.
Synthetic rule conversions are cross-checked with Windows
SystemTimeToTzSpecificLocalTime. The third strict case rejects a 172-byte output
that crosses the arena boundary. ARM64/libnx compilation covers the real provider;
Horizon timezone IPC execution remains untested in hardware.

The private startup now passes the timezone import, reaching further x87 math.
Four added requests were evaluated with the exact compiled ARM64 model, with no
host-libm substitution. The reduced asset fixture then failed while loading a
menu model. A complete private asset fixture instead stops earlier on
a DDS metadata query for a texture, after 20 reads totaling
4,521,984 bytes. The [metadata bridge](native-dds-image-info.md) now passes and
the [COM texture bridge](native-dds-texture.md) passes complete-chain DDS loads
and now stops on missing DXT5 mip generation. This does not
yet prove the menu-model path is repaired. There are still
zero engine draws/submissions in completed reports. No engine boot, cold EXE
boot or game frame is claimed. Production graphics capabilities are unchanged.
