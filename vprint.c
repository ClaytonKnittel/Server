#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "vprint.h"

#define UNLOCKED 1
#define LOCKED 0

#undef vprintf
#undef vfprintf

int vlevel = V1;

static int sio_print_lock = UNLOCKED;

int _vprintf(const char * restrict format, ...) {
    va_list arg;
    int ret;

    if (vlevel == V0) {
        ret = 0;
    }
    else {
        va_start(arg, format);
        ret = vprintf(format, arg);
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
        ret = vfprintf(stream, format, arg);
        va_end(arg);
    }

    return ret;
}

int _dbg_printf(const char * restrict format, ...) {
    va_list arg;
    int ret;

    if (vlevel == V2) {
        va_start(arg, format);
        ret = vprintf(format, arg);
        va_end(arg);
    }
    else {
        ret = 0;
    }

    return ret;
}

int _sio_print(const char str[]) {
    return write(STDOUT_FILENO, str, strlen(str));
}

int _sio_fprint(int fd, const char str[]) {
    return write(fd, str, strlen(str));
}
