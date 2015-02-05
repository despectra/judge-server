#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include "utils.h"

data_block read_string(char* data, uint32 length) {
    data_block block;
    if(length < 4) {
        return block;
    }

    block.length = *(data + 3) + (*(data + 2) << 8) + (*(data + 1) << 16) + (*data << 24);
    data += 4;
    length -= 4;
    block.data = (char*) malloc(block.length * sizeof(char));
    int real_length = min(length, block.length);
    memcpy(block.data, data, real_length);
    return block;
}

data_block* block_init(void* data, uint32 length) {
    data_block* block = (data_block*) malloc(sizeof(data_block));
    block->length = length;
    block->data = malloc(length * sizeof(char));
    memcpy(block->data, data, length);
    return block;
}

void block_free(data_block* block) {
    if(block == NULL) {
        return;
    }
    free(block->data);
    free(block);
}

queue* queue_init() {
    queue* q = (queue*) malloc(sizeof(queue));
    q->elems_count = 0;
    q->head = NULL;
    q->tail = NULL;
    return q;
}

data_block* queue_pop(queue* q) {
    if(q == NULL) {
        return NULL;
    }
    queue_elem* head = q->head;
    if(head == NULL) {
        return NULL;
    }
    q->head = head->next;
    q->elems_count--;
    return head->value;
}

void queue_push(queue* q, const data_block* in_block) {
    if(q == NULL) {
        return;
    }
    queue_elem* elem = (queue_elem*) malloc(sizeof(queue_elem));
    elem->value = in_block;
    elem->next = NULL;
    if(q->elems_count == 0) {
        q->head = elem;
        q->tail = elem;
    } else if(q->elems_count == 1) {
        q->head->next = elem;
        q->tail = elem;
    } else {
        q->tail->next = elem;
        q->tail = elem;
    }
    q->elems_count++;
}

void queue_iterate(queue* q, void (*print_func)(data_block* block)) {
    if(q == NULL || q->elems_count == 0) {
        return;
    }
    queue_elem* cur_elem = q->head;
    while(cur_elem != NULL) {
        print_func(cur_elem->value);
        cur_elem = cur_elem->next;
    }
}

void queue_struct_free(queue* q) {
    if(q == NULL) {
        return;
    }
    queue_elem* cur_elem = q->head;
    queue_elem* prev_elem;
    data_block* block;
    while(cur_elem != NULL) {
        block = cur_elem->value;
        block_free(block);
        free(cur_elem);
        prev_elem = cur_elem;
        cur_elem = cur_elem->next;
        free(prev_elem);
    }
    free(q);
}

struct tm* getcurtime() {
    time_t cur_time;
    time(&cur_time);
    return localtime(&cur_time);
}

logger_t* logger_init() {
    const char* fname = "log.txt";
    FILE* logfile = fopen(fname, "a");
    if(logfile == NULL) {
        perror("Unable to open file log.txt");
        return NULL;
    }
    struct tm* curtime = getcurtime();

    char str[50];
    strftime(str, 50, "%d.%m.%Y at %H:%M:%S", curtime);
    fprintf(logfile, "#### Logging started [%s] ####\n", str);

    logger_t* logger = (logger_t*) malloc(sizeof(logger_t));
    logger->mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(logger->mutex, NULL);
    logger->fname = fname;
    logger->fd = logfile;
    return logger;
}

void logger_printf(logger_t* logger, const char* format, ...) {
    if(logger == NULL || logger->fd == NULL) {
        return;
    }
    pthread_mutex_lock(logger->mutex);
    struct tm* curtime = getcurtime();
    char timestr[50];
    strftime(timestr, 50, "%d.%m.%Y %H:%M:%S", curtime);
    fprintf(logger->fd, "[%s] THREAD %lu: ", timestr, pthread_self());
    va_list args;
    va_start(args, format);
    vfprintf(logger->fd, format, args);
    va_end(args);
    fprintf(logger->fd, "\n");
    pthread_mutex_unlock(logger->mutex);
}

void logger_destroy(logger_t* logger) {
    if(logger == NULL) {
        return;
    }
    pthread_mutex_lock(logger->mutex);
    struct tm* curtime = getcurtime();
    char str[50];
    strftime(str, 50, "%d.%m.%Y at %H:%M:%S", curtime);
    fprintf(logger->fd, "><>< Logging ended [%s] ><><\n\n", str);
    fclose(logger->fd);
    pthread_mutex_unlock(logger->mutex);
    pthread_mutex_destroy(logger->mutex);
    free(logger);
}
