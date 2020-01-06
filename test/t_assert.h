#ifndef _T_ASSERT_H
#define _T_ASSERT_H

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "../src/vprint.h"
#include "t_assert.h"


#ifdef VERBOSE
#define v_ensure(...) __VA_ARGS__
#else
#define v_ensure(...)
#endif


#define announce(...) { \
    v_ensure(fprintf(stderr, P_CYAN "Run:\t" P_RESET #__VA_ARGS__ "\n")); \
    __VA_ARGS__; \
}

#define assert(actual, expect) { \
    _assert((actual), (expect), P_YELLOW "Assert:\t" P_RESET #actual, __LINE__); \
}

#define assert_neq(actual, expect) { \
    _assert_neq((actual), (expect), P_YELLOW "Assert:\t" P_RESET #actual, __LINE__); \
}

void _assert(long actual, long expect, const char msg[], int linenum) {
    if (actual != expect) {
        fprintf(stderr, P_CYAN "Line %d: " P_RED "Failed " P_RESET "%s\n"
                "\tGot %ld\tExpect %ld\n", linenum, msg, actual, expect);
        exit(1);
    }
}

void _assert_neq(long actual, long expect, const char msg[], int linenum) {
    if (actual == expect) {
        fprintf(stderr, P_CYAN "Line %d: " P_RED "Failed " P_RESET "%s\n"
                "\tGot %ld\tExpect not %ld\n", linenum, msg, actual, expect);
        exit(1);
    }
}

void silence_stdout() {
    int fd = open("/dev/null", O_WRONLY);
    if (fd == -1) {
        printf("Unable to silence stdout!\n");
        return;
    }
    dup2(fd, STDOUT_FILENO);
}

#endif /* T_ASSERT_H */
