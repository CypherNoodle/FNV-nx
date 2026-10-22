#ifndef FNV_WINDOWS_DISPLAY_RESOLUTION_H
#define FNV_WINDOWS_DISPLAY_RESOLUTION_H
#include <stdint.h>
/* Observed platform facts. Surface size is the prepared guest desktop/present
 * geometry. Output size can differ (TV scaling); neither is an internal game
 * render-resolution request. mode0=handheld,1=docked. */
struct fnv_display_resolution {uint32_t mode,output_width,output_height,surface_width,surface_height;};
/* Read-only Horizon query. 1=complete snapshot,0=unavailable/changed during read.
 * Caller retains valid NWindow, serializes resize and pauses guest queries while
 * publishing a new snapshot. Does not resize/register/close native buffers. */
int fnv_display_resolution_horizon(void *nwindow,struct fnv_display_resolution *);
/* Callback for fnv_user32_metrics; context is an immutable published snapshot.
 * 1=known metric,0=unsupported,-1=invalid snapshot. No IPC or host window calls. */
int fnv_display_resolution_metrics(void *,uint32_t index,int32_t *out);
int fnv_display_resolution_changed(const struct fnv_display_resolution *,const struct fnv_display_resolution *);
#endif
