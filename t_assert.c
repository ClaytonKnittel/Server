#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "vprint.h"
#include "t_assert.h"

void _assert(int actual, int expect, const char msg[], int linenum) {
    if (actual != expect) {
        fprintf(stderr, P_CYAN "Line %d: " P_RED "Failed " P_RESET "%s\n"
                "\tGot %d\tExpect %d\n", linenum, msg, actual, expect);
        exit(1);
    }
}

void _assert_neq(int actual, int expect, const char msg[], int linenum) {
    if (actual == expect) {
        fprintf(stderr, P_CYAN "Line %d: " P_RED "Failed " P_RESET "%s\n"
                "\tGot %d\tExpect not %d\n", linenum, msg, actual, expect);
        exit(1);
    }
}

void silence_stdout() {
    int fd = open("/dev/null", O_WRONLY);
    dup2(fd, STDOUT_FILENO);
}

