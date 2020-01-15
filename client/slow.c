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
    printf("Usage: %s <ip address>:<port>\n",
            program_name);
    exit(1);
}


int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    int port = 80;

#define NSOCKS 1
    int socks[NSOCKS];

    char *endptr;
    int c, i;

    if (argc != 2) {
        usage(argv[0]);
    }

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

    printf("Connecting to %s:%d\n", inet_ntoa(server.sin_addr), port);

    for (int i = 0; i < NSOCKS; i++) {
        socks[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (socks[i] == -1) {
            printf("Could not connect: %s\n", strerror(errno));
            while (--i >= 0) {
                close(socks[i]);
            }
        }

        if (connect(socks[i], (struct sockaddr*) &server,
                    sizeof(struct sockaddr_in)) == -1) {
            printf("Unable to connect socket to port %d, ""reason %s\n",
                    port, strerror(errno));
            while (i >= 0) {
                close(socks[i]);
                i--;
            }
            return -1;
        }
    }
    char msg[] = "GET / HTTP/1.1\r\nConnection: keep-alive\r\n\r\n";

    for (size_t j = 0; j < sizeof(msg) - 1; j++) {
        for (size_t i = 0; i < NSOCKS; i++) {
            write(socks[i], &msg[j], 1);
            if (i == NSOCKS - 1) {
                write(STDOUT_FILENO, &msg[j], 1);
            }
        }
        usleep(300000);
    }

    usleep(10000);
    char buf[4096];
    ssize_t read_in = read(socks[0], buf, sizeof(buf));

    if (read_in == -1) {
        printf("Unable to read server's response, reason: %s\n",
                strerror(errno));
    }
    else {
        write(STDOUT_FILENO, buf, read_in);
    }
    
    char msg2[] = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";

    for (size_t j = 0; j < sizeof(msg2) - 1; j++) {
        for (size_t i = 0; i < NSOCKS; i++) {
            write(socks[i], &msg2[j], 1);
            if (i == NSOCKS - 1) {
                write(STDOUT_FILENO, &msg2[j], 1);
            }
        }
        usleep(300000);
    }

    usleep(10000);
    read_in = read(socks[0], buf, sizeof(buf));

    if (read_in == -1) {
        printf("Unable to read server's response, reason: %s\n",
                strerror(errno));
    }
    else {
        write(STDOUT_FILENO, buf, read_in);
    }

    for (int i = 0; i < NSOCKS; i++) {
        close(socks[i]);
    }

    return 0;
}

