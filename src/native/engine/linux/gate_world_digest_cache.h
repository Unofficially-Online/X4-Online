/* Exact-content cache for Linux world leaf SHA-256 digests.
 *
 * Single-threaded: the world capture's owning thread alone may use a cache.
 * Only the serialized leaf bytes are cached; callers must still read and
 * serialize every object and separately hash identities and root records.
 * Entries remain valid across captures and schemas because hits require an
 * exact length and byte-for-byte comparison of the complete hash input.
 */
#ifndef GATE_WORLD_DIGEST_CACHE_H
#define GATE_WORLD_DIGEST_CACHE_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../lockstep_sha256.h"

#define WORLD_DIGEST_CACHE_SLOTS 16384u
#define WORLD_DIGEST_CACHE_MAX_INPUT 128u
_Static_assert((WORLD_DIGEST_CACHE_SLOTS & (WORLD_DIGEST_CACHE_SLOTS - 1u)) == 0,
               "digest cache slot count must be a power of two");

typedef struct {
    unsigned char input[WORLD_DIGEST_CACHE_MAX_INPUT];
    unsigned char digest[32];
    size_t length;
    unsigned char occupied;
} world_digest_cache_entry;

typedef void *(*world_digest_cache_calloc_fn)(size_t count, size_t size);

typedef struct {
    world_digest_cache_entry *entries;
    world_digest_cache_calloc_fn allocate;
    uint64_t hits, misses, uncached;
    int allocation_attempted;
} world_digest_cache;

/* Initialize once before use. Pass NULL for the normal calloc allocator.
 * The allocator is injectable for failure tests; successful allocations must
 * be zero-initialized and compatible with free(). */
static inline void world_digest_cache_init(world_digest_cache *cache, world_digest_cache_calloc_fn allocate) {
    memset(cache, 0, sizeof(*cache));
    cache->allocate = allocate ? allocate : calloc;
}

/* Only three short fixed-position samples are used to select a slot. This is
 * not an identity or correctness check: every hit compares the entire input. */
static inline size_t world_digest_cache_slot(const unsigned char *input, size_t length) {
    uint64_t first = 0, middle = 0, last = 0;
    size_t width = length < sizeof(first) ? length : sizeof(first);
    if (width) {
        memcpy(&first, input, width);
        memcpy(&middle, input + (length - width) / 2, width);
        memcpy(&last, input + length - width, width);
    }
    uint64_t hash = (uint64_t)length ^ first ^
                    ((middle << 21) | (middle >> 43)) ^
                    ((last << 42) | (last >> 22));
    hash ^= hash >> 33;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33;
    hash *= UINT64_C(0xc4ceb9fe1a85ec53);
    hash ^= hash >> 33;
    return (size_t)(hash & (WORLD_DIGEST_CACHE_SLOTS - 1u));
}

/* input must be readable for length bytes. NULL is accepted only for an
 * empty input. Allocation failure is remembered and always hashes directly. */
static inline void world_digest_cache_hash(world_digest_cache *cache, const void *input,
                                           size_t length, unsigned char digest[32]) {
    static const unsigned char empty = 0;
    const unsigned char *bytes = length ? (const unsigned char *)input : &empty;
    if (length > WORLD_DIGEST_CACHE_MAX_INPUT) {
        ++cache->uncached;
        lockstep_sha256(bytes, length, digest);
        return;
    }
    if (!cache->allocation_attempted) {
        cache->allocation_attempted = 1;
        cache->entries = cache->allocate(WORLD_DIGEST_CACHE_SLOTS, sizeof(*cache->entries));
    }
    if (!cache->entries) {
        ++cache->uncached;
        lockstep_sha256(bytes, length, digest);
        return;
    }
    world_digest_cache_entry *entry = &cache->entries[world_digest_cache_slot(bytes, length)];
    if (entry->occupied && entry->length == length && (!length || !memcmp(entry->input, bytes, length))) {
        ++cache->hits;
        memcpy(digest, entry->digest, 32);
        return;
    }
    ++cache->misses;
    unsigned char computed[32];
    lockstep_sha256(bytes, length, computed);
    if (length) memmove(entry->input, bytes, length);
    memcpy(entry->digest, computed, 32);
    entry->length = length;
    entry->occupied = 1;
    memcpy(digest, computed, 32);
}

/* Optional for tests or explicit teardown; the world capture normally keeps
 * the small table for its process lifetime. */
static inline void world_digest_cache_release(world_digest_cache *cache) {
    free(cache->entries);
    cache->entries = NULL;
    cache->allocation_attempted = 1;
}
#endif
