#define _GNU_SOURCE

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


#ifdef __APPLE__

// from Linux include/sched.h

#ifdef __LP64__
#define CPU_SETSIZE 1024
#else
#define CPU_SETSIZE 32
#endif

#define __CPU_BITTYPE   unsigned long int
#define __CPU_BITS      (8 * sizeof(__CPU_BITTYPE))
#define __CPU_ELT(x)    ((x) / __CPU_BITS)
#define __CPU_MASK(x)   ((__CPU_BITTYPE) 1 << ((x) & (__CPU_BITS - 1)))

typedef struct {
    __CPU_BITTYPE   __bits[ CPU_SETSIZE / __CPU_BITS ];
} cpu_set_t;

#define CPU_ZERO(set)   __builtin_memset(set, 0, sizeof(cpu_set_t));

#define CPU_SET(cpu, set) \
    do { \
        size_t __cpu = (cpu); \
        if (__cpu < 8 * sizeof(cpu_set_t)) { \
            (set)->__bits[__CPU_ELT(__cpu)] |= __CPU_MASK(__cpu); \
        } \
    } while (0)

#define CPU_CLR(cpu, set) \
    do { \
        size_t __cpu = (cpu); \
        if (__cpu < 8 * sizeof(cpu_set_t)) { \
            (set)->__bits[__CPU_ELT(__cpu)] &= ~__CPU_MASK(__cpu); \
        } \
    } while (0)

#define CPU_ISSET(cpu, set) \
    ((((size_t) (cpu)) < 8 * sizeof(cpu_set_t)) \
        ? ((set)->__bits[__CPU_ELT(__cpu)] & __CPU_MASK(__cpu)) != 0 \
        : 0)

#endif


/*
 * provides a portable way of setting the cpu affinity of a thread. On Linux,
 * this will bind the thread to the given cpu. On MacOS, this will give the
 * thread the affinity tag cpu
 */
int pthread_setaffinity(pthread_t thread, size_t cpu);

size_t pthread_getaffinity(pthread_t thread);


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

