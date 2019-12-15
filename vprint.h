#ifndef __VPRINT_H
#define __VPRINT_H

/**
 * This must be included after stdio.h, if it is
 * to be included elsewhere. However, stdio.h is
 * included here, so it need not be included
 * wherever this is needed
 */

#include <stdio.h>

#define V0 0
#define V1 1
#define V2 2


// verbosity level
// V0: no printing
// V1: normal printing v*printf
// V2: print dbg_printf too
extern int vlevel;

/**
 * if prints are to be optimized out, then define QUIET
 */

#ifdef QUIET

#define vprintf(...)
#define vfprintf(...)
#define dbg_printf(...)

#define sio_print(...)
#define sio_fprint(...)

#else

#define vprintf(...) _vprintf(__VA_ARGS__)
#define vfprintf(...) _vfprintf(__VA_ARGS__)
#define dbg_printf(...) _dbg_printf(__VA_ARGS__)

#define sio_print(str) _sio_print(str);
#define sio_fprint(file, str) _sio_fprint(file, str);

#endif


int _vprintf(const char * restrict format, ...);
int _vfprintf(FILE *stream, const char * restrict format, ...);

int _dbg_printf(const char * restrict format, ...);

int _sio_print(const char str[]);
int _sio_fprint(int fd, const char str[]);

#endif /* __VPRINT_H */
