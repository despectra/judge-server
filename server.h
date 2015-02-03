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

#define OP_CODE_CMP_LIST 0x89
#define OP_CODE_CHK_SLN 0x10

typedef struct {
    int socket;
    struct sockaddr addr;
    socklen_t addrlen;
} endpoint;

typedef struct client_ptr {
    uint32 id;
    struct client_ptr* next;
    struct client_ptr* prev;
} client_ptr;

typedef struct {
    uint16 id;
    client_ptr* clients_list;
    pthread_mutex_t* list_mutex;
    UT_hash_handle hh;
} compiler_def;

typedef struct {
    uint32 id;
    int socket;
    uint32 loading;
    queue* solutions_queue;
    pthread_mutex_t* queue_mutex;
    UT_hash_handle hh;
} client_def;

void start_server();

#endif // SERVER_H
