#include <sys/time.h>
#include <string.h>

#include "thread_pool.h"

void* thread_exec(void* args);

thread_pool* init_thread_pool(int threads_num) {
    thread_pool* pool = malloc(sizeof(thread_pool));
    pool->threads_num = threads_num;
    pool->cancelled = false;
    pool->mutex = malloc(sizeof(pthread_mutex_t));
    pool->mutex_free = malloc(sizeof(pthread_mutex_t));
    pool->cond = malloc(sizeof(pthread_cond_t));
    pthread_mutex_init(pool->mutex, NULL);
    pthread_mutex_init(pool->mutex_free, NULL);
    pthread_cond_init(pool->cond, NULL);
    pool->tasks_queue = queue_init();
    pool->threads = malloc(threads_num * sizeof(pthread_t));

    pthread_t* thread;
    for(int i = 0; i < threads_num; i++) {
        thread = &pool->threads[i];
        pthread_create(thread, NULL, thread_exec, pool);
    }
    return pool;
}

void destroy_thread_pool(thread_pool* pool) {
    pool->cancelled = true;
    pthread_mutex_lock(pool->mutex);
    pthread_cond_broadcast(pool->cond);
    pthread_mutex_unlock(pool->mutex);
    
    for(int i = 0; i < pool->threads_num; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);
    pthread_mutex_destroy(pool->mutex);
    pthread_mutex_destroy(pool->mutex_free);
    pthread_cond_destroy(pool->cond);

    queue_iterate(pool->tasks_queue, free);
    queue_struct_free(pool->tasks_queue);
    free(pool);
}

void thread_pool_execute(thread_pool* pool, void* (*func)(void*), void* arg) {
    task* t = malloc(sizeof(task));
    t->task_func = func;
    t->arg = arg;
    
    pthread_mutex_lock(pool->mutex);
    queue_push(pool->tasks_queue, t);
    pthread_cond_signal(pool->cond);
    pthread_mutex_unlock(pool->mutex);
}

void* thread_exec(void* args) {
    thread_pool* pool = (thread_pool*) args;
    while(!pool->cancelled) {
        pthread_mutex_lock(pool->mutex);
        while(!pool->cancelled && pool->tasks_queue->elems_count == 0) {
            pthread_cond_wait(pool->cond, pool->mutex);
        }
        if(pool->cancelled) {
            pthread_mutex_unlock(pool->mutex);
            return NULL;
        }
        task* t = queue_pop(pool->tasks_queue);
        pthread_mutex_unlock(pool->mutex);
        t->task_func(t->arg);
        pthread_mutex_lock(pool->mutex_free);
        t->task_func = NULL;
        t->arg = NULL;
        free(t);
        pthread_mutex_unlock(pool->mutex_free);
    }
    return NULL;
}
