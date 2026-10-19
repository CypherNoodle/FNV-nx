# Unsupported ten-byte x87 memory operands

The mandatory `aot_frame_restore.py` overlay rejects x87 instructions with
ten-byte memory operands through the existing `RECOMP_UNIMPL` path. This
includes extended-real loads/stores and packed-BCD loads/stores. The runtime
uses double registers and does not implement these memory representations.
Dropping a store or treating an extended-real load as a four-byte float would
silently change program state.

The diagnostic carries the decoded instruction's address and text and enters
the ordinary unimplemented-instruction tally. CPU builds must keep their
existing trap policy enabled. This guard does not implement 80-bit arithmetic,
exception delivery or the remaining x87 environment operations.

`tests/test_x87_memory_guard.py` uses synthetic x86 encodings, checks their
decoded operand widths, compiles the resulting stop paths for ARM64 and
executes those paths under Unicorn. Each path must report its exact address
once, preserve canary bytes and avoid dereferencing the guest operand. Five
ordinary float, double and register forms are checked for continued translation.
No game input is needed and no Switch hardware result is claimed.

```sh
python pc/tests/test_x87_memory_guard.py --sdk /path/to/xboxrecomp --python-deps /path/to/python-deps --cc /path/to/aarch64-none-elf-gcc --output /private/fresh-output
```

Changing this mandatory overlay invalidates previous generation identities.
Regenerate and rebuild matching CPU artifacts; do not reuse an old NRO merely
because the new synthetic test passes.
