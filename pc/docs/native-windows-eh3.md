# Normal MSVC EH3 frame helpers

This extension implements normal registration-frame exchange for an additional
MSVC helper family. It does not implement exception dispatch, handler execution,
unwind, throwing or resuming a Windows exception.

An initializer experiment initially appeared to reach the engine boundary after
450 indirect calls. Observing the constructor loop revealed an early-exit false
positive: its third callback changed the caller's ESI, frame shadow and table
bound. Two normal EH3 helper addresses were already known functions, but the
generator recognized only the separately audited SEH4 pair.

The EH3 prolog establishes its frame through `MOV EBP,EAX`. That assignment
must publish the new frame, and its caller must read the frame back after the
helper returns. The normal epilog also exchanges frame state with its caller.
Omitting either side can restore registers from an unrelated frame.

`audit_eh3_helpers.py` requires complete 17-instruction prolog and 11-instruction
epilog bodies, existing text entries, an absolute cookie in data and exact input
hashes. It records the specific publication instruction. It never registers an
unknown target or accepts a helper from an isolated byte marker.

`aot_eh3_frames.py` adds the audited prolog/helpers to the generator's existing
normal-frame census before translating a function, so calls publish and read
back their local EBP. Only the audited frame-establishment instruction receives
the additional shadow publication. The external SDK is unchanged. Use
`run_aot_analysis.py --frame-restore --eh3-helpers /private/audit.json` in a
separate named generation; the overlay and whole-body audit are pinned in its
report and compared between reference and filtered generations.

The synthetic host fixture runs 1,000 nested-frame cycles across four contexts,
clobbering and restoring nonvolatile registers and checking live saved frames,
registration chains, stack, FS and LastError. The epilog intentionally reuses
the saved-EBP slot for its synthetic return, so the saved value is checked while
the frame is live. ARM64 compilation is separate from ARM64 execution. Thirty-three
audit/pipeline checks include deleting every helper instruction and changing
proof fields. Legacy normal-frame and SEH4 fixtures remain separate regressions.

Fresh complete generation, audited compilation and initializer observation are
required before releasing a hardware diagnostic. A synthetic fixture proves
only its covered frame patterns; it does not establish game startup.
