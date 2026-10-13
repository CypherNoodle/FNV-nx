#pragma once
#include <rex/ppc/context.h>
void FnvObserveBootScope(unsigned function, const char* phase,
                         const PPCContext& ctx, const unsigned char* base);
struct FnvBootTraceScope {
  unsigned function;
  const PPCContext& ctx;
  const unsigned char* base;
  FnvBootTraceScope(unsigned id, const PPCContext& context, const unsigned char* memory)
      : function(id), ctx(context), base(memory) {
    FnvObserveBootScope(function,"enter",ctx,base);
  }
  ~FnvBootTraceScope() { FnvObserveBootScope(function,"leave",ctx,base); }
};
