#include <pthread.h>
#include <string.h>

struct mt_context {
    pthread_t *threads;
    size_t n_threads;
};


/*
 * argument struct passed to the initialization function in create_mt_context.
 * arg is the same as the arg passed into that method, and thread_id is a
 * unique identifier given to that thread, with the main thread being given
 * an id of 0
 */
struct mt_args {
    void *arg;
    int thread_id;
};


/*
 * provides a portable way of setting the cpu affinity of a thread. On Linux,
 * this will bind the thread to the given cpu. On MacOS, this will give the
 * thread the affinity tag cpu
 */
int pthread_setaffinity(pthread_t thread, size_t cpu);

size_t pthread_getaffinity(pthread_t thread);


#define MT_PARTITION 0x1
#define MT_SYNC_BARRIER 0x2


static __inline void clear_mt_context(struct mt_context *context) {
    __builtin_memset(context, 0, sizeof(struct mt_context));
}


/*
 * initializes the specified number of threads, all with callback start_routine
 * and argument arg, and populates the mt_context struct passed in. Each of
 * the created threads begins execution if they are all created successfully
 *
 * Options:
 *  MT_PARTITION: sets the CPU affinities of each thread so that they are
 *      evenly distributed across the available processors. Note: if this
 *      flag is set, then n_threads must equal the number of logical cpus
 *      on this machine (get_n_cpus() in util.h)
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

