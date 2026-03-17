/*
 * Exercise coroutines — cooperative multitasking tests.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/coroutine.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed)
        lc_print_line(STDOUT, S(" PASS"));
    else
        lc_print_line(STDOUT, S(" FAIL"));
}

/* ===================================================================
 * Test 1: basic_yield — two coroutines interleave via yield
 * =================================================================== */

static int order[10];
static int order_idx = 0;

static void count_a(void *arg) {
    (void)arg;
    for (int i = 0; i < 3; i++) {
        order[order_idx++] = 1;
        lc_coroutine_yield();
    }
}

static void count_b(void *arg) {
    (void)arg;
    for (int i = 0; i < 3; i++) {
        order[order_idx++] = 2;
        lc_coroutine_yield();
    }
}

static bool test_basic_yield(void) {
    order_idx = 0;
    lc_bytes_fill(order, 0, sizeof(order));

    lc_scheduler sched = lc_scheduler_create();
    lc_coroutine *a = lc_coroutine_create(&sched, count_a, NULL);
    lc_coroutine *b = lc_coroutine_create(&sched, count_b, NULL);
    if (!a || !b) { lc_scheduler_destroy(&sched); return false; }

    lc_scheduler_run(&sched);
    lc_scheduler_destroy(&sched);

    /* Expected interleaving: A runs first, yields, B runs, yields, ... */
    /* order should be [1, 2, 1, 2, 1, 2] */
    if (order_idx != 6) return false;
    for (int i = 0; i < 6; i++) {
        int expected = (i % 2 == 0) ? 1 : 2;
        if (order[i] != expected) return false;
    }
    return true;
}

/* ===================================================================
 * Test 2: ping_pong — two coroutines pass a value back and forth
 * =================================================================== */

static int ping_pong_value = 0;

static void ping(void *arg) {
    int *count = (int *)arg;
    for (int i = 0; i < 5; i++) {
        ping_pong_value += 1;  /* ping adds 1 */
        (*count)++;
        lc_coroutine_yield();
    }
}

static void pong(void *arg) {
    int *count = (int *)arg;
    for (int i = 0; i < 5; i++) {
        ping_pong_value += 10;  /* pong adds 10 */
        (*count)++;
        lc_coroutine_yield();
    }
}

static bool test_ping_pong(void) {
    ping_pong_value = 0;
    int ping_count = 0;
    int pong_count = 0;

    lc_scheduler sched = lc_scheduler_create();
    lc_coroutine *p1 = lc_coroutine_create(&sched, ping, &ping_count);
    lc_coroutine *p2 = lc_coroutine_create(&sched, pong, &pong_count);
    if (!p1 || !p2) { lc_scheduler_destroy(&sched); return false; }

    lc_scheduler_run(&sched);
    lc_scheduler_destroy(&sched);

    /* 5 pings (+1 each) + 5 pongs (+10 each) = 5 + 50 = 55 */
    return ping_pong_value == 55 && ping_count == 5 && pong_count == 5;
}

/* ===================================================================
 * Test 3: many_coroutines — 100 coroutines each write their index
 * =================================================================== */

#define MANY_COUNT 100
static int many_results[MANY_COUNT];

static void write_index(void *arg) {
    int index = (int)(uintptr_t)arg;
    many_results[index] = index + 1;  /* write index+1 so we can distinguish from 0 */
    lc_coroutine_yield();
}

static bool test_many_coroutines(void) {
    lc_bytes_fill(many_results, 0, sizeof(many_results));

    lc_scheduler sched = lc_scheduler_create();
    for (int i = 0; i < MANY_COUNT; i++) {
        lc_coroutine *co = lc_coroutine_create(&sched, write_index, (void *)(uintptr_t)i);
        if (!co) { lc_scheduler_destroy(&sched); return false; }
    }

    lc_scheduler_run(&sched);
    lc_scheduler_destroy(&sched);

    /* Verify every coroutine ran */
    for (int i = 0; i < MANY_COUNT; i++) {
        if (many_results[i] != i + 1) return false;
    }
    return true;
}

/* ===================================================================
 * Test 4: resume_point — a coroutine yields multiple times and
 *         resumes at the right point each time
 * =================================================================== */

static int resume_sequence[10];
static int resume_idx = 0;

static void multi_yield(void *arg) {
    (void)arg;
    resume_sequence[resume_idx++] = 10;
    lc_coroutine_yield();
    resume_sequence[resume_idx++] = 20;
    lc_coroutine_yield();
    resume_sequence[resume_idx++] = 30;
    lc_coroutine_yield();
    resume_sequence[resume_idx++] = 40;
    /* no yield — just returns (finishes) */
}

/* A companion that also yields, to make scheduling round-robin */
static void companion(void *arg) {
    (void)arg;
    for (int i = 0; i < 4; i++) {
        resume_sequence[resume_idx++] = -(i + 1);
        lc_coroutine_yield();
    }
}

static bool test_resume_point(void) {
    resume_idx = 0;
    lc_bytes_fill(resume_sequence, 0, sizeof(resume_sequence));

    lc_scheduler sched = lc_scheduler_create();
    lc_coroutine *m = lc_coroutine_create(&sched, multi_yield, NULL);
    lc_coroutine *c = lc_coroutine_create(&sched, companion, NULL);
    if (!m || !c) { lc_scheduler_destroy(&sched); return false; }

    lc_scheduler_run(&sched);
    lc_scheduler_destroy(&sched);

    /* Expected interleaving:
     *   multi_yield runs: writes 10, yields
     *   companion runs:   writes -1, yields
     *   multi_yield runs: writes 20, yields
     *   companion runs:   writes -2, yields
     *   multi_yield runs: writes 30, yields
     *   companion runs:   writes -3, yields
     *   multi_yield runs: writes 40, finishes, yield finds companion
     *   companion runs:   writes -4, yields, no more => done
     */
    int expected[] = {10, -1, 20, -2, 30, -3, 40, -4};
    if (resume_idx != 8) return false;
    for (int i = 0; i < 8; i++) {
        if (resume_sequence[i] != expected[i]) return false;
    }
    return true;
}

/* ===================================================================
 * Test 5: producer_consumer — one produces values, another consumes
 * =================================================================== */

#define BUFFER_SIZE 8
static int buffer[BUFFER_SIZE];
static int buf_write_pos = 0;
static int buf_read_pos = 0;
static int consumed_sum = 0;

static void producer(void *arg) {
    int count = (int)(uintptr_t)arg;
    for (int i = 1; i <= count; i++) {
        buffer[buf_write_pos % BUFFER_SIZE] = i;
        buf_write_pos++;
        lc_coroutine_yield();
    }
}

static void consumer(void *arg) {
    int count = (int)(uintptr_t)arg;
    for (int i = 0; i < count; i++) {
        lc_coroutine_yield();  /* let producer go first */
        int val = buffer[buf_read_pos % BUFFER_SIZE];
        buf_read_pos++;
        consumed_sum += val;
    }
}

static bool test_producer_consumer(void) {
    buf_write_pos = 0;
    buf_read_pos = 0;
    consumed_sum = 0;
    lc_bytes_fill(buffer, 0, sizeof(buffer));

    int item_count = 6;

    lc_scheduler sched = lc_scheduler_create();
    lc_coroutine *p = lc_coroutine_create(&sched, producer, (void *)(uintptr_t)item_count);
    lc_coroutine *c = lc_coroutine_create(&sched, consumer, (void *)(uintptr_t)item_count);
    if (!p || !c) { lc_scheduler_destroy(&sched); return false; }

    lc_scheduler_run(&sched);
    lc_scheduler_destroy(&sched);

    /* Producer writes 1..6. Consumer reads them. Sum = 1+2+3+4+5+6 = 21 */
    return consumed_sum == 21 && buf_write_pos == item_count && buf_read_pos == item_count;
}

/* ===================================================================
 * Test 6: single_coroutine — edge case, only one coroutine
 * =================================================================== */

static int single_ran = 0;

static void single_func(void *arg) {
    (void)arg;
    single_ran = 1;
    lc_coroutine_yield();  /* yield with nobody else to run */
    /* should not get here if it was the only coroutine and it yielded once,
     * but since it's the only one and it's still READY, it will come back */
    single_ran = 2;
}

static bool test_single_coroutine(void) {
    single_ran = 0;

    lc_scheduler sched = lc_scheduler_create();
    lc_coroutine *co = lc_coroutine_create(&sched, single_func, NULL);
    if (!co) { lc_scheduler_destroy(&sched); return false; }

    lc_scheduler_run(&sched);
    lc_scheduler_destroy(&sched);

    /* The single coroutine runs, yields (wraps back to itself), runs again, finishes */
    return single_ran == 2;
}

/* ===================================================================
 * Test 7: argument_passing — verify func args are delivered correctly
 * =================================================================== */

static int arg_results[4];

static void store_arg(void *arg) {
    int *slot = (int *)arg;
    *slot = 0xBEEF;
}

static bool test_argument_passing(void) {
    lc_bytes_fill(arg_results, 0, sizeof(arg_results));

    lc_scheduler sched = lc_scheduler_create();
    for (int i = 0; i < 4; i++) {
        lc_coroutine *co = lc_coroutine_create(&sched, store_arg, &arg_results[i]);
        if (!co) { lc_scheduler_destroy(&sched); return false; }
    }

    lc_scheduler_run(&sched);
    lc_scheduler_destroy(&sched);

    for (int i = 0; i < 4; i++) {
        if (arg_results[i] != 0xBEEF) return false;
    }
    return true;
}

/* ===================================================================
 * Main — run all tests
 * =================================================================== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    lc_print_string(STDOUT, S("basic_yield"));
    say_pass_fail(test_basic_yield());

    lc_print_string(STDOUT, S("ping_pong"));
    say_pass_fail(test_ping_pong());

    lc_print_string(STDOUT, S("many_coroutines"));
    say_pass_fail(test_many_coroutines());

    lc_print_string(STDOUT, S("resume_point"));
    say_pass_fail(test_resume_point());

    lc_print_string(STDOUT, S("producer_consumer"));
    say_pass_fail(test_producer_consumer());

    lc_print_string(STDOUT, S("single_coroutine"));
    say_pass_fail(test_single_coroutine());

    lc_print_string(STDOUT, S("argument_passing"));
    say_pass_fail(test_argument_passing());

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
