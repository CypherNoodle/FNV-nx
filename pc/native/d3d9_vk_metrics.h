#ifndef FNV_D3D9_VK_METRICS_H
#define FNV_D3D9_VK_METRICS_H
#include <stdint.h>
struct fnv_vk_backend;
/* Host pipeline creation time, including driver compilation. Not GPU time.
 * Snapshot is serialized with the renderer; counters are cumulative. */
struct fnv_vk_pipeline_metrics {uint64_t calls,host_ns,max_host_ns;};
uint32_t fnv_vk_backend_pipeline_metrics(struct fnv_vk_backend *,struct fnv_vk_pipeline_metrics *);
#endif
