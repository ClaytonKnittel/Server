#include <pthread.h>
#include <string.h>

struct mt_context {
    pthread_t *threads;
    size_t n_threads;
};

#define MT_PARTITION 0x1
#define MT_SYNC_BARRIER 0x2


__inline void clear_mt_context(struct mt_context *context) {
    memset(context, 0, sizeof(struct mt_context));
}

/*
 * initializes the specified number of threads, all with callback start_routine
 * and argument arg, and populates the mt_context struct passed in. Each of
 * the created threads begins execution if they are all created successfully
 *
 * Options:
 *  MT_PARTITION: sets the CPU affinities of each thread so that they are
 *      evenly distributed across the available processors
 *  MT_SYNC_BARRIER: only begins execution of each thread once every thread
 *      has been successfully created. Can be used to ensure that if the
 *      initialization fails, none of the threads will have done any processing
 *
 * returns 0 on success and -1 on failure
 */
int init_mt_context(struct mt_context *context, size_t n_threads,
        void *(*start_routine) (void *), void *arg, int options);


/*
 * exits and joins each of the threads
 */
void exit_mt_routine(struct mt_context *context);

