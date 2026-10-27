#ifndef WEB_POOL_H_
#define WEB_POOL_H_

#include "threadpool.h"

typedef void * web_sync_pool_node;

typedef struct {
    web_arena *Arena;
    web_sync_pool_node Head;
    sz AllocSize;
    sz AllocAlign;
    web_mutex Mu;
} web_sync_pool;

#define WEB_SYNC_POOL_INIT_T(Pool, Arena, Type) WebSyncPoolInit((Pool), (Arena), sizeof(Type), alignof(Type))

void WebSyncPoolInit(web_sync_pool *Pool, web_arena *Arena, sz Size, sz Align);

void *WebSyncPoolAlloc(web_sync_pool *Pool);

void WebSyncPoolFree(web_sync_pool *Pool, void *Item);

#endif // WEB_POOL_H_
