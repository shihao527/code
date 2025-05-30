
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>


#define THREAD_POOL_SIZE 8 
#define TASK_QUEUE_SIZE 5 

struct task {
    struct conn_table *ct;
    int fd;
    char ip[16];
    uint16_t port;
};

struct thread_pool {
    pthread_t *threads;          
    struct task *queue;         
    int queue_size;             
    int task_count;             
    int head;                   
    int tail;                   
    pthread_mutex_t mutex;      
    pthread_cond_t not_empty;   
    pthread_cond_t not_full;    
    int shutdown;               
};


struct thread_pool *thread_pool_create(int num_threads, int queue_size);
void thread_pool_add_task(struct thread_pool *pool, struct task task);
void thread_pool_destroy(struct thread_pool *pool);
void *worker_thread(void *arg);
void handle_client(struct conn_table *ct, int fd, const char *ip, uint16_t port);
