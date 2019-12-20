#include "util.h"

#ifdef __linux__
#include <unistd.h>
#elif __APPLE__
#include <sys/param.h>
#include <sys/sysctl.h>
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

