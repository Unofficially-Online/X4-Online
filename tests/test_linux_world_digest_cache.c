/* Offline exact-content cache tests and ctypes bridge; no game code is run. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gate_world_digest_cache.h"

#ifdef CACHE_TEST_LIBRARY
static world_digest_cache test_cache;
void cache_test_init(void) { world_digest_cache_init(&test_cache, NULL); }
void cache_test_hash(const void *input, size_t length, unsigned char *digest) {
    world_digest_cache_hash(&test_cache, input, length, digest);
}
void cache_test_release(void) { world_digest_cache_release(&test_cache); }
#else
static void scalar_hash(const void *input, size_t length, unsigned char out[32]) {
    lockstep_sha256_ctx context;
    lockstep_sha256_init(&context); context.hardware = 0;
    lockstep_sha256_update(&context, input, length);
    lockstep_sha256_final(&context, out);
}
static void check_hash(world_digest_cache *cache, const unsigned char *bytes, size_t length) {
    unsigned char expected[32], actual[32];
    scalar_hash(bytes, length, expected);
    world_digest_cache_hash(cache, bytes, length, actual);
    assert(!memcmp(actual, expected, 32));
}
static unsigned allocation_calls;
static void *fail_allocate(size_t count, size_t size) {
    (void)count; (void)size; ++allocation_calls; return NULL;
}
int main(void) {
    world_digest_cache cache;
    world_digest_cache_init(&cache, NULL);
    unsigned char input[160];
    for (size_t i = 0; i < sizeof(input); ++i) input[i] = (unsigned char)(i * 41u + 9u);
    check_hash(&cache, input, 0); check_hash(&cache, input, 0);
    assert(cache.misses == 1 && cache.hits == 1);
    /* Each byte of every cacheable length affects the result, including bytes
     * outside the slot selector's sampled positions. */
    for (size_t length = 1; length <= WORLD_DIGEST_CACHE_MAX_INPUT; ++length) {
        check_hash(&cache, input, length);
        for (size_t position = 0; position < length; ++position) {
            input[position] ^= 0x80;
            check_hash(&cache, input, length);
            input[position] ^= 0x80;
        }
    }
    size_t first_slot = world_digest_cache_slot(input, 128);
    check_hash(&cache, input, 128);
    uint64_t misses_before = cache.misses;
    input[32] ^= 1; /* not one of the sampled bytes */
    assert(first_slot == world_digest_cache_slot(input, 128));
    check_hash(&cache, input, 128);
    assert(cache.misses == misses_before + 1);
    input[32] ^= 1;
    check_hash(&cache, input, 128); /* same slot, prior item was evicted */
    assert(cache.misses == misses_before + 2);
    uint64_t uncached_before = cache.uncached;
    check_hash(&cache, input, 129); check_hash(&cache, input, 160);
    assert(cache.uncached == uncached_before + 2);
    for (uint32_t i = 0; i < 20000; ++i) {
        memcpy(input, &i, sizeof(i));
        check_hash(&cache, input, 128);
    }
    /* Output may overlap input; cache the original bytes before returning. */
    unsigned char original[160], expected[32];
    memcpy(original, input, sizeof(input)); scalar_hash(original, 128, expected);
    world_digest_cache_hash(&cache, input, 128, input);
    assert(!memcmp(input, expected, 32));
    check_hash(&cache, original, 128);
    world_digest_cache_release(&cache);

    world_digest_cache_init(&cache, fail_allocate); allocation_calls = 0;
    check_hash(&cache, original, 129); /* large entries do not allocate */
    assert(allocation_calls == 0);
    check_hash(&cache, original, 128); check_hash(&cache, original, 128);
    assert(allocation_calls == 1 && !cache.entries && cache.uncached == 3 && !cache.hits && !cache.misses);
    world_digest_cache_release(&cache);
    puts("world digest cache PASS");
    return 0;
}
#endif
