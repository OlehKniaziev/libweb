#include "pool.h"

typedef struct sync_pool_node_header {
    struct sync_pool_node_header *Next;
} sync_pool_node_header;

void WebSyncPoolInit(web_sync_pool *Pool, web_arena *Arena, sz Size, sz Align) {
    WEB_STRUCT_ZERO(Pool);
    Pool->Arena = Arena;
    Pool->AllocSize = Size;
    Pool->AllocAlign = Align;
    WebMutexInit(&Pool->Mu);
}

void *WebSyncPoolAlloc(web_sync_pool *Pool) {
    WebMutexLock(&Pool->Mu);

    web_sync_pool_node Node = NULL;

    if (Pool->Head == NULL) {
        sz NodeAllocSize = sizeof(sync_pool_node_header) + Pool->AllocSize;
        NodeAllocSize = WebAlignForward(NodeAllocSize, Pool->AllocAlign);

        Node = WEB_ARENA_PUSH_ZERO(Pool->Arena, NodeAllocSize, 1);

        goto Cleanup;
    }

    Node = Pool->Head;
    Pool->Head = ((sync_pool_node_header *)Pool->Head)->Next;

Cleanup:
    WebMutexUnlock(&Pool->Mu);

    sz DataOffset = WebAlignForward(sizeof(sync_pool_node_header), Pool->AllocAlign);
    return Node + DataOffset;
}

void WebSyncPoolFree(web_sync_pool *Pool, void *Item) {
    WebMutexLock(&Pool->Mu);

    sz HeaderOffset = -sizeof(sync_pool_node_header);
    HeaderOffset += sizeof(sync_pool_node_header) - WebAlignForward(sizeof(sync_pool_node_header), Pool->AllocAlign);

    sync_pool_node_header *Header = (sync_pool_node_header *)(Item + HeaderOffset);
    Header->Next = Pool->Head;
    Pool->Head = Header;

    WebMutexUnlock(&Pool->Mu);
}
