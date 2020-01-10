#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/socket.h>


static void usage(const char* program_name) {
    printf("Usage: %s <ip address>:<port> [-n num clients]\n",
            program_name);
    exit(1);
}


int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    int *socks;
    int nsocks = 1, port = 80;

    char *endptr;
    int c, i;

    if (argc == 1) {
        usage(argv[0]);
    }

#define NUM_OPT \
    strtol(optarg, &endptr, 0);                 \
    if (*optarg == '\0' || *endptr != '\0') {   \
        usage(argv[0]);                         \
    }

    while ((c = getopt(argc - 1, argv + 1, "n:")) != -1) {
        switch(c) {
        case 'n':
            nsocks = NUM_OPT;
            break;
        case '?':
            usage(argv[0]);
        default:
            return -1;
        }
    }

#undef NUM_OPT

    char* colon = strchr(argv[1], ':');
    if (colon != NULL) {
        *colon = '\0';
        colon++;

        char *endptr;
        port = strtol(colon, &endptr, 0);
        if (*colon == '\0' || *endptr != '\0') {
            printf("Usage: %s <ip address>:<port>\n", argv[0]);
            return 2;
        }
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    if (inet_aton(argv[1], &server.sin_addr) == 0) {
        printf("Invalid ip address: %s\n", argv[1]);
        return 3;
    }
    
    printf("Connecting to %s:%d with %d client(s)\n",
            inet_ntoa(server.sin_addr), port, nsocks);

    char msg[] = "test message 0!\n";

    socks = (int*) malloc(nsocks * sizeof(int));
    for (i = 0; i < nsocks; i++) {
        socks[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (socks[i] == -1) {
            printf("brokeded: %s\n", strerror(errno));
        }

        if (connect(socks[i], (struct sockaddr*) &server,
                    sizeof(struct sockaddr_in)) == -1) {
            printf("Unable to connect socket to port %d, ""reason %s\n",
                    port, strerror(errno));
            free(socks);
            return -1;
        }

        if (i == nsocks - 1) {
            continue;
        }
        for (int j = 0; j < sizeof(msg) - 1; j++) {
            write(socks[i], &msg[j], 1);
            write(STDOUT_FILENO, &msg[j], 1);
        }
        msg[13]++;
        struct timespec dt = {
            .tv_sec = 0,
            .tv_nsec = 400000000
        };
        nanosleep(&dt, NULL);
    }

    write(socks[nsocks - 1], "exit", 4);

    struct timespec dt = {
        .tv_sec = 0,
        .tv_nsec = 1000000
    };
    nanosleep(&dt, NULL);

    for (i = nsocks - 1; i >= 0; i--) {
        close(socks[i]);
    }

    free(socks);

    return 0;
}
