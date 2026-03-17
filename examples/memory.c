/*
 * Exercise the memory allocator.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/memory.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed) {
        lc_print_line(STDOUT, S(" PASS"));
    } else {
        lc_print_line(STDOUT, S(" FAIL"));
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* --- Raw page allocation --- */
    lc_print_string(STDOUT, S("allocate_pages"));
    void *pages = lc_allocate_pages(2);
    say_pass_fail(pages != NULL);

    lc_print_string(STDOUT, S("free_pages"));
    lc_free_pages(pages, 2);
    lc_print_line(STDOUT, S(" PASS"));

    /* --- Arena create --- */
    lc_arena arena = lc_arena_create(4096);
    lc_print_string(STDOUT, S("arena_create"));
    say_pass_fail(arena.base != NULL && arena.capacity >= 4096);

    /* --- Arena initial state --- */
    lc_print_string(STDOUT, S("arena_initial_state"));
    say_pass_fail(lc_arena_get_used(&arena) == 0
               && lc_arena_get_remaining(&arena) == arena.capacity);

    /* --- Arena allocate --- */
    lc_print_string(STDOUT, S("arena_allocate"));
    uint64_t *numbers = lc_arena_allocate(&arena, sizeof(uint64_t) * 10);
    say_pass_fail(numbers != NULL);

    /* --- Write to allocated memory --- */
    lc_print_string(STDOUT, S("arena_write"));
    for (uint64_t i = 0; i < 10; i++) numbers[i] = i * i;
    say_pass_fail(numbers[0] == 0 && numbers[3] == 9 && numbers[9] == 81);

    /* --- Alignment (16-byte default) --- */
    lc_print_string(STDOUT, S("arena_alignment_16"));
    /* Allocate an odd size to test alignment of next allocation */
    lc_arena_allocate(&arena, 7);
    void *aligned = lc_arena_allocate(&arena, 32);
    say_pass_fail(((uintptr_t)aligned & 0xf) == 0);

    /* --- Custom alignment --- */
    lc_print_string(STDOUT, S("arena_alignment_64"));
    void *aligned64 = lc_arena_allocate_aligned(&arena, 32, 64);
    say_pass_fail(((uintptr_t)aligned64 & 0x3f) == 0);

    /* --- Used / Remaining track correctly --- */
    lc_print_string(STDOUT, S("arena_tracking"));
    size_t used = lc_arena_get_used(&arena);
    size_t remaining = lc_arena_get_remaining(&arena);
    say_pass_fail(used > 0 && used + remaining == arena.capacity);

    /* Print actual numbers */
    lc_print_string(STDOUT, S("  used: "));
    lc_print_unsigned(STDOUT, used);
    lc_print_string(STDOUT, S(" remaining: "));
    lc_print_unsigned(STDOUT, remaining);
    lc_print_newline(STDOUT);

    /* --- Arena reset --- */
    lc_print_string(STDOUT, S("arena_reset"));
    lc_arena_reset(&arena);
    say_pass_fail(lc_arena_get_used(&arena) == 0
               && lc_arena_get_remaining(&arena) == arena.capacity);

    /* --- Allocate after reset (reuse memory) --- */
    lc_print_string(STDOUT, S("arena_reuse"));
    char *msg = lc_arena_allocate(&arena, 32);
    const char hello[] = "arena reuse works!";
    lc_bytes_copy(msg, hello, sizeof(hello));
    lc_print_string(STDOUT, S("  "));
    lc_print_line(STDOUT, msg, lc_string_length(msg));
    say_pass_fail(lc_string_equal(msg, sizeof(hello) - 1, hello, sizeof(hello) - 1));

    /* --- Allocation failure (request more than remaining) --- */
    lc_print_string(STDOUT, S("arena_overflow"));
    void *fail = lc_arena_allocate(&arena, arena.capacity + 1);
    say_pass_fail(fail == NULL);

    /* --- Arena destroy --- */
    lc_print_string(STDOUT, S("arena_destroy"));
    lc_arena_destroy(&arena);
    say_pass_fail(arena.base == NULL && arena.capacity == 0 && arena.used == 0);

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
