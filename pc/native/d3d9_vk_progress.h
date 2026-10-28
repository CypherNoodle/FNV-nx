#ifndef FNV_D3D9_VK_PROGRESS_H
#define FNV_D3D9_VK_PROGRESS_H
#include "d3d9_vk_backend.h"
#include "d3d9_vk_metrics.h"
enum fnv_vk_phase {FNV_VK_PHASE_IDLE,FNV_VK_PHASE_DRAW,FNV_VK_PHASE_GEOMETRY,FNV_VK_PHASE_TEXTURE,FNV_VK_PHASE_ALLOCATE,FNV_VK_PHASE_SHADER,FNV_VK_PHASE_PIPELINE,FNV_VK_PHASE_FENCE,FNV_VK_PHASE_SUBMIT,FNV_VK_PHASE_ACQUIRE,FNV_VK_PHASE_PRESENT};
/* Independent atomic counters: approximate cross-field snapshot, no renderer
 * lock. The backend must remain alive throughout this read. Time is host time. */
struct fnv_vk_progress {uint32_t phase;uint64_t since_ns,wait_calls,wait_ns,submit_calls,submit_ns;};
uint32_t fnv_vk_backend_progress(struct fnv_vk_backend *,struct fnv_vk_progress *);
/* BUSY leaves both outputs intact. Successful snapshots use one try-lock. */
uint32_t fnv_vk_backend_try_snapshot(struct fnv_vk_backend *,struct fnv_vk_stats *,struct fnv_vk_pipeline_metrics *);
#endif
