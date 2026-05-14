#ifndef POOL_H_
#define POOL_H_

#include "threadpool.h"

typedef void *(*web_sync_pool_new_proc)(uz *Size);

typedef struct web_sync_pool_node {
    struct web_sync_pool_node *Next;
    u8 Item[];
} web_sync_pool_node;

typedef struct {
    web_sync_pool_new_proc NewProc;
    web_sync_pool_node *Head;
    web_mutex Mu;
} web_sync_pool;

void WebSyncPoolInit(web_sync_pool *Pool, web_sync_pool_new_proc NewProc);

void *WebSyncPoolAlloc(web_sync_pool *Pool);

void WebSyncPoolFree(web_sync_pool *Pool, void *Item);

#endif // POOL_H_
