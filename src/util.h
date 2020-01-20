#ifndef _UTIL_H
#define _UTIL_H

#ifdef __linux__
#include <sys/time.h>
#endif
#include <time.h>
#include <stddef.h>

#include "vprint.h"

#define TOSTRING(s) #s

#define CHECK(val) \
    ({    \
        int __retval = (val);   \
        if (__retval == -1) {   \
            vfprintf(stderr, TOSTRING(val) " call failed in " __FILE__ ":%d"  \
                    ", reason: %s", __LINE__, strerror(errno)); \
        }   \
        __retval;   \
    })


#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

// rounds up to the nearest 2^order
#define ROUND_UP_ORD(val, order) \
    (((val) + (((order) << 1LU) - 1)) & ~(((order) << 1LU) - 1))


#define PTR_ADD(ptr, amt) \
    ((void*) (((char*) (ptr)) + amt))

/*
#define malloc(size) \
    ({ \
        void* ptr = malloc(size); \
        printf(P_GREEN "malloccing %p, " __FILE__ ":%d" P_RESET "\n", ptr, __LINE__); \
        ptr; \
    })
#define calloc(ptr, s, t) \
    { \
        void* ptr = calloc(s, t); \
        printf(P_GREEN "calloccing %p, " __FILE__ ":%d" P_RESET "\n", ptr, __LINE__); \
        ptr; \
    }
#define free(ptr) \
    { \
        printf(P_RED "Freeing %p, " __FILE__ ":%d" P_RESET "\n", (ptr), __LINE__); \
        free(ptr); \
    }
*/




// gives the character width of the decimal representation of
// the number 2 ^ pow2, where '^' is the power operation
int dec_width(int pow2);

// gives the first set bit of val, assuming val is not zero
static int __inline first_set_bit(size_t val) {
    size_t pos;
    __asm__("bsf %1, %0" : "=r" (pos) : "rm" (val));
    return (int) pos;
}

// gives the last set bit of val, assuming val is not zero
static int __inline last_set_bit(size_t val) {
    size_t pos;
    __asm__("bsr %1, %0" : "=r" (pos) : "rm" (val));
    return (int) pos;
}

// gives the number of logical cores on this machine
int get_n_cpus();

// calculates the number of seconds between t1 and t0 (t1 - t0)
double timespec_diff(struct timespec *t1, struct timespec *t0);

// true if timespec t1 occurs after t2
static __inline int timespec_after(struct timespec *t1, struct timespec *t2) {
    return (t1->tv_sec == t2->tv_sec) ? t1->tv_nsec > t2->tv_nsec :
        t1->tv_sec > t2->tv_sec;
}


#ifdef __APPLE__
// need to define memrchr for mac systems

// from https://opensource.apple.com/source/sudo/sudo-46/src/memrchr.c

/*
 * Reverse memchr()
 * Find the last occurrence of 'c' in the buffer 's' of size 'n'.
 */
void *memrchr(const void *s, int c, size_t n);

#endif


#endif /* _UTIL_H */
