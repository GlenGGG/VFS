#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct threadpool* initThreadPool(int num_threads)
{
    threadpool* pool;
    pool = (threadpool*)malloc(sizeof(struct threadpool));
    pool->num_threads = 0;
    pool->num_working = 0;
    pthread_mutex_init(&(pool->thcount_lock), NULL);
    pthread_cond_init(&(pool->threads_all_idle), NULL);

    if (init_taskqueue(&pool->queue) < 0) {
        error("init_taskqueue", "thread_pool");
        return NULL;
    }

    pool->threads
        = (struct thread**)malloc(num_threads * sizeof(struct thread*));

    pool->is_alive = true;
    for (int i = 0; i < num_threads; ++i) {
        create_thread(pool, &(pool->threads[i]), i);
    }

    while (pool->num_threads != num_threads) {
    }

    return pool;
}

void addTask2ThreadPool(threadpool* pool, task* curtask)
{
    push_taskqueue(&pool->queue, curtask);
}

void waitThreadPool(threadpool* pool)
{
    pthread_mutex_lock(&pool->thcount_lock);
    while (pool->queue.len || pool->num_working) {
        pthread_cond_wait(&pool->threads_all_idle, &pool->thcount_lock);
    }
    pthread_mutex_unlock(&pool->thcount_lock);
}

int destoryThreadPool(threadpool* pool)
{
    int num; // stored num_threads
    pthread_mutex_lock(&(pool->thcount_lock));
    num = pool->num_threads;
    pthread_mutex_unlock(&(pool->thcount_lock));

    pool->is_alive = false;
    waitThreadPool(pool);
    if (destory_taskqueue(&pool->queue) < 0)
        return -1;

    for (int i = 0; i < num; ++i)
        free(pool->threads[i]);

    pthread_mutex_destroy(&(pool->thcount_lock));
    pthread_cond_destroy(&(pool->threads_all_idle));

    free(pool->threads);

    return 0;
}

int getNumofThreadWorking(threadpool* pool) { return pool->num_working; }

int create_thread(struct threadpool* pool, struct thread** pthread, int id)
{
    *pthread = (struct thread*)malloc(sizeof(struct thread));
    if (*pthread == NULL) {
        error("create_thread(): Could not allocate memory for thread\n",
            "thread_pool.log");
        return -1;
    }

    (*pthread)->pool = pool;
    (*pthread)->id = id;

    pthread_create(&((*pthread)->pthread), NULL, (void*)thread_do, (*pthread));
    pthread_detach((*pthread)->pthread);

    return 0;
}

void* thread_do(struct thread* pthread)
{
    char thread_name[128] = { 0 };
    char log_name[NAME_LEN];
    struct thread_info threadinfo;
    struct log_file logf;
    struct time_counter counter;
    sprintf(thread_name, "thread-pool-%d", pthread->id);
    sprintf(log_name, SERVER_CODE"-%s.log", thread_name);
    prctl(PR_SET_NAME, thread_name);
    memset(&(counter.cost_time), 0, sizeof(counter.cost_time));
    counter.invokes = 0;
    counter.total_cost_time = 0;
    pthread_mutex_init(&(counter.mutex), NULL);
    threadinfo.counter = &counter;

    log_file_init(&logf, log_name);
    threadinfo.id = &(pthread->id);
    threadinfo.logf = &logf;
    threadinfo.thread_name = thread_name;

    pthread->threadinfo = &threadinfo;
    threadpool* pool = pthread->pool;

    pthread_mutex_lock(&(pool->thcount_lock));
    ++(pool->num_threads);
    pthread_mutex_unlock(&(pool->thcount_lock));

    while (pool->is_alive) {

        pthread_mutex_lock(&(pool->queue.has_jobs->mutex));
        /*always check if pool is still alive*/
        while (pool->is_alive && !(pool->queue.has_jobs->status)) {
            pthread_cond_wait(
                &(pool->queue.has_jobs->cond), &(pool->queue.has_jobs->mutex));
        }
        pthread_mutex_unlock(&(pool->queue.has_jobs->mutex));

        if (pool->is_alive) {

            pthread_mutex_lock(&(pool->thcount_lock));
            ++(pool->num_working);
            pthread_mutex_unlock(&(pool->thcount_lock));

            void* (*func)(void*);
            void* arg;

            task* curtask = take_taskqueue(&pool->queue);
            if (curtask) {
                func = curtask->function;
                arg = (curtask->arg);
                (*(thread_info**)(arg)) = &(threadinfo);
                func(arg);
                free(curtask->arg);
                free(curtask);
            }
        }
        pthread_mutex_lock(&(pool->thcount_lock));
        --(pool->num_working);
        if (pool->num_working == 0)
            pthread_cond_signal(&(pool->threads_all_idle));
        pthread_mutex_unlock(&(pool->thcount_lock));
    }

    pthread_mutex_lock(&(pool->thcount_lock));
    --(pool->num_threads);
    pthread_mutex_unlock(&(pool->thcount_lock));

    return NULL;
}
int init_taskqueue(struct taskqueue* queue)
{
    pthread_mutex_init(&queue->mutex, NULL);
    queue->front = NULL;
    queue->rear = NULL;
    queue->len = 0;
    if ((queue->has_jobs = (staconv*)malloc(sizeof(staconv))) == NULL)
        return -1;
    queue->has_jobs->status = 0;
    pthread_mutex_init((&queue->has_jobs->mutex), NULL);
    pthread_cond_init((&queue->has_jobs->cond), NULL);
    return 0;
}
int push_taskqueue(struct taskqueue* queue, struct task* curtask)
{
    bool is_empty = false; /*check if the queue is empty*/
    if (queue == NULL || curtask == NULL || queue == NULL
        || queue->has_jobs == NULL)
        return -1;

    pthread_mutex_lock(&queue->mutex);
    if (queue->front == NULL) {
        is_empty = true;
        queue->front = curtask;
        queue->rear = curtask;
        curtask->pre = NULL;
    } else {
        queue->rear->next = curtask;
        curtask->pre = queue->rear;
        queue->rear = curtask;
    }
    curtask->next = NULL;
    ++(queue->len);
    pthread_mutex_unlock(&queue->mutex);

    if (is_empty) {
        pthread_mutex_lock(&(queue->has_jobs->mutex));
        queue->has_jobs->status = 1;
        pthread_cond_broadcast(&(queue->has_jobs->cond));
        pthread_mutex_unlock(&(queue->has_jobs->mutex));
    }
    return 0;
}
int destory_taskqueue(struct taskqueue* queue)
{
    pthread_mutex_lock(&(queue->mutex));
    if (queue->front != NULL || queue->rear != NULL || queue->len != 0) {
        pthread_mutex_unlock(&(queue->mutex));
        return -1;
    }
    pthread_mutex_unlock(&(queue->mutex));

    pthread_mutex_destroy(&(queue->has_jobs->mutex));
    pthread_cond_destroy(&(queue->has_jobs->cond));
    free(queue->has_jobs);
    pthread_mutex_destroy(&(queue->mutex));

    return 0;
}
task* take_taskqueue(struct taskqueue* queue)
{
    struct task* t;
    bool is_empty = false;
    pthread_mutex_lock(&(queue->mutex));
    if (queue->front == NULL)
        is_empty = true;
    else {
        t = queue->rear;
        queue->rear = queue->rear->pre;
        --(queue->len);

        /*if queue is empty after pop*/
        if (queue->rear == NULL) {
            queue->front = NULL;
            pthread_mutex_lock(&(queue->has_jobs->mutex));
            queue->has_jobs->status = 0;
            pthread_mutex_unlock(&(queue->has_jobs->mutex));
        } else {
            queue->rear->next = NULL;
        }
        t->next = NULL;
        t->pre = NULL;
    }
    pthread_mutex_unlock(&(queue->mutex));
    if (is_empty)
        return NULL;
    return t;
}
void error(const char* msg, const char* log_name)
{
    static struct log_file logf;
    if (logf.log_name != NULL && logf.log_name != log_name) {
        logf.log_name = log_name;
        logf.log_len = 0;
        logf.out_buffer[0] = 0;
    }
    logger(ERROR, msg, "", 0, 1, &logf);
}
