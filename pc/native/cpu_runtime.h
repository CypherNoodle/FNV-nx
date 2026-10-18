#ifndef FNV_PC_CPU_RUNTIME_H
#define FNV_PC_CPU_RUNTIME_H
#include "pe_image.h"

typedef void (*fnv_cpu_function)(void);
#define FNV_CPU_MAX_NATIVE_METHODS 1024u
#define FNV_CPU_NATIVE_METHOD_BASE 0xFE010000u
struct fnv_cpu_runtime;
enum fnv_cpu_result {
    FNV_CPU_OK, FNV_CPU_INVALID, FNV_CPU_BUSY, FNV_CPU_NO_MEMORY,
    FNV_CPU_NOT_ATTACHED, FNV_CPU_FAULTED, FNV_CPU_GUEST_FAULT,
    FNV_CPU_THREAD_EXIT
};
enum fnv_cpu_fault_kind {
    FNV_CPU_UNIMPLEMENTED=1, FNV_CPU_UNKNOWN_TARGET, FNV_CPU_NOT_CODE,
    FNV_CPU_BAD_MEMORY, FNV_CPU_CPUID_UNAVAILABLE, FNV_CPU_CLOCK_UNAVAILABLE,
    FNV_CPU_CLOCK_BACKWARDS, FNV_CPU_PEER_FAULT
};
/* Matches the external SDK's current model, not a complete architectural
 * EFLAGS/x87-80/MXCSR/SEH implementation. GPR order: x86 encoding order. */
struct fnv_cpu_state {
    uint32_t gpr[8], fs_base, seh_ebp;
    double x87[8];
    int32_t x87_top, direction, fp_compare;
    uint16_t fp_control, fp_status;
    unsigned char xmm[8][16];
    uint64_t mmx[8];
};
struct fnv_cpu_fault {
    enum fnv_cpu_fault_kind kind;
    uint32_t entry, address, detail, trace_index;
    uint64_t call_count;
    uint32_t trace[16];
    struct fnv_cpu_state state;
    char text[96];
};
/* These providers must implement an audited guest CPU/clock contract.
 * No ARM feature passthrough, guessed tick frequency or successful fallback. */
struct fnv_cpu_config {
    unsigned char *memory;
    uint32_t guest_base, memory_bytes, code_low, code_high;
    void *context;
    int (*cpuid)(void *, uint32_t leaf, uint32_t subleaf, uint32_t out[4]);
    int (*timestamp)(void *, uint64_t *guest_ticks);
    uint64_t timestamp_hz;
    void (*diagnostic)(void *, const struct fnv_cpu_fault *);
};
struct fnv_cpu_export {
    const char *dll, *name;
    uint16_t ordinal;
    int by_ordinal;
    fnv_cpu_function function; /* Guest stack/register ABI, not a Win32 host ABI. */
};

/* One immutable address mapping per process; lifecycle operations are locked,
 * guest execution is not. Destroy only after all guest threads detach. */
enum fnv_cpu_result fnv_cpu_create(const struct fnv_cpu_config *, struct fnv_cpu_runtime **);
enum fnv_cpu_result fnv_cpu_destroy(struct fnv_cpu_runtime *);
/* Prepared native methods, separate from the IAT. Atomic registration before
 * the first attach; no hot replacement or token reuse. Unregister only after
 * all threads detach. Caller serializes module setup/teardown with attach. */
enum fnv_cpu_result fnv_cpu_register_native(struct fnv_cpu_runtime *,const fnv_cpu_function *,size_t count,uint32_t *tokens);
enum fnv_cpu_result fnv_cpu_unregister_native(struct fnv_cpu_runtime *,uint32_t first_token,size_t count);
enum fnv_cpu_result fnv_cpu_prepare_memory(struct fnv_cpu_runtime *,uint32_t address,size_t bytes,void **);
struct fnv_cpu_runtime *fnv_cpu_current(void);
/* Attached thread only: canonical static import token for a bound bridge,
 * or zero if this function was not imported. No registration or mutation. */
uint32_t fnv_cpu_import_token(fnv_cpu_function);
/* Nonblocking health probe for native waits; caller retains the runtime. */
int fnv_cpu_is_faulted(const struct fnv_cpu_runtime *);
/* Host lifecycle failures outside execute can poison the session and report
 * their diagnostic. Caller retains runtime; does not unwind/preempt peers. */
void fnv_cpu_invalidate(struct fnv_cpu_runtime *,const struct fnv_cpu_fault *);
/* One successful static-IAT bind before attaching threads. Failure preserves
 * IAT and active registry. No DLL loading or implicit API implementation. */
enum fnv_native_result fnv_cpu_bind_imports(struct fnv_cpu_runtime *,
    struct fnv_native_image *, struct fnv_native_import *scratch, size_t capacity,
    const struct fnv_cpu_export *, size_t exports, size_t *count);
void fnv_cpu_state_init(struct fnv_cpu_state *, uint32_t esp_value, uint32_t fs_value);
enum fnv_cpu_result fnv_cpu_attach(struct fnv_cpu_runtime *, const struct fnv_cpu_state *);
enum fnv_cpu_result fnv_cpu_detach(void);
enum fnv_cpu_result fnv_cpu_get_state(struct fnv_cpu_state *);
enum fnv_cpu_result fnv_cpu_set_state(const struct fnv_cpu_state *);
/* Caller supplies a prepared guest call frame (including return VA). Does not
 * push a fictitious return address, run an EXE entry, or catch host OS faults.
 * An unsupported operation unwinds generated C to this boundary and poisons
 * the session. Other threads stop at entry/return boundaries; there is no
 * asynchronous preemption of an already running guest loop. No reentrant call. */
enum fnv_cpu_result fnv_cpu_execute(uint32_t entry, struct fnv_cpu_fault *);
/* Nonfaulting control-flow unwind to the current execute boundary. EAX holds
 * the exit code; caller must detach/complete its thread. Never under a lock. */
_Noreturn void fnv_cpu_exit_thread(uint32_t code);
/* Bounded flat-arena helpers for future import handlers. Generated MEM macros
 * remain unchecked: these helpers are not a guest VirtualAlloc/SEH subsystem. */
void *fnv_cpu_memory(uint32_t address, size_t bytes);
/* Bounds-only nonfaulting probe for bridges that return an HRESULT. Page
 * permissions and the lifetime of a region still require a VM/provider. */
void *fnv_cpu_try_memory(uint32_t address,size_t bytes);
uint32_t fnv_cpu_argument_u32(unsigned index);
void fnv_cpu_return_u32(uint32_t value, uint32_t argument_bytes);
#endif
