#define _GNU_SOURCE
#include "context_refresh_edges.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

static unsigned calls;
static uintptr_t forwarded;
static uintptr_t original(uintptr_t arg) { calls++; forwarded = arg; return arg ^ 0xfaceu; }
static int authenticate(void *ctx, const void *object, cre_phase phase) {
    return object == ctx && (phase == CRE_BIND || phase == CRE_DESTRUCT);
}
static cre_token nested;
static int reenter(void *ctx, const void *object, cre_phase phase) {
    assert(!cre_begin(&nested, object, phase, authenticate, ctx));
    return 1;
}
static uintptr_t bind_wrapper(uintptr_t arg) { return cre_dispatch(CRE_BIND, arg, cre_current ? cre_current->object : NULL, original); }
static uintptr_t destruct_wrapper(uintptr_t arg) { return cre_dispatch(CRE_DESTRUCT, arg, cre_current ? cre_current->object : NULL, original); }
static void *wrong_thread(void *ptr) {
    cre_token *token = ptr;
    assert(cre_dispatch(CRE_BIND, 0x99, cre_current ? cre_current->object : NULL, original) == (0x99 ^ 0xfaceu));
    assert(!cre_end(token, 0));
    return NULL;
}
int main(void) {
    cre_token token = {0};
    int object = 1, other = 2;
    assert(cre_dispatch(CRE_BIND, 0x1234, cre_current ? cre_current->object : NULL, original) == (0x1234 ^ 0xfaceu));
    assert(calls == 1 && forwarded == 0x1234);
    assert(!cre_begin(&token, &object, CRE_BIND, authenticate, &other));
    assert(atomic_load(&token.state) == 0);
    assert(cre_begin(&token, &object, CRE_BIND, authenticate, &object));
    assert(cre_dispatch(CRE_BIND, 1, cre_current ? cre_current->object : NULL, original) == 0 && calls == 1);
    assert(cre_end(&token, 1));
    assert(cre_begin(&token, &object, CRE_DESTRUCT, authenticate, &object));
    assert(cre_dispatch(CRE_DESTRUCT, 0, cre_current ? cre_current->object : NULL, original) == 0 && calls == 1);
    assert(cre_end(&token, 1));
    assert(cre_begin(&token, &object, CRE_DESTRUCT, authenticate, &object));
    assert(cre_end(&token, 0));
    assert(cre_begin(&token, &object, CRE_BIND, authenticate, &object));
    assert(cre_dispatch(CRE_DESTRUCT, 7, cre_current ? cre_current->object : NULL, original) == (7 ^ 0xfaceu));
    assert(!cre_end(&token, 0));
    assert(atomic_load(&token.state) == 2);
    assert(!cre_begin(&token, &object, CRE_BIND, authenticate, &object));
    cre_token reentrant = {0};
    assert(!cre_begin(&reentrant, &object, CRE_BIND, reenter, &object));
    assert(atomic_load(&reentrant.state) == 2);
    cre_token repeat = {0};
    assert(cre_begin(&repeat, &object, CRE_BIND, authenticate, &object));
    assert(cre_dispatch(CRE_BIND, 1, cre_current ? cre_current->object : NULL, original) == 0);
    assert(cre_dispatch(CRE_BIND, 2, cre_current ? cre_current->object : NULL, original) == (2 ^ 0xfaceu));
    assert(!cre_end(&repeat, 1));
    cre_token threaded = {0}; pthread_t thread;
    assert(cre_begin(&threaded, &object, CRE_BIND, authenticate, &object));
    assert(!pthread_create(&thread, NULL, wrong_thread, &threaded));
    assert(!pthread_join(thread, NULL));
    assert(!cre_end(&threaded, 0));
    assert(atomic_load(&threaded.state) == 2);
    unsigned char edge[5];
    assert(cre_encode_edge(edge, 0xe8, CRE_DESTRUCTOR_EDGE_VA, CRE_REFRESH_VA));
    assert(!memcmp(edge, cre_destructor_original, 5));
    assert(!cre_encode_edge(edge, 0xe8, 0x100, UINT64_MAX));
    assert(!cre_encode_edge(edge, 0x90, 0x100, 0x200));
    long page_size = sysconf(_SC_PAGESIZE);
    assert(page_size >= 28);
    unsigned char *page = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    cre_emit_jump(page, (uintptr_t)bind_wrapper);
    cre_emit_jump(page + 14, (uintptr_t)destruct_wrapper);
    assert(!mprotect(page, (size_t)page_size, PROT_READ | PROT_EXEC));
    uintptr_t (*bind_stub)(uintptr_t) = (void *)page;
    uintptr_t (*destruct_stub)(uintptr_t) = (void *)(page + 14);
    assert(bind_stub(0x5678) == (0x5678 ^ 0xfaceu));
    assert(destruct_stub(0x6789) == (0x6789 ^ 0xfaceu));
    cre_token via_stub = {0};
    assert(cre_begin(&via_stub, &object, CRE_BIND, authenticate, &object));
    unsigned previous_calls = calls;
    assert(bind_stub(0x3456) == 0 && calls == previous_calls);
    assert(cre_end(&via_stub, 1));
    assert(!munmap(page, (size_t)page_size));
    puts("context refresh policy and stubs: ok");
}
