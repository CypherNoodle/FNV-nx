#pragma once
#include <cstdint>

// SDK calls these only after its existing big-endian guest read-pointer store.
extern "C" bool RexFnvRingProgressEnabled();
extern "C" void RexFnvRingProgressPublish(uint32_t address, uint32_t value);
extern "C" void RexFnvRingProgressInvalidate();
// 0 unsupported, 1 progress observed, 2 bounded timeout. No guest state changes.
extern "C" unsigned RexFnvRingProgressWait(uint32_t physical_address, uint32_t observed);
