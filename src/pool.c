#include "pool.h"

void WebSyncPoolInit(web_sync_pool *Pool, web_sync_pool_new_proc NewProc) {
    WEB_STRUCT_ZERO(Pool);
    Pool->NewProc = NewProc;
    WebMutexInit(&Pool->Mu);
}

void *WebSyncPoolAlloc(web_sync_pool *Pool) {
    WebMutexLock(&Pool->Mu);

    web_sync_pool_node *Node = NULL;

    if (Pool->Head == NULL) {
        uz ItemSize = 0;
        // FIXME(oleh): Avoid the double alloc.
        void *ItemData = Pool->NewProc(&ItemSize);
        // TODO(oleh): Replace this malloc by injecting an arena or something.
        Node = malloc(sizeof(web_sync_pool_node) + ItemSize);
        WEB_STRUCT_ZERO(Node);
        memcpy(Node->Item, ItemData, ItemSize);
        goto Cleanup;
    }

    Node = Pool->Head;
    Pool->Head = Pool->Head->Next;

Cleanup:
    WebMutexUnlock(&Pool->Mu);
    return Node->Item;
}

void WebSyncPoolFree(web_sync_pool *Pool, void *Item) {
    WebMutexLock(&Pool->Mu);

    web_sync_pool_node *Node = (web_sync_pool_node *)((u8 *)Item - sizeof(Node->Next));
    Node->Next = Pool->Head;
    Pool->Head = Node;

    WebMutexUnlock(&Pool->Mu);
}
