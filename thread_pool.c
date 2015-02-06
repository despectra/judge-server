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
    pool->tasks_queue = queue_init();
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

    queue_iterate(pool->tasks_queue, block_free);
    queue_struct_free(pool->tasks_queue);
    free(pool);
}

void thread_pool_execute(thread_pool* pool, void* (*func)(void*), void* arg) {
    task* t = (task*) malloc(sizeof(task));
    t->task_func = func;
    t->arg = arg;
    
    pthread_mutex_lock(pool->mutex);

    data_block* task_block = block_init(t, sizeof(*t));
    queue_push(pool->tasks_queue, task_block);
    
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
        while(!pool->cancelled && pool->tasks_queue->elems_count == 0) {
            pthread_cond_wait(pool->cond, pool->mutex);
        }
        if(pool->cancelled) {
            pthread_mutex_unlock(pool->mutex);
            return NULL;
        }
        data_block* task_block = queue_pop(pool->tasks_queue);
        task* t = (task*) task_block->data;
        pthread_mutex_unlock(pool->mutex);
        //printf("%d: Running task, remaining %d\n", stamp, pool->tasks_queue->elems_count);
        t->task_func(t->arg);
        //printf("%d: Done \n", stamp);
        pthread_mutex_lock(pool->mutex_free);
        t->task_func = NULL;
        t->arg = NULL;
        block_free(task_block);
        pthread_mutex_unlock(pool->mutex_free);
    }
    return NULL;
}
