#ifndef SERVER_H
#define SERVER_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include "thread_pool.h"
#include "utils.h"
#include "uthash.h"

#define PORT 42016

#define OP_CODE_CMP_LIST 0x89

typedef struct {
    int socket;
    struct sockaddr addr;
    socklen_t addrlen;
} client_args;

typedef struct {

} compilers_map;

void start_server();

#endif // SERVER_H
