#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct task_queue_elem {
    void* (*task_func)(void*);
    void* arg;
    struct task_queue_elem* next;
} task;

typedef struct {
    int threads_num;
    bool cancelled;
    int tasks_count;
    task* queue_begin;
    task* queue_end;
    pthread_mutex_t* mutex;
    pthread_mutex_t* mutex_free;
    pthread_cond_t* cond;
    pthread_t* threads;
} thread_pool;

typedef struct {
    thread_pool* pool;
    int thread_id;
} thread_start_arg;

thread_pool* init_thread_pool(int threads_num);
void destroy_thread_pool(thread_pool* pool);
void thread_pool_execute(thread_pool* pool, void* (*task)(void*), void* arg);

#endif
