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
#include <sys/time.h>
#include "solutions_db.h"
#include "thread_pool.h"
#include "utils.h"
#include "uthash.h"
#include "utlist.h"

#define PORT 42016

#define THREAD_POOL_CAPACITY 20

#define OP_COMPILERS_LIST 0x89
#define OP_CHECK_SLN 0x10

#define ERR_NOCOMPILER 1
#define ERR_NOCLIENTS 2
#define

typedef struct {
    int socket;
    struct sockaddr addr;
    socklen_t addrlen;
} endpoint_t;

typedef struct {
    uint32 id;
    UT_hash_handle hh;
} client_id_t;

typedef struct {
    uint16 id;
    client_id_t* clients_list;
    pthread_mutex_t* list_mutex;
    UT_hash_handle hh;
} compiler_t;

typedef struct {
    uint32 id;
    endpoint_t* endpoint;
    queue* solutions_queue;
    solution* checking_solution;
    pthread_mutex_t* mutex;
    UT_hash_handle hh;
} client_t;

void run_server(logger_t* logger);

#endif // SERVER_H
