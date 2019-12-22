#include <stdlib.h>

#include "mt.h"
#include "vprint.h"


int init_mt_context(struct mt_context *context, size_t n_threads,
        void *(*start_routine) (void *), void *arg, int options) {
    size_t i;
    int err;
    pthread_t *threads;

    printf("initializing mt context\n");

    // one of the "threads" to run the routine will be the main process
    n_threads--;

    threads = (pthread_t*) malloc(n_threads * sizeof(pthread_t));
    if (threads == NULL) {
        fprintf(stderr, "Unable to malloc space for %lu threads\n", n_threads);
        return -1;
    }

    for (i = 0; i < n_threads; i++) {
        if ((err = pthread_create(&threads[i], NULL,
                        start_routine, arg)) != 0) {
            fprintf(stderr, "Unable to spawn thread %lu, reason: %s\n",
                    i, strerror(err));
            // clean up threads that were already created
            while (i > 0) {
                i--;
                pthread_cancel(threads[i]);
                pthread_join(threads[i], NULL);
            }
            free(context->threads);
            return -1;
        }
    }

    printf("done with init\n");

    context->n_threads = n_threads - 1;
    context->threads = threads;

    start_routine(arg);
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

    free(context->threads);
}

