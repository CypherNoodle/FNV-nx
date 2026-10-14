#include <rex/cvar.h>
REXCVAR_DEFINE_BOOL(fnv_batch_submissions, false, "FNV",
                   "Experimental primary-buffer batching; flush before command waits and presentation");
REXCVAR_DEFINE_BOOL(fnv_gpu_timestamps, false, "FNV",
                   "Sparse nonblocking GPU timestamp groups; diagnostic overhead may affect timing");
extern "C" bool RexFnvBatchSubmissionsEnabled() { return REXCVAR_GET(fnv_batch_submissions); }
extern "C" bool RexFnvGpuTimestampsEnabled() { return REXCVAR_GET(fnv_gpu_timestamps); }
