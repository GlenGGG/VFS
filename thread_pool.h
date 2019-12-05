#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H
#include <pthread.h>
#include <stdbool.h>
#include <sys/prctl.h>
#include "webserver.h"

#define THREAD_NUM 17


typedef  struct staconv {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int status;
} staconv;

typedef struct task {
    struct task* next;
    struct task* pre;
    void* (*function)(void* arg);
    void* arg;
} task;
\
typedef struct taskqueue {
    pthread_mutex_t mutex;
    task* front;
    task* rear;
    staconv* has_jobs;
    int len;
} taskqueue;

/*used for logging separatly*/
typedef struct thread_info {
    struct log_file* logf;
    const char* thread_name;
    const int* id;
    time_counter* counter;
} thread_info;

typedef struct thread {
    int id;
    pthread_t pthread;
    struct threadpool* pool;
    struct thread_info* threadinfo;
} thread;

typedef struct threadpool {
    thread** threads;
    volatile int num_threads;
    volatile int num_working;
    pthread_mutex_t thcount_lock;
    pthread_cond_t threads_all_idle;
    taskqueue queue;
    volatile bool is_alive;
} threadpool;

void error(const char* msg, const char* log_name);
struct threadpool* initThreadPool(int num_threas);
void addTask2ThreadPool(threadpool* pool, task* curtask);
void waitThreadPool(threadpool* pool);
int destoryThreadPool(threadpool* pool);
int getNumofThreadWorking(threadpool* pool);
int create_thread(struct threadpool* pool, struct thread** pthread, int id);
void* thread_do(struct thread* pthread);
int init_taskqueue(struct taskqueue* queue);
int push_taskqueue(struct taskqueue* queue, struct task* curtask);
int destory_taskqueue(struct taskqueue* queue);
task* take_taskqueue(struct taskqueue* queue);

#endif
