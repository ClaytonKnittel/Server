#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "../vprint.h"
#include "../dmsg.h"

//#define VERBOSE
#include "../t_assert.h"


// attempts to first write list to a temporary file,
// then reads back from it and compares it to what is
// in the list
static void test_write_read(dmsg_list *list) {
    int fd = open(
#ifndef O_TMPFILE
#define TMP_NAME "test/.temp.txt"
            TMP_NAME, O_CREAT |
#else
            "test/", O_TMPFILE |
#endif
            O_RDWR, S_IRUSR | S_IWUSR);

    if (fd == -1) {
        fprintf(stderr, "Unable to create file, reason: %s\n", strerror(errno));
        exit(1);
    }

    assert(dmsg_write(list, fd), 0);

    lseek(fd, 0, SEEK_SET);

    char *buf = (char*) malloc(list->len + 1);
    buf[list->len] = '\0';

    assert(read(fd, buf, list->len), list->len);
    v_ensure(dmsg_print(list, STDERR_FILENO));
    v_ensure(fprintf(stderr, "From file:\t%s\n", buf));

    char *buf2 = (char*) malloc(list->len + 1);
    buf2[list->len] = '\0';

    assert(dmsg_cpy(list, buf2), 0);

    v_ensure(fprintf(stderr, "From dmsg_cpy:\t%s\n", buf2));

    assert(strcmp(buf, buf2), 0);

    close(fd);
#ifndef O_TMPFILE
    unlink(TMP_NAME);
#undef TMP_NAME
#endif
}

int main() {
    dmsg_list list;
    int i;

    silence_stdout();

    // test invalid numbers as initial node size
    // not square
    assert_neq(dmsg_init2(&list, 3), 0);
    // zero not allowed
    assert_neq(dmsg_init2(&list, 0), 0);
    // not square
    for (i = 17; i < 32; i++) {
        assert_neq(dmsg_init2(&list, i), 0);
    }

    // test writing
    {
        char msg1[] = "four";
        char msg2[] = "eight___";


        assert(dmsg_init2(&list, 4), 0);
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, 0);
        assert(list.list_size, 1);

        announce(dmsg_append(&list, msg1, sizeof(msg1) - 1));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, sizeof(msg1) - 1);
        assert(list.list_size, 2);
        assert(memcmp(msg1, list.list[0].msg, sizeof(msg1) - 1), 0);

        announce(dmsg_append(&list, msg2, sizeof(msg2) - 1));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, sizeof(msg1) - 1 + sizeof(msg2) - 1);
        assert(list.list_size, 3);
        assert(memcmp(msg1, list.list[0].msg, sizeof(msg1) - 1), 0);
        assert(memcmp(msg2, list.list[1].msg, sizeof(msg2) - 1), 0);

        test_write_read(&list);

        dmsg_free(&list);
    }

    {
        char msg1[] = "test message 1!";

        assert(dmsg_init2(&list, 2), 0);

        announce(dmsg_append(&list, msg1, sizeof(msg1) - 1));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, sizeof(msg1) - 1);
        assert(list.list_size, 4);

        test_write_read(&list);

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
        assert(dmsg_init2(&list, 8), 0);

        announce(dmsg_append(&list, msg, SIZE));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, SIZE);
        assert(list.list_size, 8);

        test_write_read(&list);

        dmsg_free(&list);

        // make many small writes
        assert(dmsg_init2(&list, 8), 0);

        size_t counts[] = {7LU, 18LU, 32LU, 62LU, 2LU, 384LU, 511LU, 8LU};
        size_t offset = 0;
        for (int i = 0; i < sizeof(counts) / sizeof(size_t); i++) {
            dmsg_append(&list, msg + offset, counts[i]);
            offset += counts[i];
            assert(list.len, offset);
        }
        assert(offset, SIZE);
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, SIZE);
        assert(list.list_size, 8);

        test_write_read(&list);

        dmsg_free(&list);
#undef SIZE
    }

    // testing file reading
    {
        char msg1[] = "four";
        char msg2[] = "mor__romextra";

        int fd = open(
#ifndef O_TMPFILE
#define TMP_NAME "test/.input.txt"
                TMP_NAME, O_CREAT | O_TRUNC |
#else
                "test/", O_TMPFILE |
#endif
                O_RDWR, S_IRUSR | S_IWUSR);

        // dmsg_read
        assert(dmsg_init2(&list, 4), 0);

        write(fd, msg1, sizeof(msg1) - 1);
        lseek(fd, 0, SEEK_SET);
        assert(dmsg_read(&list, fd), sizeof(msg1) - 1);

        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, sizeof(msg1) - 1);
        assert(list.list_size, 2);
        assert(memcmp(list.list[0].msg, msg1, sizeof(msg1) - 1), 0);

        write(fd, msg2, sizeof(msg2) - 1);
        lseek(fd, sizeof(msg1) - 1, SEEK_SET);
        assert(dmsg_read(&list, fd), sizeof(msg2) - 1);

        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, sizeof(msg1) - 1 + sizeof(msg2) - 1);
        assert(list.list_size, 3);
        assert(memcmp(list.list[0].msg, msg1, sizeof(msg1) - 1), 0);
        assert(memcmp(list.list[1].msg, msg2, list.list[1].size), 0);
        assert(memcmp(list.list[2].msg, msg2 + list.list[1].size, list.list[2].size), 0);

        dmsg_free(&list);

        // dmsg_read_n
        assert(dmsg_init2(&list, 4), 0);

        // erase contents of file
        ftruncate(fd, 0);
        lseek(fd, 0, SEEK_SET);
        write(fd, msg1, sizeof(msg1) - 1);
        write(fd, msg2, sizeof(msg2) - 1);
        lseek(fd, 0, SEEK_SET);

        assert(dmsg_read_n(&list, fd, 3), 3);

        v_ensure(fprintf(stderr, "Write 3 bytes\n"));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, 3);
        assert(list.list_size, 1);
        assert(memcmp(list.list[0].msg, msg1, 3), 0);

        assert(dmsg_read_n(&list, fd, 2), 2);

        v_ensure(fprintf(stderr, "Write 5 bytes\n"));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, 5);
        assert(list.list_size, 2);
        assert(memcmp(list.list[0].msg, msg1, 4), 0);
        assert(memcmp(list.list[1].msg, msg2, 1), 0);

        assert(dmsg_read_n(&list, fd, 6), 6);

        v_ensure(fprintf(stderr, "Write 6 bytes\n"));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, 11);
        assert(list.list_size, 2);
        assert(memcmp(list.list[0].msg, msg1, 4), 0);
        assert(memcmp(list.list[1].msg, msg2, 7), 0);

        // try reading in 1 more byte than remains
        assert(dmsg_read_n(&list, fd, 7), 6);

        v_ensure(fprintf(stderr, "Write 7 (6) bytes\n"));
        v_ensure(dmsg_print(&list, STDERR_FILENO));

        assert(list.len, sizeof(msg1) - 1 + sizeof(msg2) - 1);
        assert(list.list_size, 3);
        assert(memcmp(list.list[0].msg, msg1, 4), 0);
        assert(memcmp(list.list[1].msg, msg2, 8), 0);
        assert(memcmp(list.list[2].msg, msg2 + 8, 5), 0);

        dmsg_free(&list);

        close(fd);
#ifndef O_TMPFILE
        unlink(TMP_NAME);
#undef TMP_NAME
#endif
    }


    // test stream-like operations
    {
        // test reading across lines
        char msg[] = "test\nmessage\ntwo\n";
        for (int i = 2; i <= 8; i *= 2) {
            assert(dmsg_init2(&list, i), 0);
            assert(dmsg_append(&list, msg, sizeof(msg) - 1), 0);

            v_ensure(dmsg_print(&list, STDERR_FILENO));

#define SIZE 8
            char buf[SIZE];

            assert(dmsg_getline(&list, buf, sizeof(buf)), 5);
            assert(errno, 0);
            assert(strcmp(buf, "test"), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 8);
            assert(errno, 0);
            assert(strcmp(buf, "message"), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 4);
            assert(errno, 0);
            assert(strcmp(buf, "two"), 0);

            dmsg_free(&list);
#undef SIZE
        }


        // test overflowing lines
        for (int i = 2; i <= 8; i *= 2) {
            assert(dmsg_init2(&list, i), 0);
            assert(dmsg_append(&list, msg, sizeof(msg) - 1), 0);

            v_ensure(dmsg_print(&list, STDERR_FILENO));

#define SIZE 4
            char buf[SIZE];

            assert(dmsg_getline(&list, buf, sizeof(buf)), 4);
            assert(errno, DMSG_PARTIAL_READ);
            assert(strcmp(buf, "tes"), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 2);
            assert(errno, 0);
            assert(strcmp(buf, "t"), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 4);
            assert(errno, DMSG_PARTIAL_READ);
            assert(strcmp(buf, "mes"), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 4);
            assert(errno, DMSG_PARTIAL_READ);
            assert(strcmp(buf, "sag"), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 2);
            assert(errno, 0);
            assert(strcmp(buf, "e"), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 4);
            assert(errno, 0);
            assert(strcmp(buf, "two"), 0);

            dmsg_free(&list);
#undef SIZE
        }


#define SIZE 12
        // test not ending with newline
        char msg2[] = "new message\nthris";
        for (int i = 2; i <= 8; i *= 2) {
            assert(dmsg_init2(&list, i), 0);
            assert(dmsg_append(&list, msg2, sizeof(msg2) - 1), 0);

            v_ensure(dmsg_print(&list, STDERR_FILENO));

            char buf[SIZE];

            assert(dmsg_getline(&list, buf, sizeof(buf)), 12);
            assert(errno, 0);
            assert(strcmp(buf, "new message"), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 0);
            assert(errno, DMSG_NO_NEWLINE);

            dmsg_free(&list);

        }

        // test dmsg_seek
        for (int i = 2; i <= 8; i *= 2) {
            assert(dmsg_init2(&list, i), 0);
            assert(dmsg_append(&list, msg2, sizeof(msg2) - 1), 0);
            
            char buf[SIZE];

            // test SEEK_SET

            // test seeking out of bounds with SEEK_SET
            assert_neq(dmsg_seek(&list, -1, SEEK_SET), 0);
            assert(dmsg_seek(&list, sizeof(msg2) - 1, SEEK_SET), 0);
            assert_neq(dmsg_seek(&list, sizeof(msg2), SEEK_SET), 0);
            assert(dmsg_seek(&list, 0, SEEK_SET), 0);

            assert(dmsg_getline(&list, buf, sizeof(buf)), 12);
            assert(dmsg_seek(&list, 3, SEEK_SET), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 9);
            assert(strcmp(buf, " message"), 0);

            // test SEEK_CUR

            assert(dmsg_seek(&list, -4, SEEK_CUR), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 4);
            assert(strcmp(buf, "age"), 0);

            // test going out of bounds with relative seek
            // and seek when the buffer is filled
            assert_neq(dmsg_seek(&list, -13, SEEK_CUR), 0);
            assert(dmsg_seek(&list, -12, SEEK_CUR), 0);
            assert(dmsg_getline(&list, buf, 7), 7);
            assert(strcmp(buf, "new me"), 0);
            // now try reading in all of "message"
            assert(dmsg_seek(&list, -2, SEEK_CUR), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 8);

            assert(dmsg_seek(&list, 2, SEEK_CUR), 0);
            assert(dmsg_getline(&list, buf, sizeof(buf)), 0);
            assert(errno, DMSG_NO_NEWLINE);


            // test SEEK_END

            // test seeking out of bounds with SEEK_END
            assert_neq(dmsg_seek(&list, 1, SEEK_END), 0);
            assert(dmsg_seek(&list, 1 - (ssize_t) sizeof(msg2), SEEK_END), 0);
            assert_neq(dmsg_seek(&list, -(ssize_t) sizeof(msg2), SEEK_END), 0);
            assert(dmsg_seek(&list, 0, SEEK_END), 0);

            dmsg_free(&list);
        }
#undef SIZE

    }

    fprintf(stderr, P_GREEN "All dmsg_list tests passed" P_RESET "\n");

    return 0;
}

