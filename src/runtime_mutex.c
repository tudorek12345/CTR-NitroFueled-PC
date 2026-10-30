/* Linux pthread-backed opaque PS4 mutex handles. No guest threads yet. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <stdatomic.h>
typedef struct { int type; } GuestAttr;
/* v0.4: destroyed mutexes and condition variables are never freed. Games keep copies of a handle
 * and use or destroy it again after it was destroyed (seen on the European build of CTR: a
 * trylock and a destroy through freed memory inside winpthreads, an access violation); a dead
 * object answers EINVAL instead, as the PS4 does for a destroyed id. */
#define RETIRING UINT32_C(0x80000000)
#define DEAD UINT32_C(0x40000000)
#define USERS_MASK UINT32_C(0x3fffffff)
/* Low bits count host calls in progress. Destruction reserves an idle object before
 * releasing its native handle. Wrapper memory stays allocated for copied guest handles. */
typedef struct { pthread_mutex_t native; uint32_t state; } GuestMutex;
static int retain(uint32_t *state) {
    uint32_t value=__atomic_load_n(state,__ATOMIC_ACQUIRE);
    for (;;) {
        if (value & DEAD) return EINVAL;
        if (value & RETIRING) { value=__atomic_load_n(state,__ATOMIC_ACQUIRE); continue; }
        if ((value & USERS_MASK)==USERS_MASK) return EBUSY;
        if (__atomic_compare_exchange_n(state,&value,value+1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return 0;
    }
}
static void release(uint32_t *state) { __atomic_fetch_sub(state,1,__ATOMIC_RELEASE); }
static int retire(uint32_t *state) {
    uint32_t value=0;
    if (__atomic_compare_exchange_n(state,&value,RETIRING,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return 0;
    return value & DEAD ? EINVAL : EBUSY;
}
static size_t dead_uses;
static int dead_use(const char *what) {
    if (__atomic_fetch_add(&dead_uses,1,__ATOMIC_RELAXED)<5) printf("Runtime: %s on a destroyed handle (EINVAL)\n",what);
    return EINVAL;
}
static _Atomic size_t created, locks, unlocks;
static int32_t orbis_error(int e) {
    if (!e) return 0;
    unsigned code;
    switch (e) {
    case EPERM: code=1; break;
    case ENOMEM: code=12; break;
    case EBUSY: code=16; break;
    case EINVAL: code=22; break;
    case EAGAIN: code=35; break;
    case EDEADLK: code=11; break;
    default: fprintf(stderr, "STOP: unmapped host pthread error %d\n", e); exit(21);
    }
    return (int32_t)(UINT32_C(0x80020000) | code);
}
static ABI int32_t attr_init(GuestAttr **out) {
    if (!out) return orbis_error(EINVAL);
    GuestAttr *attr = malloc(sizeof(*attr));
    if (!attr) return orbis_error(ENOMEM);
    attr->type = 1; *out = attr; return 0;
}
static ABI int32_t attr_type(GuestAttr **attr, int type) {
    if (!attr || !*attr || type < 1 || type > 4) return orbis_error(EINVAL);
    (*attr)->type = type; return 0;
}
/* Priority protocols need a PS4 scheduler; the value is validated and recorded only. */
static ABI int32_t attr_protocol(GuestAttr **attr, int protocol) {
    if (!attr || !*attr || protocol < 0 || protocol > 2) return orbis_error(EINVAL);
    return 0;
}
static ABI int32_t attr_destroy(GuestAttr **attr) {
    if (!attr || !*attr) return orbis_error(EINVAL);
    free(*attr); *attr = NULL; return 0;
}
static ABI int32_t mutex_init(GuestMutex **out, GuestAttr **attr, const char *name) {
    (void)name;
    if (!out || (attr && !*attr)) return orbis_error(EINVAL);
    int type = attr ? (*attr)->type : 1;
    if (type < 1 || type > 4) return orbis_error(EINVAL);
    GuestMutex *mutex = malloc(sizeof(*mutex));
    if (!mutex) return orbis_error(ENOMEM);
    pthread_mutexattr_t native_attr;
    int e = pthread_mutexattr_init(&native_attr);
    if (e) { free(mutex); return orbis_error(e); }
    int native_type = type == 2 ? PTHREAD_MUTEX_RECURSIVE : type == 3 ? PTHREAD_MUTEX_NORMAL : PTHREAD_MUTEX_ERRORCHECK;
    e = pthread_mutexattr_settype(&native_attr, native_type);
    if (!e) e = pthread_mutex_init(&mutex->native, &native_attr);
    pthread_mutexattr_destroy(&native_attr);
    if (e) { free(mutex); return orbis_error(e); }
    mutex->state = 0;
    __atomic_store_n(out,mutex,__ATOMIC_RELEASE); ++created; return 0;
}
static pthread_mutex_t static_init = PTHREAD_MUTEX_INITIALIZER;
/* Statically initialized handles (0/1) are created once, even under contention. */
static int32_t acquire_mutex(GuestMutex **mutex, GuestMutex **held, int empty_error) {
    if (!mutex) return orbis_error(EINVAL);
    for (;;) {
        GuestMutex *object=__atomic_load_n(mutex,__ATOMIC_ACQUIRE);
        if ((uintptr_t)object==2) return orbis_error(EINVAL);
        if ((uintptr_t)object>2) {
            int e=retain(&object->state);
            if (e) return orbis_error(e==EINVAL ? dead_use("mutex use") : e);
            *held=object;
            return 0;
        }
        if (empty_error) return orbis_error(empty_error);
        pthread_mutex_lock(&static_init);
        int32_t e=0;
        if ((uintptr_t)__atomic_load_n(mutex,__ATOMIC_ACQUIRE)<2) e=mutex_init(mutex,NULL,NULL);
        pthread_mutex_unlock(&static_init);
        if (e) return e;
    }
}
static ABI int32_t mutex_lock(GuestMutex **mutex) {
    GuestMutex *held;
    int32_t e = acquire_mutex(mutex,&held,0);
    if (e) return e;
    int r = pthread_mutex_trylock(&held->native);
    if (r == EBUSY) { /* contended: timed for the wait profile */
        const uint64_t start = runtime_wait_clock();
        r = pthread_mutex_lock(&held->native);
        runtime_wait_note(1, runtime_wait_clock() - start);
    }
    release(&held->state);
    e = orbis_error(r);
    if (!e) ++locks;
    return e;
}
static ABI int32_t mutex_trylock(GuestMutex **mutex) {
    GuestMutex *held;
    int32_t e = acquire_mutex(mutex,&held,0);
    if (e) return e;
    e = orbis_error(pthread_mutex_trylock(&held->native));
    release(&held->state);
    if (!e) ++locks;
    return e;
}
static ABI int32_t mutex_unlock(GuestMutex **mutex) {
    GuestMutex *held;
    int32_t e=acquire_mutex(mutex,&held,EPERM);
    if (e) return e;
    e = orbis_error(pthread_mutex_unlock(&held->native));
    release(&held->state);
    if (!e) ++unlocks;
    return e;
}
static ABI int32_t mutex_destroy(GuestMutex **mutex) {
    if (!mutex) return orbis_error(EINVAL);
    GuestMutex *held=__atomic_load_n(mutex,__ATOMIC_ACQUIRE);
    if ((uintptr_t)held==2) return orbis_error(EINVAL);
    if ((uintptr_t)held<2) return 0;
    int e=retire(&held->state);
    if (e) return orbis_error(e==EINVAL ? dead_use("mutex destroy") : e);
    e = pthread_mutex_destroy(&held->native);
    __atomic_store_n(&held->state,e ? 0 : DEAD,__ATOMIC_RELEASE);
    if (!e) __atomic_compare_exchange_n(mutex,&held,(GuestMutex *)(uintptr_t)2,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
    return orbis_error(e);
}
static int deadline_after(struct timespec *end, uint64_t usec) {
    if (clock_gettime(CLOCK_REALTIME, end)) return errno;
    uint64_t ns = (uint64_t)end->tv_nsec + (usec % 1000000) * 1000;
    end->tv_sec += (time_t)(usec / 1000000 + ns / 1000000000);
    end->tv_nsec = (long)(ns % 1000000000);
    return 0;
}
static int32_t timed_error(int e) { return e == ETIMEDOUT ? (int32_t)UINT32_C(0x8002003c) : orbis_error(e); }
static ABI int32_t mutex_timedlock(GuestMutex **mutex, uint32_t usec) {
    struct timespec end;
    if (deadline_after(&end, usec)) return orbis_error(EINVAL);
    GuestMutex *held;
    int32_t e = acquire_mutex(mutex,&held,0);
    if (e) return e;
    e = timed_error(pthread_mutex_timedlock(&held->native, &end));
    release(&held->state);
    if (!e) ++locks;
    return e;
}
/* Condition variables: opaque handles over host pthread_cond (CLOCK_REALTIME,
 * like FreeBSD's default), sharing the host mutex behind GuestMutex. */
typedef struct { pthread_cond_t native; uint32_t state; } GuestCond;
static _Atomic size_t conds, waits, wakeups;
static ABI int32_t cond_init(GuestCond **out, void **attr, const char *name) {
    (void)attr; (void)name;
    if (!out) return orbis_error(EINVAL);
    GuestCond *c = malloc(sizeof(*c));
    if (!c) return orbis_error(ENOMEM);
    int e = pthread_cond_init(&c->native, NULL);
    if (e) { free(c); return orbis_error(e); }
    c->state = 0;
    __atomic_store_n(out,c,__ATOMIC_RELEASE); ++conds; return 0;
}
static int32_t acquire_cond(GuestCond **cond, GuestCond **held) {
    if (!cond) return orbis_error(EINVAL);
    for (;;) {
        GuestCond *object=__atomic_load_n(cond,__ATOMIC_ACQUIRE);
        if ((uintptr_t)object==2) return orbis_error(EINVAL);
        if ((uintptr_t)object>2) {
            int e=retain(&object->state);
            if (e) return orbis_error(e==EINVAL ? dead_use("cond use") : e);
            *held=object;
            return 0;
        }
        pthread_mutex_lock(&static_init);
        int32_t e=0;
        if ((uintptr_t)__atomic_load_n(cond,__ATOMIC_ACQUIRE)<2) e=cond_init(cond,NULL,NULL);
        pthread_mutex_unlock(&static_init);
        if (e) return e;
    }
}
static ABI int32_t cond_destroy(GuestCond **cond) {
    if (!cond) return orbis_error(EINVAL);
    GuestCond *held=__atomic_load_n(cond,__ATOMIC_ACQUIRE);
    if ((uintptr_t)held==2) return orbis_error(EINVAL);
    if ((uintptr_t)held<2) return 0;
    int e=retire(&held->state);
    if (e) return orbis_error(e==EINVAL ? dead_use("cond destroy") : e);
    e = pthread_cond_destroy(&held->native);
    __atomic_store_n(&held->state,e ? 0 : DEAD,__ATOMIC_RELEASE);
    if (!e) __atomic_compare_exchange_n(cond,&held,(GuestCond *)(uintptr_t)2,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
    return orbis_error(e);
}
static ABI int32_t cond_wait(GuestCond **cond, GuestMutex **mutex) {
    GuestCond *c;
    GuestMutex *m;
    int32_t e = acquire_cond(cond,&c);
    if (e) return e;
    e=acquire_mutex(mutex,&m,EINVAL);
    if (e) { release(&c->state); return e; }
    ++waits;
    const uint64_t start = runtime_wait_clock();
    const int e2 = pthread_cond_wait(&c->native, &m->native);
    release(&m->state); release(&c->state);
    runtime_wait_note(0, runtime_wait_clock() - start);
    return orbis_error(e2);
}
static int32_t cond_wait_until(GuestCond **cond, GuestMutex **mutex, const struct timespec *end) {
    if (!end) return orbis_error(EINVAL);
    GuestCond *c;
    GuestMutex *m;
    int32_t e = acquire_cond(cond,&c);
    if (e) return e;
    e=acquire_mutex(mutex,&m,EINVAL);
    if (e) { release(&c->state); return e; }
    ++waits;
    const uint64_t start = runtime_wait_clock();
    const int e2 = pthread_cond_timedwait(&c->native, &m->native, end);
    release(&m->state); release(&c->state);
    runtime_wait_note(0, runtime_wait_clock() - start);
    return timed_error(e2);
}
static ABI int32_t cond_timedwait(GuestCond **cond, GuestMutex **mutex, uint32_t usec) {
    struct timespec end;
    if (deadline_after(&end, usec)) return orbis_error(EINVAL);
    return cond_wait_until(cond, mutex, &end);
}
static ABI int32_t cond_signal(GuestCond **cond) {
    GuestCond *held;
    int32_t e = acquire_cond(cond,&held);
    if (e) return e;
    ++wakeups; e=orbis_error(pthread_cond_signal(&held->native));
    release(&held->state); return e;
}
static ABI int32_t cond_broadcast(GuestCond **cond) {
    GuestCond *held;
    int32_t e = acquire_cond(cond,&held);
    if (e) return e;
    ++wakeups; e=orbis_error(pthread_cond_broadcast(&held->native));
    release(&held->state); return e;
}
/* PS4 struct timespec is {int64 sec, int64 nsec}, identical to Linux x86-64. */
typedef struct { int64_t sec, nsec; } GuestTimespec;
static ABI int32_t cond_abs_timedwait(GuestCond **cond, GuestMutex **mutex, const GuestTimespec *abs) {
    if (!abs || abs->nsec < 0 || abs->nsec >= 1000000000) return orbis_error(EINVAL);
    struct timespec end = {.tv_sec = (time_t)abs->sec, .tv_nsec = (long)abs->nsec};
    return cond_wait_until(cond, mutex, &end);
}
/* POSIX pthread APIs return positive guest errno values, unlike scePthread. */
static int32_t posix_result(int32_t result) { return result ? (int32_t)((uint32_t)result & 0xffff) : 0; }
static ABI int32_t posix_attr_init(GuestAttr **out) { return posix_result(attr_init(out)); }
static ABI int32_t posix_attr_type(GuestAttr **attr,int type) { return posix_result(attr_type(attr,type)); }
static ABI int32_t posix_attr_destroy(GuestAttr **attr) { return posix_result(attr_destroy(attr)); }
static ABI int32_t posix_mutex_init(GuestMutex **out,GuestAttr **attr) { return posix_result(mutex_init(out,attr,NULL)); }
static ABI int32_t posix_mutex_destroy(GuestMutex **m) { return posix_result(mutex_destroy(m)); }
static ABI int32_t posix_mutex_lock(GuestMutex **m) { return posix_result(mutex_lock(m)); }
static ABI int32_t posix_mutex_trylock(GuestMutex **m) { return posix_result(mutex_trylock(m)); }
static ABI int32_t posix_mutex_unlock(GuestMutex **m) { return posix_result(mutex_unlock(m)); }
static ABI int32_t posix_cond_init(GuestCond **c, void **a) { return posix_result(cond_init(c, a, NULL)); }
static ABI int32_t posix_cond_destroy(GuestCond **c) { return posix_result(cond_destroy(c)); }
static ABI int32_t posix_cond_wait(GuestCond **c, GuestMutex **m) { return posix_result(cond_wait(c, m)); }
static ABI int32_t posix_cond_timedwait(GuestCond **c, GuestMutex **m, const GuestTimespec *t) { return posix_result(cond_abs_timedwait(c, m, t)); }
static ABI int32_t posix_cond_signal(GuestCond **c) { return posix_result(cond_signal(c)); }
static ABI int32_t posix_cond_broadcast(GuestCond **c) { return posix_result(cond_broadcast(c)); }
uintptr_t runtime_mutex_resolve(const char *name) {
    if (runtime_nid_equals(name,"0TyVk4MSLt0")) return (uintptr_t)posix_cond_init;
    if (runtime_nid_equals(name,"RXXqi4CtF8w")) return (uintptr_t)posix_cond_destroy;
    if (runtime_nid_equals(name,"Op8TBGY5KHg")) return (uintptr_t)posix_cond_wait;
    if (runtime_nid_equals(name,"27bAgiJmOh0")) return (uintptr_t)posix_cond_timedwait;
    if (runtime_nid_equals(name,"2MOy+rUfuhQ")) return (uintptr_t)posix_cond_signal;
    if (runtime_nid_equals(name,"mkx2fVhNMsg")) return (uintptr_t)posix_cond_broadcast;
    if (runtime_nid_equals(name,"2Tb92quprl0#libkernel")) return (uintptr_t)cond_init;
    if (runtime_nid_equals(name,"g+PZd2hiacg#libkernel")) return (uintptr_t)cond_destroy;
    if (runtime_nid_equals(name,"WKAXJ4XBPQ4#libkernel")) return (uintptr_t)cond_wait;
    if (runtime_nid_equals(name,"BmMjYxmew1w#libkernel")) return (uintptr_t)cond_timedwait;
    if (runtime_nid_equals(name,"kDh-NfxgMtE#libkernel")) return (uintptr_t)cond_signal;
    if (runtime_nid_equals(name,"JGgj7Uvrl+A#libkernel")) return (uintptr_t)cond_broadcast;
    if (runtime_nid_equals(name,"IafI2PxcPnQ#libkernel")) return (uintptr_t)mutex_timedlock;
    if (runtime_nid_equals(name,"dQHWEsJtoE4")) return (uintptr_t)posix_attr_init;
    if (runtime_nid_equals(name,"mDmgMOGVUqg")) return (uintptr_t)posix_attr_type;
    if (runtime_nid_equals(name,"HF7lK46xzjY")) return (uintptr_t)posix_attr_destroy;
    if (runtime_nid_equals(name,"ttHNfU+qDBU")) return (uintptr_t)posix_mutex_init;
    if (runtime_nid_equals(name,"ltCfaGr2JGE")) return (uintptr_t)posix_mutex_destroy;
    if (runtime_nid_equals(name,"7H0iTOciTLo")) return (uintptr_t)posix_mutex_lock;
    if (runtime_nid_equals(name,"K-jXhbt2gn4")) return (uintptr_t)posix_mutex_trylock;
    if (runtime_nid_equals(name,"2Z+PpY6CaJg")) return (uintptr_t)posix_mutex_unlock;
    if (runtime_nid_equals(name,"F8bUHwAG284#libkernel")) return (uintptr_t)attr_init;
    if (runtime_nid_equals(name,"iMp8QpE+XO4#libkernel")) return (uintptr_t)attr_type;
    if (runtime_nid_equals(name,"smWEktiyyG0#libkernel")) return (uintptr_t)attr_destroy;
    if (runtime_nid_equals(name,"1FGvU0i9saQ#libkernel")) return (uintptr_t)attr_protocol;
    if (runtime_nid_equals(name,"cmo1RIYva9o#libkernel")) return (uintptr_t)mutex_init;
    if (runtime_nid_equals(name,"9UK1vLZQft4#libkernel")) return (uintptr_t)mutex_lock;
    if (runtime_nid_equals(name,"upoVrzMHFeE#libkernel")) return (uintptr_t)mutex_trylock;
    if (runtime_nid_equals(name,"tn3VlD0hG60#libkernel")) return (uintptr_t)mutex_unlock;
    if (runtime_nid_equals(name,"2Of0f+3mhhE#libkernel")) return (uintptr_t)mutex_destroy;
    return 0;
}
void runtime_mutex_report(void) {
    printf("Runtime: mutexes created=%zu, locks=%zu, unlocks=%zu\n", created, locks, unlocks);
    printf("Runtime: condition variables created=%zu, waits=%zu, wakeups=%zu\n", conds, waits, wakeups);
}

