#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
#define OPTSTR "b:hl:np:qt:vV"
#else
#define OPTSTR "b:hl:p:qt:vV"
#endif


static struct server server;
static int nthreads = 0;

// if not -1, the file descriptor of the open file where all output of this
// program is dumped
static int output_fd = -1;


void usage(const char* program_name) {
    printf("Usage: %s [options]\n\n"
           "\t-p port\t\tthe port the server should listen on.\n"
           "\t\t\tThe default is %d\n"
           "\t-b backlog\tnumber of connections to backlog in\n"
           "\t\t\tthe listen syscall. The default is %d\n"
           "\t-t n_threads\tnumber of worker threads to create\n"
           "\n"
           "\t-q\t\trun in quiet mode, which only prints errors\n"
           "\t\t\t(note: to optimize out prints, #define QUIET\n"
           "\t\t\tbefore including vprint.h)\n"
           "\t-v\t\trun with verbose level 1, which prints all\n"
           "\t\t\tv*prints, but not dbg_prints. This is the default\n"
           "\t-V\t\trun with verbose level 2, which prints everything\n"
           "\t-l out_file\tlogs all output of the server in supplied file\n"
           "\n"
           "\t-h\t\tdisplay this message\n",
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
        case 'l':
            output_fd = open(optarg, O_RDWR | O_TRUNC | O_CREAT | O_SYNC, 0644);
            if (output_fd == -1) {
                printf("Failed to open/create file \"%s\" for writing, "
                        "reason: %s\n", optarg, strerror(errno));
                return -1;
            }
            printf("fd %d\n", output_fd);
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
    if (http_init() != 0) {
        return 1;
    }

    return 0;
}

void close_handler(int signum) {
    close_server(&server);

    // clean up memory used by http processor
    http_exit();

    if (output_fd != -1) {
        fflush(stdout);
        fflush(stderr);
        close(output_fd);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    int ret;

    if ((ret = init(&server, argc, argv)) != 0) {
        if (output_fd != -1) {
            close(output_fd);
        }
        return ret;
    }

    if (output_fd != -1) {
        // redirect both stdout and stderr to output_fd
        dup2(output_fd, STDOUT_FILENO);
        dup2(output_fd, STDERR_FILENO);
    }

    print_server_params(&server);

    if (nthreads == 0) {
        run_server(&server);
    }
    else {
        run_server2(&server, nthreads);
    }

    if (output_fd != -1) {
        close(output_fd);
    }

    return 0;
}
