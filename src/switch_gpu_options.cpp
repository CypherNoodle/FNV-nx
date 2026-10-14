#include <rex/cvar.h>
REXCVAR_DEFINE_BOOL(fnv_batch_submissions, false, "FNV",
                   "Experimental primary-buffer batching; flush before command waits and presentation");
REXCVAR_DEFINE_BOOL(fnv_gpu_timestamps, false, "FNV",
                   "Sparse nonblocking GPU timestamp groups; diagnostic overhead may affect timing");
REXCVAR_DEFINE_BOOL(fnv_fsi_late_opaque, false, "FNV",
                   "Experimental shorter FSI interlock for guarded opaque side-effect-free shaders");
extern "C" bool RexFnvLateFsiEnabled() { return REXCVAR_GET(fnv_fsi_late_opaque); }
extern "C" bool RexFnvBatchSubmissionsEnabled() { return REXCVAR_GET(fnv_batch_submissions); }
extern "C" bool RexFnvGpuTimestampsEnabled() { return REXCVAR_GET(fnv_gpu_timestamps); }
