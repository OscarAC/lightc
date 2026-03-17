/*
 * musl malloc benchmark — same tests as bench_heap.c but using libc malloc.
 * Compiled WITH libc for comparison.
 *
 * Build:  gcc -O2 -o bench_musl examples/bench_musl.c -lpthread
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define ITERS      1000000
#define BATCH_SIZE 256

static void bench_alloc_free(const char *label, size_t size) {
    printf("%s", label);

    for (int i = 0; i < 1000; i++) { void *p = malloc(size); *(volatile char *)p = 0; free(p); }

    uint64_t start = rdtsc();
    for (int i = 0; i < ITERS; i++) {
        void *p = malloc(size);
        *(volatile char *)p = 0; /* prevent optimization */
        free(p);
    }
    uint64_t elapsed = rdtsc() - start;
    printf("%lu cycles/op\n", elapsed / ITERS);
}

static void bench_batch(const char *label, size_t size) {
    printf("%s", label);
    void *ptrs[BATCH_SIZE];

    for (int i = 0; i < BATCH_SIZE; i++) ptrs[i] = malloc(size);
    for (int i = 0; i < BATCH_SIZE; i++) free(ptrs[i]);

    int rounds = ITERS / BATCH_SIZE;
    uint64_t start = rdtsc();
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < BATCH_SIZE; i++)
            ptrs[i] = malloc(size);
        for (int i = 0; i < BATCH_SIZE; i++)
            free(ptrs[i]);
    }
    uint64_t elapsed = rdtsc() - start;
    uint64_t ops = (uint64_t)rounds * BATCH_SIZE * 2;
    printf("%lu cycles/op\n", elapsed / ops);
}

static void bench_mixed(void) {
    printf("  mixed sizes (batch):    ");
    void *ptrs[BATCH_SIZE];
    static const size_t sizes[] = {16, 64, 100, 200, 500, 1000, 2000, 4000, 8, 48};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int i = 0; i < BATCH_SIZE; i++) ptrs[i] = malloc(sizes[i % nsizes]);
    for (int i = 0; i < BATCH_SIZE; i++) free(ptrs[i]);

    int rounds = ITERS / BATCH_SIZE;
    uint64_t start = rdtsc();
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < BATCH_SIZE; i++)
            ptrs[i] = malloc(sizes[i % nsizes]);
        for (int i = 0; i < BATCH_SIZE; i++)
            free(ptrs[i]);
    }
    uint64_t elapsed = rdtsc() - start;
    uint64_t ops = (uint64_t)rounds * BATCH_SIZE * 2;
    printf("%lu cycles/op\n", elapsed / ops);
}

typedef struct {
    uint64_t cycles;
    uint64_t ops;
} bench_result;

static void *thread_bench(void *arg) {
    bench_result *result = (bench_result *)arg;
    void *ptrs[BATCH_SIZE];
    int rounds = ITERS / BATCH_SIZE;

    uint64_t start = rdtsc();
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < BATCH_SIZE; i++)
            ptrs[i] = malloc(64);
        for (int i = 0; i < BATCH_SIZE; i++)
            free(ptrs[i]);
    }
    uint64_t elapsed = rdtsc() - start;

    result->cycles = elapsed;
    result->ops = (uint64_t)rounds * BATCH_SIZE * 2;
    return NULL;
}

static void bench_threaded(int nthreads) {
    printf("  64B batch (%d threads):  ", nthreads);

    pthread_t threads[8];
    bench_result results[8];

    for (int i = 0; i < nthreads; i++)
        pthread_create(&threads[i], NULL, thread_bench, &results[i]);
    for (int i = 0; i < nthreads; i++)
        pthread_join(threads[i], NULL);

    uint64_t total_ops = 0, max_cycles = 0;
    for (int i = 0; i < nthreads; i++) {
        total_ops += results[i].ops;
        if (results[i].cycles > max_cycles) max_cycles = results[i].cycles;
    }
    printf("%lu cycles/op (%luM ops total)\n", max_cycles / total_ops, total_ops / 1000000);
}

int main(void) {
    printf("musl malloc benchmark\n");
    printf("---------------------\n");

    printf("alloc+free pairs (1M iterations):\n");
    bench_alloc_free("  16 bytes:               ", 16);
    bench_alloc_free("  64 bytes:               ", 64);
    bench_alloc_free("  256 bytes:              ", 256);
    bench_alloc_free("  2000 bytes:             ", 2000);

    printf("\nbatch alloc then free (256 per batch):\n");
    bench_batch("  16 bytes:               ", 16);
    bench_batch("  64 bytes:               ", 64);
    bench_batch("  256 bytes:              ", 256);
    bench_mixed();

    printf("\nmulti-threaded (batch alloc/free):\n");
    bench_threaded(1);
    bench_threaded(2);
    bench_threaded(4);

    return 0;
}
