#include "util.h"

#ifdef __linux__
#include <unistd.h>
#elif __APPLE__
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

int dec_width(int pow2) {
    pow2 -= (pow2 + 9) / 10;
    return 1 + (pow2 / 3);
}

/*
 * credit: https://stackoverflow.com/questions/150355/programmatically-find-the-number-of-cores-on-a-machine
 */
int get_n_cpus() {
#ifdef __linux__
    return sysconf(_SC_NPROCESSORS_ONLN);
#elif __APPLE__
    int nm[2];
    size_t len = 4;
    uint32_t count;

    nm[0] = CTL_HW;
    nm[1] = HW_AVAILCPU;
    sysctl(nm, 2, &count, &len, NULL, 0);

    if (count < 1) {
        nm[1] = HW_NCPU;
        sysctl(nm, 2, &count, &len, NULL, 0);
        if (count < 1) {
            count = 1;
        }
    }
    return count;
#else
    return -1;
#endif
}


double timespec_diff(struct timespec *t1, struct timespec *t0) {
    return ((1000000000LU * t1->tv_sec + t1->tv_nsec) -
            (1000000000LU * t0->tv_sec + t0->tv_nsec)) / 1000000000.;
}


#ifdef __APPLE__
// from https://opensource.apple.com/source/sudo/sudo-46/src/memrchr.c

/*
 * Reverse memchr()
 * Find the last occurrence of 'c' in the buffer 's' of size 'n'.
 */
void *
memrchr(s, c, n)
    const void *s;
    int c;
    size_t n;
{
    const unsigned char *cp;

    if (n != 0) {
	cp = (unsigned char *)s + n;
	do {
	    if (*(--cp) == (unsigned char)c)
		return((void *)cp);
	} while (--n != 0);
    }
    return((void *)0);
}

#endif

