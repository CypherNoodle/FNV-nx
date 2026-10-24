# Private Ghidra analysis for the PC port

Ghidra is an optional reverse-engineering aid. It does not replace the x86 AOT
translator, the Windows ABI providers, Vulkan, numerical verification or testing
on Nintendo Switch. Decompiled types/control flow are hypotheses until checked
against instructions, call conventions and execution evidence.

pc/tools/run_ghidra_audit.py invokes an existing Ghidra installation through its
bundled headless Java entry point. Required arguments are --ghidra, --exe,
--capture, --exe-sha256, --capture-sha256, --text-va, --functions and --output.
--java can select a Java executable; --timeout limits the run to1..600seconds.
Use a fresh PRIVATE output directory. Up to32 unique32bit function addresses
must lie within the captured code range. Input identity mismatch stops before
creating the project. No installed Ghidra files or source EXE are modified.

The script imports the PE32 file with automatic analysis disabled, then checks
and overlays the explicitly supplied code-section bytes INSIDE the Ghidra
project, at the supplied guest VA. This is an analysis copy of existing captured
code, not a new executable, unpacker, cold-boot loader or proof of entry point.
The original EXE and captured bytes must already be available from an authorized
local source. This tool does not produce or obtain them.

AuditCapturedFunctions.java disassembles/creates selected function entries and
asks the decompiler for a bounded report,20seconds per function. Reports retain
warnings and failed functions. Unresolved switch tables can give incomplete
function bodies even when decompilation succeeds. No inferred function boundary
or pseudocode is automatically applied to the port. The current script does not
repair switch tables, discover all functions, rename unknown game APIs, perform
full-program analysis or certify shader/CPU translation.

Ghidra projects, reports and captured-code overlays contain PRIVATE game data.
Do not commit or redistribute them. The reusable Python/Java sources contain no
game bytes or fixed game function addresses. The runner writes validation.json
with commands,input/dependency hashes,output hash and completion marker; Java
exit code alone is insufficient because headless script failures may still exit0.
The report explicitly sets complete_function_recovery,engine_started and
hardware_tested false. It is not a complete dependency lock for every Ghidra
module; it records the launcher jar and application properties.

All logs/projects/settings/cache/temp are directed to the requested output.
On Windows user.home alone does not move Ghidra settings: child-only
XDG_CONFIG_HOME and XDG_CACHE_HOME overrides are also supplied. No global
Windows or installed Ghidra configuration changes are needed.

Verified locally with Ghidra12.1.4 and Java25: one four-entry custom-audio/lookup
audit and one five-entry synchronization audit completed with unchanged inputs.
These are private observations, not evidence of a complete engine frame. Use
cross-references, parameter access and lock ownership alongside live wait traces;
static calls to Enter/LeaveCriticalSection alone do not prove a runtime deadlock.

JVM fatal-error logs are directed to `jvm-error-%p.log` inside the private
output directory, and the child working directory is that output directory.
If HotSpot crashes in compiled Java code, `--java-c1-only` disables the C2
compiler for an explicit retry. A failed JVM run remains a failed analysis;
this option does not relax the completion marker or frozen-input checks.
