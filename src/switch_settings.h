#pragma once
#include <cstdint>
void LoadFnvSwitchSettings();
extern "C" void RexFnvApplyThreadAffinity(uint32_t handle, const char* name);
