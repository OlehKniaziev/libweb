#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include "common.h"

#include <pthread.h>

typedef struct {
    pthread_t Id;
} web_thread;

typedef void *(*web_thread_proc)(void *arg);

b32 WebThreadLaunch(web_thread *Thread, web_thread_proc ThreadProc, void *ThreadProcArg);

typedef void (*web_thread_pool_task_proc)(void *arg);

typedef struct {
    web_thread_pool_task_proc Proc;
    void *Arg;
} web_thread_pool_task;

typedef struct {
    web_arena *Arena;

    web_thread *Threads;
    uz ThreadsCount;

    web_thread_pool_task *QueueItems;
    uz QueueCapacity;
    uz QueueHead;
    uz QueueTail;

    pthread_cond_t QueueCondVar;
    pthread_mutex_t QueueCondMu;
} web_thread_pool;

typedef struct {
    uz NumThreads;
} web_thread_pool_config;

b32 WebThreadPoolInit(web_thread_pool *, web_arena *, web_thread_pool_config *);

void WebThreadPoolScheduleTask(web_thread_pool *, web_thread_pool_task);

#endif // THREADPOOL_H_
