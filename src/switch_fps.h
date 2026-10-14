#pragma once
#include <rex/ui/vulkan/api.h>
extern "C" void RexFnvFpsFrame();
extern "C" void RexFnvFpsDraw(VkCommandBuffer commands, uint32_t width, uint32_t height,
                             PFN_vkCmdClearAttachments clear);
