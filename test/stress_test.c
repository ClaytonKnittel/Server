#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "../get_ip_addr.h"
#include "../vprint.h"
//#define VERBOSE
#include "../t_assert.h"

#define NUM_CONNECTIONS 128


volatile int ready;
int srvpid;

void server_initialized(int sig) {
    ready = 1;
}

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
    }

int main(int argc, char *argv[]) {
    struct sockaddr_in server;
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
        char *server_args[] = {"./srv", "-n", "-q", NULL};
        char *env_args[1] = {NULL};

        server.sin_addr.s_addr = get_ip_addr();

        ready = 0;
        signal(SIGUSR1, &server_initialized);
        signal(SIGINT, &sigint);
        signal(SIGCHLD, &server_close);
        if ((srvpid = fork()) == 0) {
            execve("./srv", server_args, env_args);
            return 0;
        }

        // wait for the server to notify us that it has been initialized
        while (!ready);
    }
    else {
        // otherwise, parse the server's ip address out of the argument
        // list, the server is assumed to be run externally in this case
        char* colon = strchr(argv[1], ':');
        if (colon != NULL) {
            *colon = '\0';
            colon++;

            char *endptr;
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
    for (i = 0; i < NUM_CONNECTIONS; i++) {
        assert_neq(connect(cfds[i], (struct sockaddr*) &server,
                    sizeof(struct sockaddr_in)), -1);
        write(cfds[i], "test", 4);
    }

    for (i = 0; i < NUM_CONNECTIONS; i++) {
        close(cfds[i]);
    }

    if (srvpid != -1) {
        // we need to terminate the server
        kill(srvpid, SIGINT);
    }

    printf(P_GREEN "Stress test results:" P_RESET "\n");

    return 0;
}

