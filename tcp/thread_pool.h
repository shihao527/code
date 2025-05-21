
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>


#define THREAD_POOL_SIZE 8  // Number of worker threads (adjust based on CPU cores)
#define TASK_QUEUE_SIZE 5 // Max tasks in queue (adjust based on load)

struct task {
    struct conn_table *ct;
    int fd;
    char ip[16];
    uint16_t port;
};

struct thread_pool {
    pthread_t *threads;          // Array of worker threads
    struct task *queue;         // Task queue
    int queue_size;             // Max queue size
    int task_count;             // Current number of tasks
    int head;                   // Queue head (next task to process)
    int tail;                   // Queue tail (where to add tasks)
    pthread_mutex_t mutex;      // Mutex for queue access
    pthread_cond_t not_empty;   // Condition for non-empty queue
    pthread_cond_t not_full;    // Condition for non-full queue
    int shutdown;               // Flag to shut down pool
};


struct thread_pool *thread_pool_create(int num_threads, int queue_size);
void thread_pool_add_task(struct thread_pool *pool, struct task task);
void thread_pool_destroy(struct thread_pool *pool);
void *worker_thread(void *arg);
void handle_client(struct conn_table *ct, int fd, const char *ip, uint16_t port);
