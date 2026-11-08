/* Differential-test and optional microbenchmark driver; stdin is the message. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef LOCKSTEP_SHA256_TEST_CPUID
static unsigned mock_max_leaf, mock_leaf1_ecx, mock_leaf7_ebx;
static void lockstep_sha256_test_cpuid(unsigned *max_leaf, unsigned *leaf1_ecx, unsigned *leaf7_ebx) {
    *max_leaf = mock_max_leaf; *leaf1_ecx = mock_leaf1_ecx; *leaf7_ebx = mock_leaf7_ebx;
}
#endif
#include "lockstep_sha256.h"

static int hash_input(int hardware, size_t chunk, size_t offset) {
    size_t length = 0, capacity = 4096;
    unsigned char *buffer = malloc(capacity + offset);
    if (!buffer) return 2;
    for (;;) {
        if (length == capacity) {
            capacity *= 2;
            unsigned char *next = realloc(buffer, capacity + offset);
            if (!next) { free(buffer); return 2; }
            buffer = next;
        }
        size_t got = fread(buffer + offset + length, 1, capacity - length, stdin);
        length += got;
        if (!got) { if (ferror(stdin)) { free(buffer); return 2; } break; }
    }
    lockstep_sha256_ctx context;
    lockstep_sha256_init(&context);
    if (hardware >= 0) context.hardware = hardware;
    for (size_t position = 0; position < length;) {
        size_t take = length - position;
        if (chunk && take > chunk) take = chunk;
        lockstep_sha256_update(&context, buffer + offset + position, take);
        position += take;
    }
    unsigned char digest[32];
    lockstep_sha256_final(&context, digest);
    for (unsigned i = 0; i < 32; ++i) printf("%02x", digest[i]);
    putchar('\n');
    free(buffer);
    return 0;
}

static double seconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) exit(2);
    return now.tv_sec + now.tv_nsec / 1e9;
}

static void benchmark_one(size_t length, unsigned rounds, int hardware) {
    unsigned char *buffer = malloc(length ? length : 1);
    if (!buffer) exit(2);
    for (size_t i = 0; i < length; ++i) buffer[i] = (unsigned char)(i * 37u + 11u);
    unsigned char digest[32];
    volatile unsigned checksum = 0;
    double begin = seconds();
    for (unsigned i = 0; i < rounds; ++i) {
        if (length) buffer[0] = (unsigned char)i;
        lockstep_sha256_ctx context;
        lockstep_sha256_init(&context); context.hardware = hardware;
        lockstep_sha256_update(&context, buffer, length);
        lockstep_sha256_final(&context, digest);
        for (unsigned j = 0; j < sizeof(digest); ++j) checksum += digest[j];
    }
    double elapsed = seconds() - begin;
    printf("%s bytes=%zu rounds=%u seconds=%.6f MiB_per_s=%.2f checksum=%u\n",
           hardware ? "sha-ni" : "scalar", length, rounds, elapsed,
           (double)length * rounds / (1024.0 * 1024.0 * elapsed), checksum);
    free(buffer);
}

enum { TEST_THREADS = 16 };
static pthread_barrier_t start_barrier;
static int thread_failed[TEST_THREADS], thread_hardware[TEST_THREADS];
static void *hash_thread(void *argument) {
    size_t index = (size_t)argument;
    static const unsigned char expected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    unsigned char digest[32]; lockstep_sha256_ctx context;
    pthread_barrier_wait(&start_barrier);
    lockstep_sha256_init(&context); thread_hardware[index] = context.hardware;
    lockstep_sha256_update(&context, "abc", 3); lockstep_sha256_final(&context, digest);
    thread_failed[index] = memcmp(digest, expected, sizeof(digest)) != 0;
    return NULL;
}
static int concurrent_initialization(void) {
    pthread_t threads[TEST_THREADS];
    if (pthread_barrier_init(&start_barrier, NULL, TEST_THREADS)) return 2;
    for (size_t i = 0; i < TEST_THREADS; ++i)
        if (pthread_create(&threads[i], NULL, hash_thread, (void *)i)) return 2;
    for (unsigned i = 0; i < TEST_THREADS; ++i) if (pthread_join(threads[i], NULL)) return 2;
    pthread_barrier_destroy(&start_barrier);
    for (unsigned i = 0; i < TEST_THREADS; ++i)
        if (thread_failed[i] || thread_hardware[i] != thread_hardware[0]) return 1;
    return 0;
}
static int compress_block(int hardware) {
    unsigned char input[96];
    if (fread(input, 1, sizeof(input), stdin) != sizeof(input)) return 2;
    lockstep_sha256_ctx context; lockstep_sha256_init(&context); context.hardware = hardware;
    for (unsigned i = 0; i < 8; ++i)
        context.h[i] = (uint32_t)input[4*i]<<24 | (uint32_t)input[4*i+1]<<16 |
                       (uint32_t)input[4*i+2]<<8 | input[4*i+3];
    lockstep_sha256_compress(&context, input + 32);
    for (unsigned i = 0; i < 8; ++i) printf("%08x", context.h[i]);
    putchar('\n'); return 0;
}

int main(int argc, char **argv) {
    (void)lockstep_sha256;
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "threads")) return concurrent_initialization();
#ifdef LOCKSTEP_SHA256_TEST_CPUID
    if (!strcmp(argv[1], "cpuid")) {
        if (argc != 5) return 2;
        mock_max_leaf = (unsigned)strtoul(argv[2], NULL, 0);
        mock_leaf1_ecx = (unsigned)strtoul(argv[3], NULL, 0);
        mock_leaf7_ebx = (unsigned)strtoul(argv[4], NULL, 0);
        lockstep_sha256_ctx context; lockstep_sha256_init(&context);
        printf("%d", context.hardware);
        if (!context.hardware) {
            unsigned char digest[32];
            lockstep_sha256_update(&context, "abc", 3);
            lockstep_sha256_final(&context, digest);
            putchar(' ');
            for (unsigned i = 0; i < sizeof(digest); ++i) printf("%02x", digest[i]);
        }
        putchar('\n'); return 0;
    }
#endif
    int available = lockstep_sha256_hardware_available();
    if (!strcmp(argv[1], "compress")) {
        if (argc != 3) return 2;
        int hardware = !strcmp(argv[2], "hardware");
        if (hardware && !available) return 77;
        return compress_block(hardware);
    }
    if (!strcmp(argv[1], "available")) { puts(available ? "1" : "0"); return 0; }
    if (!strcmp(argv[1], "bench")) {
        const size_t lengths[] = {64, 256, 1024, 80u * 1024 * 1024};
        const unsigned rounds[] = {100000, 30000, 10000, 1};
        for (unsigned i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i) {
            benchmark_one(lengths[i], rounds[i], 0);
            if (available) benchmark_one(lengths[i], rounds[i], 1);
        }
        return 0;
    }
    if (argc != 4 || (strcmp(argv[1], "scalar") && strcmp(argv[1], "hardware") && strcmp(argv[1], "auto"))) return 2;
    int hardware = !strcmp(argv[1], "hardware") ? 1 : !strcmp(argv[1], "auto") ? -1 : 0;
    if (hardware == 1 && !available) return 77;
    return hash_input(hardware, (size_t)strtoul(argv[2], NULL, 10), (size_t)strtoul(argv[3], NULL, 10));
}
