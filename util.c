#include "util.h"

int dec_width(int pow2) {
    pow2 -= (pow2 + 9) / 10;
    return 1 + (pow2 / 3);
}

int first_set_bit(size_t val) {
    int pos;
    __asm__("bsf %1, %0" : "=r" (pos) : "rm" (val));
    return pos;
}

