#include <errno.h>
#include "threadpool.h"

b32 WebThreadLaunch(web_thread *Thread, web_thread_proc ThreadProc, void *ThreadProcArg) {
    pthread_attr_t Attr;
    pthread_attr_init(&Attr);
    int Status = pthread_create(&Thread->Id, &Attr, ThreadProc, ThreadProcArg);
    return Status == 0;
}

static void *ThreadPoolWorkerProc(void *Arg) {
    web_thread_pool *ThreadPool = (web_thread_pool *)Arg;

    while (1) {
        WebMutexLock(&ThreadPool->QueueCondMu);

        while (ThreadPool->QueueHead == ThreadPool->QueueTail) {
            pthread_cond_wait(&ThreadPool->QueueCondVar, &ThreadPool->QueueCondMu.Inner);
        }

        web_thread_pool_task Task = ThreadPool->QueueItems[ThreadPool->QueueHead];
        ThreadPool->QueueHead = (ThreadPool->QueueHead + 1) % ThreadPool->QueueCapacity;

        WebMutexUnlock(&ThreadPool->QueueCondMu);

        Task.Proc(Task.Arg);
    }

    return NULL;
}

b32 WebThreadPoolInit(web_thread_pool *ThreadPool, web_arena *Arena, web_thread_pool_config *Config) {
    ThreadPool->Arena = Arena;
    ThreadPool->ThreadsCount = Config->NumThreads;

    ThreadPool->Threads = WEB_ARENA_PUSH_ZERO(Arena, sizeof(*ThreadPool->Threads) * ThreadPool->ThreadsCount);

    for (uz I = 0; I < ThreadPool->ThreadsCount; ++I) {
        if (!WebThreadLaunch(&ThreadPool->Threads[I], ThreadPoolWorkerProc, ThreadPool)) return 0;
    }

    ThreadPool->QueueHead = 0;
    ThreadPool->QueueTail = 0;

    ThreadPool->QueueCondVar = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
    WebMutexInit(&ThreadPool->QueueCondMu);

    ThreadPool->QueueCapacity = 128;
    ThreadPool->QueueItems = WEB_ARENA_PUSH_ZERO(Arena, sizeof(*ThreadPool->QueueItems) * ThreadPool->QueueCapacity);

    return 1;
}

void WebThreadPoolScheduleTask(web_thread_pool *ThreadPool, web_thread_pool_task Task) {
    WebMutexLock(&ThreadPool->QueueCondMu);

    b32 WakeUpWorkers = ThreadPool->QueueHead == ThreadPool->QueueTail;

    ThreadPool->QueueItems[ThreadPool->QueueTail] = Task;

    ThreadPool->QueueTail = (ThreadPool->QueueTail + 1) % ThreadPool->QueueCapacity;

    if (ThreadPool->QueueTail == ThreadPool->QueueHead) {
        uz NewCapacity = ThreadPool->QueueCapacity * 2;
        ThreadPool->QueueItems = WebArenaRealloc(ThreadPool->Arena,
                                                 ThreadPool->QueueItems,
                                                 sizeof(*ThreadPool->QueueItems) * ThreadPool->QueueCapacity,
                                                 sizeof(*ThreadPool->QueueItems) * NewCapacity);
        ThreadPool->QueueCapacity = NewCapacity;
    }

    WebMutexUnlock(&ThreadPool->QueueCondMu);

    if (WakeUpWorkers) {
        pthread_cond_broadcast(&ThreadPool->QueueCondVar);
    }
}

void WebMutexInit(web_mutex *Mu) {
    pthread_mutexattr_t Attrs = {0};
    pthread_mutexattr_init(&Attrs);
    pthread_mutexattr_settype(&Attrs, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&Mu->Inner, &Attrs);
}

void WebMutexLock(web_mutex *Mu) {
    int Err = pthread_mutex_lock(&Mu->Inner);
    if (Err == EDEADLK) WEB_PANIC("Deadlock: the calling thread is already holding the mutex");
    if (Err == EINVAL) WEB_PANIC("Invalid mutex state. Was it properly initialized?");
}

void WebMutexUnlock(web_mutex *Mu) {
    int Err = pthread_mutex_unlock(&Mu->Inner);
    if (Err == EINVAL) WEB_PANIC("Invalid mutex state. Was it properly initialized?");
}

b32 WebMutexTryLock(web_mutex *Mu) {
    int Err = pthread_mutex_unlock(&Mu->Inner);
    if (Err == EINVAL) WEB_PANIC("Invalid mutex state. Was it properly initialized?");
    return Err != EBUSY;
}
