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
        while (ThreadPool->QueueHead == ThreadPool->QueueTail) {
            pthread_mutex_lock(&ThreadPool->QueueCondMu);
            pthread_cond_wait(&ThreadPool->QueueCondVar, &ThreadPool->QueueCondMu);
        }

        web_thread_pool_task Task = ThreadPool->QueueItems[ThreadPool->QueueHead];
        ThreadPool->QueueHead = (ThreadPool->QueueHead + 1) % ThreadPool->QueueCapacity;

        pthread_mutex_unlock(&ThreadPool->QueueCondMu);

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
    ThreadPool->QueueCondMu = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;

    ThreadPool->QueueCapacity = 128;
    ThreadPool->QueueItems = WEB_ARENA_PUSH_ZERO(Arena, sizeof(*ThreadPool->QueueItems) * ThreadPool->QueueCapacity);

    return 1;
}

void WebThreadPoolScheduleTask(web_thread_pool *ThreadPool, web_thread_pool_task Task) {
    pthread_mutex_lock(&ThreadPool->QueueCondMu);

    if (ThreadPool->QueueHead == ThreadPool->QueueTail) {
        pthread_cond_broadcast(&ThreadPool->QueueCondVar);
    }

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

    pthread_mutex_unlock(&ThreadPool->QueueCondMu);
}
