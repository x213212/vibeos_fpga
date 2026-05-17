#ifndef SYS_SOCKET_H
#define SYS_SOCKET_H

typedef int socklen_t;
typedef int sa_family_t;

#define AF_UNSPEC 0
#define AF_INET 2
#define SOCK_STREAM 1

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char __data[128];
};

struct sockaddr_in {
    sa_family_t sin_family;
    unsigned short sin_port;
    unsigned int sin_addr;
    char sin_zero[8];
};

#endif
