#include <sys/time.h>
#include <string.h>

#include "thread_pool.h"

void* thread_exec(void* args);

thread_pool* init_thread_pool(int threads_num) {
    thread_pool* pool = (thread_pool*) malloc(sizeof(thread_pool));
    pool->threads_num = threads_num;
    pool->cancelled = false;
    pool->mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pool->mutex_free = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pool->cond = (pthread_cond_t*) malloc(sizeof(pthread_cond_t));
    pthread_mutex_init(pool->mutex, NULL);
    pthread_mutex_init(pool->mutex_free, NULL);
    pthread_cond_init(pool->cond, NULL);
    pool->tasks_count = 0;
    pool->queue_begin = NULL;
    pool->queue_end = NULL;
    pool->threads = (pthread_t*) malloc(threads_num * sizeof(pthread_t));

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
    task* t;
    while(pool->queue_begin != NULL) {
        t = pool->queue_begin;
        pool->queue_begin = t->next;
        free(t);
    }
    free(pool);
}

void thread_pool_execute(thread_pool* pool, void* (*func)(void*), void* arg) {
    task* t = (task*) malloc(sizeof(task));
    t->task_func = func;
    t->arg = arg;
    t->next = NULL;
    
    pthread_mutex_lock(pool->mutex);
    if(pool->queue_end != NULL) {
        pool->queue_end->next = t;
    }
    if(pool->queue_begin == NULL) {
        pool->queue_begin = t;
    }
    pool->queue_end = t;
    pool->tasks_count++;
    
    pthread_cond_signal(pool->cond);
    pthread_mutex_unlock(pool->mutex);
}

void* thread_exec(void* args) {
    thread_pool* pool = (thread_pool*) args;
    struct timeval timestamp;
    gettimeofday(&timestamp, NULL);
    long stamp = timestamp.tv_usec;
    stamp = ((stamp / 100) % 10) * 100 + ((stamp / 10) % 10) * 10 + (stamp % 10);
    while(!pool->cancelled) {
        pthread_mutex_lock(pool->mutex);
        while(!pool->cancelled && pool->queue_begin == NULL) {
            pthread_cond_wait(pool->cond, pool->mutex);
        }
        if(pool->cancelled) {
            pthread_mutex_unlock(pool->mutex);
            return NULL;
        }
        task* t = pool->queue_begin;
        pool->queue_begin = t->next;
        pool->queue_end = (t == pool->queue_end ? NULL : pool->queue_end);
        pool->tasks_count--;
        pthread_mutex_unlock(pool->mutex);
        printf("%d: Running, remaining %d\n", stamp, pool->tasks_count);
        t->task_func(t->arg);
        printf("%d: Done \n", stamp);
        pthread_mutex_lock(pool->mutex_free);
        t->task_func = NULL;
        t->arg = NULL;
        t->next = NULL;
        free(t);
        pthread_mutex_unlock(pool->mutex_free);
    }
    return NULL;
}
