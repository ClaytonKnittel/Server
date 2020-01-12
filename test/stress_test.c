#ifdef __linux__
#define _BSD_SOURCE
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "../src/get_ip_addr.h"
#include "../src/util.h"
#include "../src/vprint.h"

//#define VERBOSE
#include "t_assert.h"

#define NUM_CONNECTIONS 128


volatile int ready;
volatile int srvpid;

void server_close(int sig) {
    // does not avoid race condition, but that's ok
    srvpid = -1;
}

void sigint(int sig) {
    if (srvpid != -1) {
        kill(srvpid, SIGINT);
    }
}

#define exit(...) \
    { \
        if (srvpid != -1) { \
            kill(srvpid, SIGINT); \
        } \
        exit(__VA_ARGS__); \
    }

#undef exit

int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    // for timing
    struct timespec start, end;
    int cfds[NUM_CONNECTIONS];
    size_t i;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s ip_addr[:port]\n", argv[0]);
        return -1;
    }

    // srvpid is -1 if we do not execve ./srv
    srvpid = -1;

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;

    int port = 80;

    if (argc == 1) {
        // if no input arguments are supplied, then use the ip address
        // of this machine and the defualt port 80, and we will run the
        // server on this machine, setting the notify flag so that the
        // server will send us a signal after it has been initialized
        // (note: debug mode must be on for this to work)
        char *server_args[] = {"./srv", "-q", NULL};
        char *env_args[1] = {NULL};

        server.sin_addr.s_addr = get_ip_addr();

        ready = 0;
        signal(SIGINT, &sigint);
        signal(SIGCHLD, &server_close);
        if ((srvpid = fork()) == 0) {
            execve("./srv", server_args, env_args);
            return 0;
        }

        if (srvpid == -1) {
            // unable to initialize server
            wait(NULL);
            return -1;
        }
        // wait 10ms for server to start up
        usleep(10000);
    }
    else {
        // otherwise, parse the server's ip address out of the argument
        // list, the server is assumed to be run externally in this case
        char* colon = strchr(argv[1], ':');
        if (colon != NULL) {
            *colon = '\0';
            colon++;

            char *endptr;
            port = strtol(colon, &endptr, 10);
            if (*colon == '\0' || *endptr != '\0') {
                fprintf(stderr, "Usage: %s ip_addr[:port]\n", argv[0]);
                return -1;
            }
        }
        if (inet_aton(argv[1], &server.sin_addr) == 0) {
            fprintf(stderr, "Invalid ip address: %s\n", argv[1]);
            return -1;
        }
    }
    server.sin_port = htons(port);

    for (i = 0; i < NUM_CONNECTIONS; i++) {
        cfds[i] = socket(AF_INET, SOCK_STREAM, 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < NUM_CONNECTIONS; i++) {
        assert_neq(connect(cfds[i], (struct sockaddr*) &server,
                    sizeof(struct sockaddr_in)), -1);
        write(cfds[i], "test", 4);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    for (i = 0; i < NUM_CONNECTIONS; i++) {
        close(cfds[i]);
    }
    // wait another 10ms to close all connections
    usleep(10000);

    if (srvpid != -1) {
        // we need to terminate the server
        kill(srvpid, SIGINT);
        wait(NULL);
    }

    printf(P_GREEN "Stress test results:" P_RESET "\n");

    printf(P_YELLOW "Total time:" P_RESET " %.3fs\n", timespec_diff(&end, &start));

    return 0;
}

