#pragma once
#include <cstdint>
#include <cstdio>
extern "C" bool RexFnvGraphicsTraceEnabled();
extern "C" void RexFnvGraphicsFrame();
extern "C" uint64_t RexFnvGraphicsFrameId();
extern "C" void RexFnvGraphicsBegin(bool dynamic, const void* info);
extern "C" void RexFnvGraphicsBarrier(uint32_t src, uint32_t dst, uint32_t memory,
                                     uint32_t buffers, uint32_t images);
extern "C" void RexFnvGraphicsState(bool fsi, uint32_t surface, uint32_t depth,
                                   uint32_t control, uint32_t mask, const uint32_t* colors);
extern "C" void RexFnvGraphicsReport(FILE* file, double frames);
extern "C" void RexFnvGraphicsImage(const void* info, bool depth, uint32_t format, uint32_t base);
extern "C" void RexFnvGraphicsResolve(bool fsi, uint32_t dest, uint32_t length,
                                     uint32_t base, uint32_t used, uint32_t rows, uint32_t pitch);
extern "C" void RexFnvGraphicsDepthExport(bool fsi, bool float24, bool conversion, uint64_t shader);
