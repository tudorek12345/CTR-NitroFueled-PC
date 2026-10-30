#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static atomic_int pause_trylock, entered_trylock, resume_trylock;
static pthread_mutex_t *paused_native;
static int intercepted_trylock(pthread_mutex_t *mutex);
#define pthread_mutex_trylock intercepted_trylock
#include "../src/runtime_mutex.c"
#undef pthread_mutex_trylock

void runtime_wait_note(int kind, uint64_t ns) { (void)kind; (void)ns; }

static void yield_thread(void) {
    const struct timespec delay={0,1000000};
    nanosleep(&delay,NULL);
}
static void await(atomic_int *flag) {
    const uint64_t deadline=runtime_wait_clock()+UINT64_C(5000000000);
    while (!atomic_load(flag)) { assert(runtime_wait_clock()<deadline); yield_thread(); }
}
static int intercepted_trylock(pthread_mutex_t *mutex) {
    if (mutex==paused_native && atomic_exchange(&pause_trylock,0)) {
        atomic_store(&entered_trylock,1);
        await(&resume_trylock);
    }
    return pthread_mutex_trylock(mutex);
}
static void *lock_worker(void *arg) {
    GuestMutex *alias=arg;
    assert(mutex_lock(&alias)==0);
    assert(mutex_unlock(&alias)==0);
    return NULL;
}
static void destroy_during_lock(void) {
    GuestMutex *mutex=NULL;
    assert(mutex_init(&mutex,NULL,"lifetime-race")==0);
    paused_native=&mutex->native;
    atomic_store(&pause_trylock,1);
    atomic_store(&entered_trylock,0);
    atomic_store(&resume_trylock,0);
    pthread_t worker;
    assert(pthread_create(&worker,NULL,lock_worker,mutex)==0);
    await(&entered_trylock);
    // A host call has started, but has not touched the native mutex yet.
    // The old code destroys it here; the fixed code must keep it alive.
    assert((uint32_t)mutex_destroy(&mutex)==0x80020010);
    atomic_store(&resume_trylock,1);
    assert(pthread_join(worker,NULL)==0);
    paused_native=NULL;
    GuestMutex *copy=mutex;
    assert(mutex_destroy(&mutex)==0);
    assert((uint32_t)mutex_lock(&copy)==0x80020016);
    assert((uint32_t)mutex_trylock(&copy)==0x80020016);
    assert((uint32_t)mutex_unlock(&copy)==0x80020016);
    assert((uint32_t)mutex_destroy(&copy)==0x80020016);
    assert((uint32_t)mutex_lock(&mutex)==0x80020016);
}

static GuestMutex *waiting_mutex;
static GuestCond *waiting_cond;
static atomic_int waiting, predicate;
static void *wait_worker(void *unused) {
    (void)unused;
    assert(mutex_lock(&waiting_mutex)==0);
    atomic_store(&waiting,1);
    while (!atomic_load(&predicate)) assert(cond_timedwait(&waiting_cond,&waiting_mutex,3000000)==0);
    assert(mutex_unlock(&waiting_mutex)==0);
    return NULL;
}
static void destroy_during_wait(void) {
    assert(mutex_init(&waiting_mutex,NULL,"waiting")==0);
    assert(cond_init(&waiting_cond,NULL,"waiting")==0);
    pthread_t worker;
    assert(pthread_create(&worker,NULL,wait_worker,NULL)==0);
    await(&waiting);
    // Acquiring it proves the waiter released the native mutex inside cond_wait.
    assert(mutex_lock(&waiting_mutex)==0);
    assert((uint32_t)cond_destroy(&waiting_cond)==0x80020010);
    assert((uint32_t)mutex_destroy(&waiting_mutex)==0x80020010);
    atomic_store(&predicate,1);
    assert(cond_signal(&waiting_cond)==0);
    assert(mutex_unlock(&waiting_mutex)==0);
    assert(pthread_join(worker,NULL)==0);
    GuestCond *copy=waiting_cond;
    assert(cond_destroy(&waiting_cond)==0);
    assert((uint32_t)cond_signal(&copy)==0x80020016);
    assert((uint32_t)cond_broadcast(&copy)==0x80020016);
    assert((uint32_t)cond_signal(&waiting_cond)==0x80020016);
    assert(mutex_destroy(&waiting_mutex)==0);
}

static GuestMutex *contended;
static atomic_int stop_contention;
static void *contention_worker(void *unused) {
    (void)unused;
    while (!atomic_load(&stop_contention)) {
        GuestMutex *copy=__atomic_load_n(&contended,__ATOMIC_ACQUIRE);
        int32_t e=mutex_trylock(&copy);
        if (!e) assert(mutex_unlock(&copy)==0);
        else assert((uint32_t)e==0x80020010 || (uint32_t)e==0x80020016);
    }
    return NULL;
}
static void contended_reinitialization(void) {
    assert(mutex_init(&contended,NULL,"contention")==0);
    pthread_t workers[4];
    for (int i=0;i<4;++i) assert(pthread_create(&workers[i],NULL,contention_worker,NULL)==0);
    for (int i=0;i<5000;++i) {
        int32_t e=mutex_destroy(&contended);
        if (!e) assert(mutex_init(&contended,NULL,"contention")==0);
        else assert((uint32_t)e==0x80020010);
    }
    atomic_store(&stop_contention,1);
    for (int i=0;i<4;++i) assert(pthread_join(workers[i],NULL)==0);
    assert(mutex_destroy(&contended)==0);
}

int main(void) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    destroy_during_lock();
    destroy_during_wait();
    contended_reinitialization();
    GuestMutex *mutex=NULL;
    assert(mutex_lock(&mutex)==0);
    assert((uint32_t)mutex_lock(&mutex)==0x8002000b);
    assert((uint32_t)mutex_trylock(&mutex)==0x80020010);
    assert((uint32_t)mutex_destroy(&mutex)==0x80020010);
    assert(mutex_unlock(&mutex)==0 && mutex_destroy(&mutex)==0);
    GuestAttr *attr=NULL;
    assert(attr_init(&attr)==0 && attr_type(&attr,2)==0);
    assert(mutex_init(&mutex,&attr,"recursive")==0 && attr_destroy(&attr)==0);
    assert(mutex_lock(&mutex)==0 && mutex_trylock(&mutex)==0);
    assert((uint32_t)mutex_destroy(&mutex)==0x80020010);
    assert(mutex_unlock(&mutex)==0 && mutex_unlock(&mutex)==0);
    assert(mutex_destroy(&mutex)==0);
    puts("PASS: lock/destroy race, condition-wait lifetime, four-thread contention, stale/static/recursive mutexes");
    return 0;
}
