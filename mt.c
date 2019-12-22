#include <stdlib.h>

#include "mt.h"
#include "vprint.h"



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
    int err;
    pthread_t *threads;

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
    printf("Freed threads\n");

    if (context->threads != NULL) {
        free(context->threads);
    }
}

