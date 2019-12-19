#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "../vprint.h"
#include "../dmsg.h"


//#define VERBOSE

#ifdef VERBOSE
#define v_ensure(...) __VA_ARGS__
#else
#define v_ensure(...)
#endif


#define announce(...) { \
    v_ensure(fprintf(stderr, P_CYAN "Run:\t" P_RESET #__VA_ARGS__ "\n")); \
    __VA_ARGS__; \
}

#define assert(...) { \
    _assert((__VA_ARGS__), P_YELLOW "Assert:\t" P_RESET #__VA_ARGS__ "\n"); \
}

#define assert_false(...) { \
    _assert(!(__VA_ARGS__), P_YELLOW "Assert:\t!" P_RESET #__VA_ARGS__ "\n"); \
}

static void _assert(int ret, const char msg[]) {
    if (!ret) {
        fprintf(stderr, P_RED "Failed " P_RESET "%s\n", msg);
        exit(1);
    }
}

static void silence_stdout() {
    int fd = open("/dev/null", O_WRONLY);
    dup2(fd, STDOUT_FILENO);
}

int main() {
    dmsg_list list;
    int i;

    silence_stdout();

    // test invalid numbers as initial node size
    // not square
    assert(dmsg_init2(&list, 3) != 0);
    dmsg_free(&list);
    // zero not allowed
    assert(dmsg_init2(&list, 0) != 0);
    dmsg_free(&list);
    // not square
    for (i = 17; i < 32; i++) {
        assert(dmsg_init2(&list, i) != 0);
        dmsg_free(&list);
    }

    // test writing
    {
        char msg1[] = "four";
        char msg2[] = "eight___";


        assert(dmsg_init2(&list, 4) == 0);
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len == 0);
        assert(list.list_size == 1);

        announce(dmsg_append(&list, msg1, sizeof(msg1) - 1));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len == sizeof(msg1) - 1);
        assert(list.list_size == 2);
        assert(memcmp(msg1, list.list[0].msg, sizeof(msg1) - 1) == 0);

        announce(dmsg_append(&list, msg2, sizeof(msg2) - 1));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len == sizeof(msg1) - 1 + sizeof(msg2) - 1);
        assert(list.list_size == 3);
        assert(memcmp(msg1, list.list[0].msg, sizeof(msg1) - 1) == 0);
        assert(memcmp(msg2, list.list[1].msg, sizeof(msg2) - 1) == 0);

        dmsg_free(&list);
    }

    {
        char msg1[] = "test message 1!";

        assert(dmsg_init2(&list, 2) == 0);

        announce(dmsg_append(&list, msg1, sizeof(msg1) - 1));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len == sizeof(msg1) - 1);
        assert(list.list_size == 4);

        dmsg_free(&list);
        
    }

    {
#define SIZE 1024
        char *msg = (char*) malloc(SIZE + 1);
        msg[SIZE] = '\0';

        size_t rem = SIZE;
        for (int count = 0; rem > 0; count++) {
            size_t wsize = 8LU << count;
            wsize = wsize > rem ? rem : wsize;
            memset(msg + (SIZE - rem), 'a' + count, wsize);
            rem -= wsize;
        }

        // make one big write
        assert(dmsg_init2(&list, 8) == 0);

        announce(dmsg_append(&list, msg, SIZE));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len == SIZE);
        assert(list.list_size == 8);

        dmsg_free(&list);

        // make many small writes
        assert(dmsg_init2(&list, 8) == 0);

        size_t counts[] = {7LU, 18LU, 32LU, 62LU, 2LU, 384LU, 511LU, 8LU};
        size_t offset = 0;
        for (int i = 0; i < sizeof(counts) / sizeof(size_t); i++) {
            dmsg_append(&list, msg + offset, counts[i]);
            offset += counts[i];
            assert(list.len == offset);
        }
        assert(offset == SIZE);
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len == SIZE);
        assert(list.list_size == 8);

        dmsg_free(&list);
    }

    fprintf(stderr, P_GREEN "All dmsg_list tests passed" P_RESET "\n");

    return 0;
}

