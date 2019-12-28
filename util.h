#ifndef _UTIL_H
#define _UTIL_H

#ifdef __linux__
#include <sys/time.h>
#endif
#include <time.h>
#include <stddef.h>


#define TOSTRING(s) #s

#define CHECK(val) \
    ({    \
        int __retval = (val);   \
        if (__retval == -1) {   \
            vfprintf(stderr, TOSTRING(val) " call failed in " __FILE__ ":"  \
                    TOSTRING(__LINE__) ", reason: %s", strerror(errno)); \
        }   \
        __retval;   \
    })

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


#endif /* _UTIL_H */
