#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

#define LOG_ON

typedef __uint8_t uint8;
typedef __uint16_t uint16;
typedef __uint32_t uint32;
typedef __uint64_t uint64;
typedef __int8_t int8;
typedef __int16_t int16;
typedef __int32_t int32;
typedef __int64_t int64;

/*
 *  Useful functions, types, structs for networking
 */

typedef struct {
    int socket;
    struct sockaddr_in addr;
    socklen_t addrlen;
} endpoint_t;

/*
    Try to send a pack of data of size=@data_len
 */
ssize_t send_all(int socket, char* data, socklen_t data_len);

/*
    Try to receive a pack of data of size=@data_len
 */
ssize_t recv_all(int socket, char* data, socklen_t data_len);

/*
 *  Generic data block with size
 */

typedef struct {
    void* data;
    uint32 length;
} data_block;

/*
*   initializes generic data block with size 'length'
*/
data_block* block_init(void* data, uint32 length);

void block_free(data_block* block);

data_block read_string(char* data, uint32 length);

/*
*   Simple queue
*/

typedef struct q_elem {
    void* value;
    struct q_elem* next;
} queue_elem;

typedef struct {
    uint32 elems_count;
    queue_elem* head;
    queue_elem* tail;
} queue;

queue* queue_init();

/*
*   returns the head element of the queue with removing of this element
*/
void* queue_pop(queue* q);

/*
*   pushes the data_block element in the queue
*   @value is not being copied while pushing so you should always hold pointer to it
*/
void queue_push(queue* q, void* value);

/*
*   iterates over all elements in a queue
*   for each element @print_func is being invoked
*/
void queue_iterate(queue* q, void (*print_func)(void*));

/*
*   free the queue structure without free()ing data blocks itself
*   must be called only after each data_block in queue has been free()d
*/
void queue_struct_free(queue* q);

/*
 *  Simple thread-safe file logger
 */

typedef struct {
    const char* fname;
    FILE* fd;
    pthread_mutex_t* mutex;
} logger_t;

/*
    Creates new instance of file logger
 */
logger_t* logger_init();

/*
    Writes formatted string to log
 */
void logger_printf(logger_t* logger, const char* format, ...);

/*
    Free resources associated with this @logger
 */
void logger_destroy(logger_t* logger);

#endif // UTILS_H
