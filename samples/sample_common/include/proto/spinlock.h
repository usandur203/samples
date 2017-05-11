#ifndef _VIDEO_PROTO_SPINLOCK_H
#define _VIDEO_PROTO_SPINLOCK_H

#ifdef __APPLE__
#include <sched.h>
#include <sys/errno.h>
// This will define PTHREAD_PROCESS_PRIVATE
#include <pthread.h>
typedef int pthread_spinlock_t;

// WARNING: TODO: FIXME: pshared is ignored. Currently we do NOT support SHARED locks.
inline int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
    assert(pshared == PTHREAD_PROCESS_PRIVATE);
    __asm__ __volatile__ ("" ::: "memory");
    *lock = 0;
    return 0;
}

inline int pthread_spin_destroy(pthread_spinlock_t *lock) {
    return 0;
}

inline int pthread_spin_lock(pthread_spinlock_t *lock) {
    while (1) {
        int i;
        for (i=0; i < 10000; i++) {
            if (__sync_bool_compare_and_swap(lock, 0, 1)) {
                return 0;
            }
        }
        sched_yield();
    }
}

inline int pthread_spin_trylock(pthread_spinlock_t *lock) {
    if (__sync_bool_compare_and_swap(lock, 0, 1)) {
        return 0;
    }
    return EBUSY;
}

inline int pthread_spin_unlock(pthread_spinlock_t *lock) {
    __asm__ __volatile__ ("" ::: "memory");
    *lock = 0;
    return 0;
}

#else

#include <pthread.h>

#endif


#endif//_VIDEO_PROTO_SPINLOCK_H
