# Explicit hot-leaf inlining

The Ranger load samples repeatedly reached list traversal, comparison and
insertion routines. The visible menu samples instead reached renderer state
access and Vulkan command-buffer reset. These are manually timed samples, not
statistical CPU shares or a clean load-time benchmark.

The first CPU optimization keeps the list algorithm and its comparison order.
It integrates four small generated getters/predicates into selected callers.
The original functions remain available to every other caller and to indirect
dispatch. No Windows service, list node, floating-point operation or guest memory
write is skipped.

`tools/inline_generated_leaves.py` takes explicit caller/callee names and private
generated sources. It copies each accepted leaf body literally, changes only its
C name and adds `always_inline`, then redirects the selected `RECOMP_ABI_CALL`
function arguments. The original guest return-address push, ABI-check macro,
TLS-register writes, guest stack stores and frame restoration remain. Empty
compiler memory barriers surround each integrated call so removing a native
call does not also remove its compiler memory boundary; they are not hardware
fences or replacements for thread synchronization. Strings,
comments and unselected functions are preserved as text after newline
normalization. It rejects unknown calls, directives, local static storage,
source-identity macros, duplicate definitions and mismatched call addresses.

This is compiler inlining of the existing translated operations, not a
hand-written replacement of the game's algorithm. It also does not change the
compiler optimization level, floating-point flags, volatile accesses, threading
or x87 representation. An indirect call is not assumed to target a particular
getter just because it did so in a sample.

## Validation and packaging

The synthetic test compares 4,132 bytes of registers and guest memory after
12,000 seeded cases. It includes pointer loads, conditional low-byte register
updates with preserved high bits, stack writes, and both branches of a predicate.
Seven negative transformations must reject. These fixtures are original test
inputs; they do not contain game code.

`tools/build_inline_generated_units.py` reconstructs the transformation before
admission, verifies its generated-source and original CPU-object provenance,
keeps the original compiler command except source/output paths, freezes current
headers, and checks selected ARM64 relocations. In the first private candidate,
two units lose 16 direct call sites to those leaves. Those are static sites that
can execute repeatedly, not a count of 16 calls during the entire load. Object
growth is bounded. Ten altered admission proofs must reject.

The artifact builder accepts this only through `--inline-proof`. It rejects
overlap with another CPU replacement and binds the proof to the NRO receipt.
The startup line reports the actual admitted unit/site counts; an ordinary build
reports zero. Private source copies, object files and dumps stay under `local/`.

Console load-time improvement is not established by removed relocations. Compare
the same startup at the same clocks using the stable NRO/log filenames. Pausing
under GDB changes elapsed load time; the debugger is for locating work, not for
claiming an uncontaminated speedup.

Algorithm replacement remains a separate investigation. Before changing a list
sort, identify its contents, consumers, tie behavior, and any side effects of
its virtual getters/comparator. A platform change alone does not prove that
building the list is unnecessary.
