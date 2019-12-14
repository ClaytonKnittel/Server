
#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <net/if.h>
#include <unistd.h>


#define MAX_HOSTNAME 256

static struct in_addr _get_ip_addr() {
    char hostname[MAX_HOSTNAME];
    struct hostent* host_entry;

    if (gethostname(hostname, sizeof(hostname)) == -1) {
        printf("Failed to get host name\n");
        return (struct in_addr) { .s_addr = 0 };
    }

    host_entry = gethostbyname(hostname);
    if (host_entry == NULL) {
        printf("Failed to get host entry for name %s\n", hostname);
        return (struct in_addr) { .s_addr = 0 };
    }

    return *(struct in_addr*) host_entry->h_addr_list[0];
}

static struct in_addr __get_ip_addr() {
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    
    // we want an IPv4 IP address
    ifr.ifr_addr.sa_family = AF_INET;

    strncpy(ifr.ifr_name, "etho0", IFNAMSIZ - 1);

    ioctl(fd, SIOCGIFADDR, &ifr);

    close(fd);

    return ((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr;
}

uint32_t get_ip_addr() {
    return _get_ip_addr().s_addr;
}

char* get_ip_addr_str() {
    struct in_addr addr = _get_ip_addr();
    return inet_ntoa(addr);
}


