#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <signal.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#include "server.h"
#include "vprint.h"
#include "get_ip_addr.h"
#include "http.h"
#include "dmsg.h"

#if !defined(__APPLE__) && !defined(__linux__)
#error Only compatible with Linux and MacOS
#endif


#ifdef DEBUG
#define OPTSTR "b:hnp:qt:vV"
#else
#define OPTSTR "b:hp:qt:vV"
#endif


static struct server server;
static int nthreads = 0;


void usage(const char* program_name) {
    printf("Usage: %s [options]\n\n"
           "\t-p port\t\tthe port the server should listen on.\n"
           "\t\t\tThe default is %d\n"
           "\t-b backlog\tnumber of connections to backlog in\n"
           "\t\t\tthe listen syscall. The default is %d\n"
           "\n"
           "\t-q\t\trun in quiet mode, which only prints errors\n"
           "\t\t\t(note: to optimize out prints, #define QUIET\n"
           "\t\t\tbefore including vprint.h)\n"
           "\t-v\t\trun with verbose level 1, which prints all\n"
           "\t\t\tv*prints, but not dbg_prints. This is the default\n"
           "\t-V\t\trun with verbose level 2, which prints everything\n"
           "\n",
           program_name, DEFAULT_PORT, DEFAULT_BACKLOG);

    exit(1);
}


void close_handler(int signum);


int init(struct server *server, int argc, char *argv[]) {
    int c, port, backlog, ret;
#ifdef DEBUG
    int notify = 0;
#endif
    char* endptr;

    port = DEFAULT_PORT;
    backlog = DEFAULT_BACKLOG;

#define NUM_OPT \
    strtol(optarg, &endptr, 0);                 \
    if (*optarg == '\0' || *endptr != '\0') {   \
       usage(argv[0]);                          \
    }

    while ((c = getopt(argc, argv, OPTSTR)) != -1) {
        switch (c) {
        case 'b':
            backlog = NUM_OPT;
            break;
#ifdef DEBUG
        case 'n':
            // notify parent process when server has fully initialized
            notify = 1;
            break;
#endif
        case 'p':
            port = NUM_OPT;
            break;
        case 'q':
            vlevel = V0;
            break;
        case 't':
            nthreads = NUM_OPT;
            break;
        case 'v':
            vlevel = V1;
            break;
        case 'V':
            vlevel = V2;
            break;
        case 'h':
        case '?':
            usage(argv[0]);
        default:
            return -1;
        }
    }

#undef NUM_OPT

    if ((ret = init_server3(server, port, backlog)) != 0) {
        printf("Failed to initialize server\n");
        return ret;
    }
#ifdef DEBUG
    if (notify) {
        // notify the parent process that initialization is complete
        kill(getppid(), SIGUSR1);
    }
#endif

    signal(SIGINT, close_handler);
    signal(SIGUSR2, close_handler);

    // initialize const globals in http processor
    http_init();

    return 0;
}

void close_handler(int signum) {
    close_server(&server);

    // clean up memory used by http processor
    http_exit();

    exit(0);
}

int main(int argc, char *argv[]) {
    int ret;

    if ((ret = init(&server, argc, argv)) != 0) {
        return ret;
    }

    print_server_params(&server);

    /*printf("Types:\nAF_INET: %x\nAF_UNIX: %x\n",
            AF_INET, AF_UNIX);*/

    if (nthreads == 0) {
        run_server(&server);
    }
    else {
        run_server2(&server, nthreads);
    }

    return 0;
}
