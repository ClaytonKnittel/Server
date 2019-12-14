#include <stdarg.h>
#include <stdio.h>
#include "vprint.h"

int vlevel = V1;

int _vprintf(const char * restrict format, ...) {
    va_list arg;
    int ret;

    if (vlevel == V0) {
        ret = 0;
    }
    else {
        va_start(arg, format);
        ret = printf(format, arg);
        va_end(arg);
    }

    return ret;
}

int _vfprintf(FILE *stream, const char * restrict format, ...) {
    va_list arg;
    int ret;

    if (vlevel == V0) {
        ret = 0;
    }
    else {
        va_start(arg, format);
        ret = fprintf(stream, format, arg);
        va_end(arg);
    }

    return ret;
}

int _dbg_printf(const char * restrict format, ...) {
    va_list arg;
    int ret;

    if (vlevel == V2) {
        va_start(arg, format);
        ret = printf(format, arg);
        va_end(arg);
    }
    else {
        ret = 0;
    }

    return ret;
}
