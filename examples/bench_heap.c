/*
 * Heap allocator benchmark.
 *
 * Measures allocation/free throughput for:
 *   - Small allocations (32 bytes)
 *   - Medium allocations (256 bytes)
 *   - Mixed sizes (random-ish pattern)
 *   - Multi-threaded (4 threads)
 *
 * Uses rdtsc for cycle-accurate timing on x86_64.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/heap.h>
#include <lightc/thread.h>

#define S(literal) literal, sizeof(literal) - 1

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* --- Benchmarks --- */

#define ITERS      1000000
#define BATCH_SIZE 256

static void bench_alloc_free(const char *label, size_t label_len, size_t size) {
    lc_print_string(STDOUT, label, label_len);

    /* Warm up */
    for (int i = 0; i < 1000; i++) {
        void *p = lc_heap_allocate(size);
        lc_heap_free(p);
    }

    /* Measure alloc+free pairs */
    uint64_t start = rdtsc();
    for (int i = 0; i < ITERS; i++) {
        void *p = lc_heap_allocate(size);
        *(volatile char *)p = 0; /* prevent optimization */
        lc_heap_free(p);
    }
    uint64_t elapsed = rdtsc() - start;

    lc_print_unsigned(STDOUT, elapsed / ITERS);
    lc_print_line(STDOUT, S(" cycles/op"));
}

static void bench_batch(const char *label, size_t label_len, size_t size) {
    lc_print_string(STDOUT, label, label_len);

    void *ptrs[BATCH_SIZE];

    /* Warm up */
    for (int i = 0; i < BATCH_SIZE; i++) ptrs[i] = lc_heap_allocate(size);
    for (int i = 0; i < BATCH_SIZE; i++) lc_heap_free(ptrs[i]);

    /* Measure: allocate a batch, then free the batch */
    int rounds = ITERS / BATCH_SIZE;
    uint64_t start = rdtsc();
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < BATCH_SIZE; i++)
            ptrs[i] = lc_heap_allocate(size);
        for (int i = 0; i < BATCH_SIZE; i++)
            lc_heap_free(ptrs[i]);
    }
    uint64_t elapsed = rdtsc() - start;
    uint64_t ops = (uint64_t)rounds * BATCH_SIZE * 2; /* alloc + free */

    lc_print_unsigned(STDOUT, elapsed / ops);
    lc_print_line(STDOUT, S(" cycles/op"));
}

static void bench_mixed(void) {
    lc_print_string(STDOUT, S("  mixed sizes (batch):    "));

    void *ptrs[BATCH_SIZE];
    /* Sizes cycle: 16, 64, 100, 200, 500, 1000, 2000, 4000, 8, 48 ... */
    static const size_t sizes[] = {16, 64, 100, 200, 500, 1000, 2000, 4000, 8, 48};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);

    /* Warm up */
    for (int i = 0; i < BATCH_SIZE; i++) ptrs[i] = lc_heap_allocate(sizes[i % nsizes]);
    for (int i = 0; i < BATCH_SIZE; i++) lc_heap_free(ptrs[i]);

    int rounds = ITERS / BATCH_SIZE;
    uint64_t start = rdtsc();
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < BATCH_SIZE; i++)
            ptrs[i] = lc_heap_allocate(sizes[i % nsizes]);
        for (int i = 0; i < BATCH_SIZE; i++)
            lc_heap_free(ptrs[i]);
    }
    uint64_t elapsed = rdtsc() - start;
    uint64_t ops = (uint64_t)rounds * BATCH_SIZE * 2;

    lc_print_unsigned(STDOUT, elapsed / ops);
    lc_print_line(STDOUT, S(" cycles/op"));
}

/* --- Multi-threaded benchmark --- */

typedef struct {
    uint64_t cycles;
    uint64_t ops;
} bench_result;

static int32_t thread_bench(void *arg) {
    bench_result *result = (bench_result *)arg;
    void *ptrs[BATCH_SIZE];
    int rounds = ITERS / BATCH_SIZE;

    uint64_t start = rdtsc();
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < BATCH_SIZE; i++)
            ptrs[i] = lc_heap_allocate(64);
        for (int i = 0; i < BATCH_SIZE; i++)
            lc_heap_free(ptrs[i]);
    }
    uint64_t elapsed = rdtsc() - start;

    result->cycles = elapsed;
    result->ops = (uint64_t)rounds * BATCH_SIZE * 2;
    return 0;
}

static void bench_threaded(int nthreads) {
    lc_print_string(STDOUT, S("  64B batch ("));
    lc_print_unsigned(STDOUT, (uint64_t)nthreads);
    lc_print_string(STDOUT, S(" threads):  "));

    lc_thread threads[8];
    bench_result results[8];

    for (int i = 0; i < nthreads; i++)
        lc_thread_create(&threads[i], thread_bench, &results[i]);
    for (int i = 0; i < nthreads; i++)
        lc_thread_join(&threads[i]);

    /* Report total throughput */
    uint64_t total_ops = 0;
    uint64_t max_cycles = 0;
    for (int i = 0; i < nthreads; i++) {
        total_ops += results[i].ops;
        if (results[i].cycles > max_cycles) max_cycles = results[i].cycles;
    }

    /* cycles/op = max wall time / total ops across all threads */
    lc_print_unsigned(STDOUT, max_cycles / total_ops);
    lc_print_string(STDOUT, S(" cycles/op ("));
    lc_print_unsigned(STDOUT, total_ops / 1000000);
    lc_print_line(STDOUT, S("M ops total)"));
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    lc_print_line(STDOUT, S("lightc heap benchmark"));
    lc_print_line(STDOUT, S("---------------------"));

    lc_print_line(STDOUT, S("alloc+free pairs (1M iterations):"));
    bench_alloc_free(S("  16 bytes:               "), 16);
    bench_alloc_free(S("  64 bytes:               "), 64);
    bench_alloc_free(S("  256 bytes:              "), 256);
    bench_alloc_free(S("  2000 bytes:             "), 2000);

    lc_print_newline(STDOUT);
    lc_print_line(STDOUT, S("batch alloc then free (256 per batch):"));
    bench_batch(S("  16 bytes:               "), 16);
    bench_batch(S("  64 bytes:               "), 64);
    bench_batch(S("  256 bytes:              "), 256);
    bench_mixed();

    lc_print_newline(STDOUT);
    lc_print_line(STDOUT, S("multi-threaded (batch alloc/free):"));
    bench_threaded(1);
    bench_threaded(2);
    bench_threaded(4);

    return 0;
}
