#include "Support.h"

// Runtime V28 Swift runtime dependency audit.
//
// This file is not trying to inspect the Mach-O from inside the kernel. It
// records the fixed source-owned runtime surface so the shell can report it,
// while runtime-audit.sh proves the load-bearing linked symbols with llvm-nm.
// Frozen V28 counts:
// KERNEL_RUNTIME_AUDIT_VERSION 28U
// KERNEL_RUNTIME_OWNED_HOOK_COUNT 10U
// KERNEL_RUNTIME_HEAP_SHIM_COUNT 5U
// KERNEL_RUNTIME_AUDIT_REQUIRED_SYMBOL_COUNT 5U

static const char *source_owned_hooks[KERNEL_RUNTIME_OWNED_HOOK_COUNT] = {
    "swift_task_enqueueGlobalImpl",
    "swift_task_enqueueMainExecutorImpl",
    "swift_task_enqueueGlobalWithDelayImpl",
    "swift_task_enqueueGlobalWithDeadlineImpl",
    "swift_task_getMainExecutorImpl",
    "swift_task_isMainExecutorImpl",
    "swift_task_checkIsolatedImpl",
    "swift_task_isIsolatingCurrentContextImpl",
    "swift_task_donateThreadToGlobalExecutorUntilImpl",
    "swift_task_asyncMainDrainQueueImpl",
};

static const char *linked_hooks[KERNEL_RUNTIME_LINKED_HOOK_COUNT] = {
    "swift_task_enqueueGlobalImpl",
    "swift_task_asyncMainDrainQueueImpl",
};

static const char *source_heap_shims[KERNEL_RUNTIME_HEAP_SHIM_COUNT] = {
    "malloc",
    "free",
    "calloc",
    "realloc",
    "posix_memalign",
};

static const char *linked_heap_shims[KERNEL_RUNTIME_LINKED_HEAP_SHIM_COUNT] = {
    "malloc",
    "free",
    "posix_memalign",
};

static const char *required_linked_symbols[KERNEL_RUNTIME_AUDIT_REQUIRED_SYMBOL_COUNT] = {
    "swift_task_enqueueGlobalImpl",
    "swift_task_asyncMainDrainQueueImpl",
    "malloc",
    "free",
    "posix_memalign",
};

unsigned int kernel_runtime_audit_version(void) {
    return KERNEL_RUNTIME_AUDIT_VERSION;
}

unsigned int kernel_runtime_source_hook_count(void) {
    return KERNEL_RUNTIME_OWNED_HOOK_COUNT;
}

unsigned int kernel_runtime_linked_hook_count(void) {
    return KERNEL_RUNTIME_LINKED_HOOK_COUNT;
}

unsigned int kernel_runtime_heap_shim_count(void) {
    return KERNEL_RUNTIME_HEAP_SHIM_COUNT;
}

unsigned int kernel_runtime_linked_heap_shim_count(void) {
    return KERNEL_RUNTIME_LINKED_HEAP_SHIM_COUNT;
}

unsigned int kernel_runtime_required_symbol_count(void) {
    return KERNEL_RUNTIME_AUDIT_REQUIRED_SYMBOL_COUNT;
}

unsigned int kernel_runtime_audit_selftest(void) {
    return source_owned_hooks[0] != 0
        && linked_hooks[0] != 0
        && source_heap_shims[0] != 0
        && linked_heap_shims[0] != 0
        && required_linked_symbols[0] != 0
        && KERNEL_RUNTIME_OWNED_HOOK_COUNT == 10U
        && KERNEL_RUNTIME_LINKED_HOOK_COUNT == 2U
        && KERNEL_RUNTIME_HEAP_SHIM_COUNT == 5U
        && KERNEL_RUNTIME_LINKED_HEAP_SHIM_COUNT == 3U
        && KERNEL_RUNTIME_AUDIT_REQUIRED_SYMBOL_COUNT == 5U;
}
