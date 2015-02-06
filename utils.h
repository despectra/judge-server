#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <bits/pthreadtypes.h>
#include <stdio.h>

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

//#define LOG_ON

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned long uint32;
typedef unsigned long long uint64;
typedef char int8;
typedef short int16;
typedef long int32;
typedef long long int64;

/*
 * Generic data block with size
 */

typedef struct {
    void* data;
    uint32 length;
} data_block;

/*
* initializes generic data block with size 'length'
*/
data_block* block_init(void* data, uint32 length);

void block_free(data_block* block);

data_block read_string(char* data, uint32 length);

/*
* Simple queue
*/

typedef struct q_elem {
    data_block* value;
    struct q_elem* next;
} queue_elem;

typedef struct {
    uint32 elems_count;
    queue_elem* head;
    queue_elem* tail;
} queue;

queue* queue_init();

/*
* returns the head element of the queue with removing of this element
*/
data_block* queue_pop(queue* q);

/*
* pushes the data_block element in the queue
* in_block is not being copied while pushing so you should always hold pointer to it
*/
void queue_push(queue* q, const data_block* in_block);

/*
* iterates over all elements in a queue
* for each element print_func is being invoked
*/
void queue_iterate(queue* q, void (*print_func)(data_block* block));

/*
* free the queue structure without free()ing data blocks itself
* must be called only after each data_block in queue has been free()d
*/
void queue_struct_free(queue* q);

/*
 * Simple file logger (thread safe)
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
 Free resources associated with this logger
 */
void logger_destroy(logger_t* logger);

#endif // UTILS_H
