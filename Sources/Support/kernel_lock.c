#include "Support.h"

//===----------------------------------------------------------------------===//
// Runtime V33 atomic and spinlock substrate.
//
// This deliberately exposes a tiny C-owned synchronization surface before any
// Swift task dispatch moves across cores. The implementation uses clang/gcc
// atomic builtins, which lower to the target's acquire/release exclusive-access
// sequence on AArch64 without pulling in heap state.
//===----------------------------------------------------------------------===//

void kernel_atomic_full_barrier(void) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

unsigned int kernel_atomic_load_u32(unsigned int *ptr) {
    if (ptr == 0) {
        return 0;
    }
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

void kernel_atomic_store_u32(unsigned int *ptr, unsigned int value) {
    if (ptr == 0) {
        return;
    }
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

unsigned int kernel_atomic_fetch_add_u32(unsigned int *ptr, unsigned int value) {
    if (ptr == 0) {
        return 0;
    }
    return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

unsigned long kernel_atomic_fetch_add_u64(unsigned long *ptr, unsigned long value) {
    if (ptr == 0) {
        return 0;
    }
    return __atomic_fetch_add(ptr, value, __ATOMIC_SEQ_CST);
}

unsigned int kernel_atomic_compare_exchange_u32(unsigned int *ptr,
                                                unsigned int expected,
                                                unsigned int desired) {
    if (ptr == 0) {
        return 0;
    }
    unsigned int local_expected = expected;
    return __atomic_compare_exchange_n(ptr, &local_expected, desired, 0,
                                       __ATOMIC_ACQUIRE,
                                       __ATOMIC_RELAXED) ? 1U : 0U;
}

void kernel_spinlock_init(kernel_spinlock_t *lock) {
    if (lock == 0) {
        return;
    }
    kernel_atomic_store_u32(&lock->state, 0);
    __atomic_store_n(&lock->acquisitions, 0UL, __ATOMIC_RELEASE);
    __atomic_store_n(&lock->contentions, 0UL, __ATOMIC_RELEASE);
}

unsigned int kernel_spinlock_try_lock(kernel_spinlock_t *lock) {
    if (lock == 0) {
        return 0;
    }
    if (kernel_atomic_compare_exchange_u32(&lock->state, 0, 1) == 0) {
        return 0;
    }
    kernel_atomic_fetch_add_u64(&lock->acquisitions, 1);
    return 1;
}

void kernel_spinlock_lock(kernel_spinlock_t *lock) {
    if (lock == 0) {
        return;
    }
    while (kernel_spinlock_try_lock(lock) == 0) {
        kernel_atomic_fetch_add_u64(&lock->contentions, 1);
        while (kernel_atomic_load_u32(&lock->state) != 0) {
            nop();
        }
    }
}

void kernel_spinlock_unlock(kernel_spinlock_t *lock) {
    if (lock == 0) {
        return;
    }
    kernel_atomic_store_u32(&lock->state, 0);
}

unsigned long kernel_spinlock_acquisition_count(const kernel_spinlock_t *lock) {
    if (lock == 0) {
        return 0;
    }
    return __atomic_load_n(&lock->acquisitions, __ATOMIC_ACQUIRE);
}

unsigned long kernel_spinlock_contention_count(const kernel_spinlock_t *lock) {
    if (lock == 0) {
        return 0;
    }
    return __atomic_load_n(&lock->contentions, __ATOMIC_ACQUIRE);
}

int kernel_atomic_selftest(void) {
    if (KERNEL_ATOMIC_VERSION != 33U) {
        return 0;
    }

    unsigned int value32 = 0;
    unsigned long value64 = 0;
    kernel_atomic_store_u32(&value32, 1);
    unsigned int loaded = kernel_atomic_load_u32(&value32);
    unsigned int before32 = kernel_atomic_fetch_add_u32(&value32, 2);
    unsigned int cas_ok = kernel_atomic_compare_exchange_u32(&value32, 3, 7);
    unsigned int cas_fail = kernel_atomic_compare_exchange_u32(&value32, 3, 9);
    unsigned long before64 = kernel_atomic_fetch_add_u64(&value64, 5);
    kernel_atomic_full_barrier();

    return loaded == 1 &&
        before32 == 1 &&
        value32 == 7 &&
        cas_ok == 1 &&
        cas_fail == 0 &&
        before64 == 0 &&
        value64 == 5 ? 1 : 0;
}

int kernel_spinlock_selftest(void) {
    if (KERNEL_LOCK_VERSION != 33U) {
        return 0;
    }

    kernel_spinlock_t lock;
    unsigned int guarded = 0;
    kernel_spinlock_init(&lock);
    if (kernel_spinlock_try_lock(&lock) == 0) {
        return 0;
    }
    if (kernel_spinlock_try_lock(&lock) != 0) {
        return 0;
    }
    guarded = 33;
    kernel_spinlock_unlock(&lock);
    kernel_spinlock_lock(&lock);
    guarded++;
    kernel_spinlock_unlock(&lock);

    return guarded == 34 &&
        kernel_atomic_load_u32(&lock.state) == 0 &&
        kernel_spinlock_acquisition_count(&lock) == 2 &&
        kernel_spinlock_contention_count(&lock) == 0 ? 1 : 0;
}
