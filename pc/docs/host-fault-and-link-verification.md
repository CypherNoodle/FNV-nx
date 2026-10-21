# Host exception attribution and replacement objects

The captured-startup diagnostic records the native PC and its offset relative
to the loaded host executable. `last_indirect_site` records the most recent
instrumented guest indirect call on that thread. Execution can pass through
many directly called functions after that call. It is historical context and
must not be presented as the guest instruction that faulted. Older diagnostic
logs called the same historical field `guest_site`; the offline reader accepts
both names.

Use the exact diagnostic executable that produced the log with
`pc/tools/symbolize_host_fault.py --exe PRIVATE_EXE --log PRIVATE_LOG --nm NM
--output PRIVATE_JSON`. The tool reads PE32/PE32+ image layout, checks that the
fault offset lies in an executable section, and rebases it to the linked image
address for `llvm-nm` or GNU `nm`. It reports the nearest preceding text symbol
and offset, ignoring data symbols. Same-address aliases remain explicit.
This is a candidate location, not debug-line information or a proof of function
extent. A stripped binary can produce no candidate. Confirm the instruction
with a disassembler before blaming a guest function, provider or memory API.
Input hashes bind the report to the supplied EXE, log and symbol tool; choosing
the matching EXE/log pair still requires build provenance.

A replacement generated unit existing or compiling successfully does not mean
that the replay used it. Before linking, run
`pc/tools/verify_link_replacements.py --response PRIVATE_RSP --replacement
PRIVATE_OBJECT --output PRIVATE_JSON`, repeating `--replacement` for each
recompiled object. The response format is one quoted linker argument per line,
with `-o` and the output path last. Relative object paths resolve from the
current working directory, matching a linker invoked there.

The verifier rejects a missing replacement, the old object of the same name,
both old and new objects together, and duplicate object paths. It records the
exact replacement paths/hashes and response hash. It verifies inputs only; the
caller must link using that unchanged response and record the resulting EXE
hash and link outcome separately. It does not execute the diagnostic or
validate guest behavior. Both tools write new reports exclusively and leave
private EXEs, captures, generated C and assets untouched.

Synthetic tests cover ASLR rebasing, legacy/current log labels, data-symbol
decoys, aliases, absent symbols, PE32/PE32+ bounds and stale replacement object
selection. They use fabricated PE headers and object placeholders, without
game bytes. The tests do not establish engine startup or Switch execution.
