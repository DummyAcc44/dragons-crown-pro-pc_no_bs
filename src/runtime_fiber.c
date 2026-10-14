/* libSceFiber on Win32 fibers (adapted from HayatoG/gr2_pc). Fibers share their thread's TEB, so the
 * guest TCB slot stays per thread as on the PS4. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define FIBER_MAGIC UINT64_C(0x5245424946434e44) /* "DCPFIBER" */
#define FIBER_OPT_MAGIC UINT32_C(0xbb40e64d)
#define FIBER_ERR_NULL ((int32_t)0x80590001)
#define FIBER_ERR_ALIGNMENT ((int32_t)0x80590002)
#define FIBER_ERR_INVALID ((int32_t)0x80590004)
#define FIBER_ERR_PERMISSION ((int32_t)0x80590005)
#define FIBER_ERR_STATE ((int32_t)0x80590006)
/* Guest code has no stack probes, so the whole stack is committed up front; host runtime calls
 * made from a fiber need more room than the game's own context size. */
#define FIBER_MIN_STACK (256u * 1024u)

typedef void (ABI *FiberEntry)(uint64_t arg_on_initialize, uint64_t arg_on_run);
typedef struct HostFiber {
    void *fiber, *guest;
    FiberEntry entry;
    uint64_t arg_on_initialize, arg_to_deliver;
    uint64_t *resume_out; /* where this fiber's own Switch/ReturnToThread stores its next argOnRun */
    int started, running;
    char name[32];
} HostFiber;
/* The guest SceFiber is opaque to the game; its first 16 bytes hold the magic and the host object. */
typedef struct { uint64_t magic; HostFiber *host; } GuestFiber;
static __thread struct {
    void *thread_fiber;
    HostFiber *current;
    uint64_t *thread_return_out;
} fiber_tls;
static unsigned long fibers_created, fiber_runs, fiber_switches;

static HostFiber *fiber_of(void *guest) {
    GuestFiber *g = guest;
    return g && g->magic == FIBER_MAGIC ? g->host : NULL;
}
static void WINAPI fiber_trampoline(void *param) {
    HostFiber *f = param;
    f->entry(f->arg_on_initialize, f->arg_to_deliver);
    fprintf(stderr, "STOP: fiber %s returned from its entry function\n", f->name);
    fflush(NULL);
    _exit(22);
}
static ABI int32_t opt_param_initialize(uint32_t *param) {
    if (!param) return FIBER_ERR_NULL;
    if ((uintptr_t)param & 7) return FIBER_ERR_ALIGNMENT;
    *param = FIBER_OPT_MAGIC;
    return 0;
}
static ABI int32_t fiber_initialize(void *guest, const char *name, FiberEntry entry, uint64_t arg_on_initialize,
                                    void *context, uint64_t context_size, const void *option, uint32_t build_version) {
    (void)context; (void)option; (void)build_version;
    if (!guest || !entry) return FIBER_ERR_NULL;
    if ((uintptr_t)guest & 7) return FIBER_ERR_ALIGNMENT;
    HostFiber *f = calloc(1, sizeof(*f));
    if (!f) return FIBER_ERR_INVALID;
    f->guest = guest; f->entry = entry; f->arg_on_initialize = arg_on_initialize;
    snprintf(f->name, sizeof(f->name), "%s", name ? name : "");
    SIZE_T size = context_size < FIBER_MIN_STACK ? FIBER_MIN_STACK : (SIZE_T)context_size;
    f->fiber = CreateFiberEx(size, size, 0, fiber_trampoline, f);
    if (!f->fiber) { free(f); return FIBER_ERR_INVALID; }
    ((GuestFiber *)guest)->magic = FIBER_MAGIC;
    ((GuestFiber *)guest)->host = f;
    if (__atomic_fetch_add(&fibers_created, 1, __ATOMIC_RELAXED) == 0) printf("Runtime: first fiber '%s', context %llu bytes\n", f->name, (unsigned long long)context_size);
    return 0;
}
static ABI int32_t fiber_finalize(void *guest) {
    HostFiber *f = fiber_of(guest);
    if (!guest) return FIBER_ERR_NULL;
    if (!f) return FIBER_ERR_INVALID;
    if (f->running) return FIBER_ERR_STATE;
    ((GuestFiber *)guest)->magic = 0;
    DeleteFiber(f->fiber);
    free(f);
    return 0;
}
static void fiber_enter(HostFiber *f, uint64_t arg) {
    if (f->started && f->resume_out) *f->resume_out = arg;
    f->arg_to_deliver = arg;
    f->started = 1; f->running = 1;
    fiber_tls.current = f;
}
static ABI int32_t fiber_run(void *guest, uint64_t arg_on_run_to, uint64_t *arg_on_return) {
    HostFiber *f = fiber_of(guest);
    if (!guest) return FIBER_ERR_NULL;
    if (!f) return FIBER_ERR_INVALID;
    if (fiber_tls.current) return FIBER_ERR_PERMISSION; /* only a thread, not a fiber, runs a fiber */
    if (f->running) return FIBER_ERR_STATE;
    if (!fiber_tls.thread_fiber) {
        fiber_tls.thread_fiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(NULL);
        if (!fiber_tls.thread_fiber) return FIBER_ERR_INVALID;
    }
    fiber_tls.thread_return_out = arg_on_return;
    __atomic_add_fetch(&fiber_runs, 1, __ATOMIC_RELAXED);
    fiber_enter(f, arg_on_run_to);
    SwitchToFiber(f->fiber);
    fiber_tls.current = NULL;
    return 0;
}
static ABI int32_t fiber_switch(void *guest, uint64_t arg_on_run_to, uint64_t *arg_on_run) {
    HostFiber *to = fiber_of(guest), *from = fiber_tls.current;
    if (!guest) return FIBER_ERR_NULL;
    if (!to) return FIBER_ERR_INVALID;
    if (!from) return FIBER_ERR_PERMISSION;
    if (to->running) return FIBER_ERR_STATE;
    from->resume_out = arg_on_run; from->running = 0;
    __atomic_add_fetch(&fiber_switches, 1, __ATOMIC_RELAXED);
    fiber_enter(to, arg_on_run_to);
    SwitchToFiber(to->fiber);
    return 0;
}
static ABI int32_t fiber_return_to_thread(uint64_t arg_on_return, uint64_t *arg_on_run) {
    HostFiber *from = fiber_tls.current;
    if (!from) return FIBER_ERR_PERMISSION;
    if (fiber_tls.thread_return_out) *fiber_tls.thread_return_out = arg_on_return;
    from->resume_out = arg_on_run; from->running = 0;
    fiber_tls.current = NULL;
    SwitchToFiber(fiber_tls.thread_fiber);
    return 0;
}
static ABI int32_t fiber_get_self(void **out) {
    if (!out) return FIBER_ERR_NULL;
    if (!fiber_tls.current) return FIBER_ERR_PERMISSION;
    *out = fiber_tls.current->guest;
    return 0;
}

static const RuntimeExport exports[] = {
    {"sceFiberOptParamInitialize", opt_param_initialize}, {"_sceFiberInitializeImpl", fiber_initialize},
    {"sceFiberFinalize", fiber_finalize}, {"sceFiberRun", fiber_run}, {"sceFiberSwitch", fiber_switch},
    {"sceFiberReturnToThread", fiber_return_to_thread}, {"sceFiberGetSelf", fiber_get_self},
};
uintptr_t runtime_fiber_resolve(const char *name) { return RUNTIME_LOOKUP(exports, name); }
void runtime_fiber_report(void) {
    printf("Runtime: fibers created=%lu, runs=%lu, switches=%lu\n", fibers_created, fiber_runs, fiber_switches);
}
