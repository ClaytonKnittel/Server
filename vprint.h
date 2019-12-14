#ifndef __VPRINT_H
#define __VPRINT_H

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
#else
#define vprintf(...) _vprintf(__VA_ARGS__)
#define vfprintf(...) _vfprintf(__VA_ARGS__)
#define dbg_printf(...) _dbg_printf(__VA_ARGS__)
#endif


int _vprintf(const char * restrict format, ...);
int _vfprintf(FILE *stream, const char * restrict format, ...);

int _dbg_printf(const char * restrict format, ...);

#endif /* __VPRINT_H */
