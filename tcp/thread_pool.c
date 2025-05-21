#include <pthread.h>
#include "hash_utils.h"
#include "thread_pool.h"
#include <stdint.h>
#include "fd_queued.h"


struct thread_pool *thread_pool_create(int num_threads, int queue_size) {
    struct thread_pool *pool = malloc(sizeof(struct thread_pool));
    if (!pool) return NULL;

    pool->threads = malloc(num_threads * sizeof(pthread_t));
    pool->queue = malloc(queue_size * sizeof(struct task));
    pool->queue_size = queue_size;
    pool->task_count = 0;
    pool->head = 0;
    pool->tail = 0;
    pool->shutdown = 0;

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->not_empty, NULL);
    pthread_cond_init(&pool->not_full, NULL);

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
        printf("creating pool %d",i);
        }

    return pool;
}

void thread_pool_add_task(struct thread_pool *pool, struct task task) {
    pthread_mutex_lock(&pool->mutex);
    printf("new task\n");
    printf("task count%d\n", pool->task_count);
    // Wait if queue is full
    while (pool->task_count == pool->queue_size && !pool->shutdown) {
        printf("2222");
        pthread_cond_wait(&pool->not_full, &pool->mutex);
        printf("wait\n");
    }

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        return;
    }
    
    // Add task to queue
    pool->queue[pool->tail] = task;
    pool->tail = (pool->tail + 1) % pool->queue_size;
    pool->task_count++;

    pthread_cond_signal(&pool->not_empty);
    
    pthread_mutex_unlock(&pool->mutex);

    
    
}

void *worker_thread(void *arg) {
    struct thread_pool *pool = (struct thread_pool *)arg;
    pthread_t tid=pthread_self();
    printf("worker thread 5555555555555555555, %lu\n", (unsigned long)tid);

    while (1) {
        pthread_mutex_lock(&pool->mutex);
        
        // Wait for tasks
        while (pool->task_count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->not_empty, &pool->mutex);
        }

        if (pool->shutdown && pool->task_count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }
        

        // Get task from queue
        struct task task = pool->queue[pool->head];
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->task_count--;

        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->mutex);

        // Process task (call handle_client)
        printf("worker thread 3333333333333333, %lu\n", (unsigned long)tid);
        handle_client(task.ct, task.fd, task.ip, task.port);
        remove_queued_fd(task.fd);
    }
    return NULL;
}

void thread_pool_destroy(struct thread_pool *pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_cond_broadcast(&pool->not_full);
    pthread_mutex_unlock(&pool->mutex);

    // Join all threads
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    free(pool->queue);
    free(pool->threads);
    free(pool);
}