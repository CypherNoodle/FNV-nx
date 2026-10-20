# Explicit flat ES alias profile

`fnv-x87-protected32-v3` adds a required `extra_selector` to the existing
experimental protected-mode contract. It must be an integer exactly equal to
`data_selector`: this version supports only ES aliasing the same flat descriptor
as DS. Other ES mappings, descriptor-table emulation and segment-limit faults
are outside this contract. An optional `transcendental_model` selects one of the
existing numerical models; omitting it retains the strict arithmetic model.

Version 1 and 2 profiles continue rejecting ES memory operands. Version 3
permits them explicitly, uses the ordinary flat effective address, and records
the ES selector in FDP metadata when the selected pointer policy updates it.
FS/GS, 16-bit addressing and invalid prefixes remain rejected. No SDK source or
installed game file is modified, and the inline jump-table site is not declared
unreachable or removed from translation.

The synthetic 32-bit Windows oracle reads ES and DS without changing either.
In the measured process both are 0x2b. This observation supports that test's
explicit contract; it is not an automatic selector-detection mechanism for an
arbitrary executable or operating system.

The profile-emitted ARM64 FADD test agrees with native x87 on 2,496 payload and
exception/status comparisons across three precision modes, four rounding modes
and both data-pointer policies. FIP/FDP/selector tracking and output guards are
checked separately. Eight invalid selector contracts are rejected. Coverage
regressions verify that old profiles still reject ES and that the new profile
does not enable FS/GS or 16-bit addressing. The full selected production census
has no unsupported x87 forms under this explicit v3/trigonometric profile.

That test also exposes a pre-existing arithmetic limitation: rounded-up C1 is
not reconstructed by the ordinary SoftFloat arithmetic bridge. There are 740
C1 differences in these cases, reported as
`ES_ALIAS_VERIFIED_WITH_ARITHMETIC_C1_GAP`, rather than a full arithmetic PASS.
Payloads and exception bits match; full x87 flag identity is not claimed. The
trigonometric model has its separately documented numerical/C1 limitations.

Changing a generation profile requires a fresh generation and matched CPU
compilation. The preceding partial CPU build was interrupted and marked
incomplete before changing these frozen code-generation inputs. The new paired
full generations and subsequent compilation do not imply engine startup, a
working NRO or measured hardware performance.
