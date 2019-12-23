#ifdef __linux__
#define _GNU_SOURCE
#include <sched.h>

#elif __APPLE__
#include <mach/mach_init.h>
#include <mach/thread_policy.h>

// forward declarations for functions which are for some reason commented out
// in mach/thread_policy.h
kern_return_t   thread_policy_set(
                    thread_t                thread,
                    thread_policy_flavor_t  flavor,
                    thread_policy_t	        policy_info,
                    mach_msg_type_number_t  count);

kern_return_t   thread_policy_get(
                    thread_t                thread,
                    thread_policy_flavor_t  flavor,
                    thread_policy_t         policy_info,
                    mach_msg_type_number_t  *count,
                    boolean_t               *get_default);

#define AFFINITY_TAG_OFFSET 0x80

#endif

#include <pthread.h>
#include <stdlib.h>

#include "mt.h"
#include "util.h"
#include "vprint.h"


int pthread_setaffinity(pthread_t thread, size_t cpu) {
#ifdef __linux__
    cpu_set_t cpu_set;

    CPU_ZERO(&cpu_set);
    CPU_SET(cpu, &cpu_set);

    return pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpu_set);

#elif __APPLE__

    thread_port_t mthread = pthread_mach_thread_np(thread);
    thread_affinity_policy_data_t policy = { AFFINITY_TAG_OFFSET + cpu };
    kern_return_t ret = thread_policy_set(mthread, THREAD_AFFINITY_POLICY,
            (thread_policy_t) &policy, THREAD_AFFINITY_POLICY_COUNT);
    if (ret != KERN_SUCCESS) {
        fprintf(stderr, "Thread policy set failed, returned %d\n", ret);
    }
    return ret;
#endif
}

size_t pthread_getaffinity(pthread_t thread) {
#ifdef __linux__
    cpu_set_t cpu_set;

    CPU_ZERO(&cpu_set);
    pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpu_set);
    for (size_t i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, &cpu_set)) {
            return i;
        }
    }
    return 0;
#elif __APPLE__

    thread_port_t mthread = pthread_mach_thread_np(thread);
    thread_affinity_policy_data_t policy;
    mach_msg_type_number_t cnt = THREAD_AFFINITY_POLICY_COUNT;
    boolean_t get_default;
    kern_return_t ret = thread_policy_get(mthread, THREAD_AFFINITY_POLICY,
            (thread_policy_t) &policy, &cnt, &get_default);
    if (ret != KERN_SUCCESS) {
        fprintf(stderr, "Thread policy get failed, returned %d\n", ret);
    }
    return policy.affinity_tag;

#endif
}



struct thread_info {
    void *(*start_routine) (void *);
    void *arg;
    int flags;
    int thread_id;
};

static void* thread_init(void* data) {
    struct thread_info *info = (struct thread_info *) data;
    void *(*start_routine) (void *) = info->start_routine;
    struct mt_args arg = {
        .arg = info->arg,
        .thread_id = info->thread_id
    };

    if (info->flags & MT_PARTITION) {
        pthread_setaffinity(pthread_self(), arg.thread_id);
    }

    free(info);
    // now that memory cleanup has completed, allow cancellation
    // of this thread
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    return start_routine(&arg);
}


int init_mt_context(struct mt_context *context, size_t n_threads,
        void *(*start_routine) (void *), void *arg, int options) {
    struct thread_info *ti;
    pthread_attr_t attr;
    size_t i;
    int err, ncpus;
    pthread_t *threads;
#ifdef __linux__
    cpu_set_t cpu_aff;
#endif

    if (options & MT_PARTITION) {
        ncpus = get_n_cpus();
        if (n_threads != ncpus) {
            fprintf(stderr, "Number of threads (%lu) must equal number of "
                    "logical processing units (%d) if MT_PARTITION is set\n",
                    n_threads, ncpus);
            return -1;
        }
    }

    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);

    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    // initially, make sure no threads can be disabled so that
    // the memory cleanup performed in the thread init function
    // is guaranteed to occur
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    // one of the "threads" to run the routine will be the main process
    n_threads--;

    threads = (pthread_t*) malloc(n_threads * sizeof(pthread_t));
    if (threads == NULL) {
        fprintf(stderr, "Unable to malloc space for %lu threads\n", n_threads);
        return -1;
    }

    for (i = 0; i < n_threads; i++) {
        // is freed in thread init function
        ti = (struct thread_info *) malloc(sizeof(struct thread_info));
        ti->start_routine = start_routine;
        ti->arg = arg;
        ti->flags = options;
        ti->thread_id = ((int) i) + 1;

#ifdef __linux__
        CPU_ZERO(&cpu_aff);
        CPU_SET(ti->thread_id, &cpu_aff);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpu_aff);
#endif

        if ((err = pthread_create(&threads[i], &attr,
                        &thread_init, ti)) != 0) {
            fprintf(stderr, "Unable to spawn thread %lu, reason: %s\n",
                    i, strerror(err));
            // clean up threads that were already created
            while (i > 0) {
                i--;
                pthread_cancel(threads[i]);
                pthread_join(threads[i], NULL);
            }
            free(context->threads);
            pthread_attr_destroy(&attr);
            return -1;
        }
    }
    pthread_attr_destroy(&attr);

    context->threads = threads;
    context->n_threads = n_threads;

    ti = (struct thread_info *) malloc(sizeof(struct thread_info));
    ti->start_routine = start_routine;
    ti->arg = arg;
    ti->flags = options;
    ti->thread_id = 0;

#ifdef __linux__
    pthread_setaffinity(pthread_self(), 0);
#endif
    thread_init(ti);
    return 0;
}


void exit_mt_routine(struct mt_context *context) {
    size_t i;
    pthread_t *threads = context->threads;

    for (i = 0; i < context->n_threads; i++) {
        pthread_cancel(threads[i]);
    }
    for (i = 0; i < context->n_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    if (context->threads != NULL) {
        free(context->threads);
    }
}

