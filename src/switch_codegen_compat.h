#pragma once

// GCC requires weak aliases after the declarator. This declaration preserves
// the original entry point for hooks and adds no guest function body.
// Same ABI as the Horizon SDK's codegen template; preinclude before the PCH.
#if defined(__GNUC__) && !defined(__clang__)
#define DEFINE_REX_FUNC(name) \
  extern "C" REX_FUNC(name) __attribute__((alias("__imp__" #name), weak, noinline)); \
  REX_EXTERN(__imp__##name)
#endif
