#include <lightc/coroutine.h>
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/heap.h>
#include <lightc/thread.h>

/*
 * Verify that the assembly context-switch offsets match the C struct layout.
 * If any of these fire, the assembly and C disagree on struct layout.
 */
#if defined(__x86_64__)
_Static_assert(sizeof(lc_coroutine_context) == 56,
               "x86_64 lc_coroutine_context must be 7 * 8 = 56 bytes");
#elif defined(__aarch64__)
_Static_assert(sizeof(lc_coroutine_context) == 104,
               "aarch64 lc_coroutine_context must be 13 * 8 = 104 bytes");
#endif

/* Implemented in arch-specific assembly (arch/{x86_64,aarch64}/coroutine.S) */
extern void lc_coroutine_switch(lc_coroutine_context *save,
                                lc_coroutine_context *restore);

/*
 * Per-thread pointer to the currently active scheduler.
 * Uses thread-local storage so each thread can run its own scheduler.
 */
static lc_tls_key scheduler_tls_key;
static lc_once scheduler_tls_init = LC_ONCE_INIT;

static void init_scheduler_tls(void) {
    lc_tls_key_create(&scheduler_tls_key);
}

static lc_scheduler *get_current_scheduler(void) {
    lc_call_once(&scheduler_tls_init, init_scheduler_tls);
    return (lc_scheduler *)lc_tls_get(scheduler_tls_key);
}

static void set_current_scheduler(lc_scheduler *sched) {
    lc_call_once(&scheduler_tls_init, init_scheduler_tls);
    lc_tls_set(scheduler_tls_key, sched);
}

/*
 * Trampoline — entered when a coroutine is first switched to.
 *
 * The context switch "returns" into this function (via ret/lr).
 * It calls the user's function, marks the coroutine finished,
 * and yields one last time to return control to the scheduler.
 */
static void coroutine_trampoline(void) {
    lc_scheduler *sched = get_current_scheduler();
    lc_coroutine *co = &sched->coroutines[sched->current];

    /* Run the user's coroutine function */
    co->func(co->arg);

    /* Mark as finished */
    co->state = LC_COROUTINE_FINISHED;
    sched->active_count--;

    /* Yield back — scheduler will find the next coroutine or return to main */
    lc_coroutine_yield();

    /* Should never reach here — the coroutine is finished and will never
     * be switched back to. */
    __builtin_unreachable();
}

/* --- Scheduler API --- */

lc_scheduler lc_scheduler_create_with_capacity(uint32_t max_coroutines) {
    lc_scheduler sched;
    lc_bytes_fill(&sched, 0, sizeof(sched));
    sched.capacity = max_coroutines;
    if (max_coroutines > 0) {
        sched.coroutines = lc_heap_allocate_zeroed(max_coroutines * sizeof(lc_coroutine)).value;
    }
    return sched;
}

lc_scheduler lc_scheduler_create(void) {
    return lc_scheduler_create_with_capacity(LC_MAX_COROUTINES);
}

lc_coroutine *lc_coroutine_create(lc_scheduler *sched, lc_coroutine_func func, void *arg) {
    if (sched->count >= sched->capacity) return NULL;

    lc_coroutine *co = &sched->coroutines[sched->count];
    sched->count++;

    /* Allocate a stack via mmap, with a guard page at the bottom */
    size_t guard_size = 4096;
    size_t total_size = LC_COROUTINE_STACK_SIZE + guard_size;
    void *stack = lc_kernel_map_memory(NULL, total_size,
                                       PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS,
                                       -1, 0);
    if (stack == MAP_FAILED) {
        sched->count--;
        return NULL;
    }

    /* Guard page at the bottom (lowest address) — stack overflow hits this */
    lc_kernel_protect_memory(stack, guard_size, PROT_NONE);

    co->func       = func;
    co->arg        = arg;
    co->stack_base = stack;
    co->stack_size = total_size;
    co->scheduler  = sched;
    co->state      = LC_COROUTINE_READY;

    /* Zero the context — only the fields we set matter */
    lc_bytes_fill(&co->context, 0, sizeof(co->context));

    /*
     * Set up the initial context so that lc_coroutine_switch "returns"
     * into coroutine_trampoline.
     */
#if defined(__x86_64__)
    /*
     * Stack grows down. The top of the mmap'd region is the initial stack top.
     *
     * We place the trampoline address as a "return address" on the stack.
     * When lc_coroutine_switch executes `ret`, it pops this address and
     * jumps to the trampoline.
     *
     * After ret: rsp = stack_top (16-byte aligned from mmap).
     * The trampoline then calls func(arg). At that point rsp is 16-byte
     * aligned, and `call` pushes 8 bytes, so the callee sees rsp 8-mod-16.
     * This matches the System V ABI requirement.
     */
    uint64_t *stack_top = (uint64_t *)((uint8_t *)stack + total_size);

    /* Push the trampoline address as the return address */
    stack_top--;
    *stack_top = (uint64_t)coroutine_trampoline;

    co->context.rsp = (uint64_t)stack_top;
    co->context.rbp = 0;

#elif defined(__aarch64__)
    /*
     * Stack grows down. Align to 16 bytes (AArch64 requires it).
     *
     * The context switch restores lr (x30) and then executes `ret`,
     * which jumps to lr. We set lr = trampoline.
     * sp = top of stack, 16-byte aligned.
     */
    uint64_t *stack_top = (uint64_t *)((uint8_t *)stack + total_size);
    stack_top = (uint64_t *)((uintptr_t)stack_top & ~15ULL);

    co->context.sp  = (uint64_t)stack_top;
    co->context.x30 = (uint64_t)coroutine_trampoline;  /* lr = return address */
    co->context.x29 = 0;                                /* fp = 0 (frame chain end) */
#endif

    return co;
}

void lc_scheduler_run(lc_scheduler *sched) {
    if (sched->count == 0) return;

    set_current_scheduler(sched);
    sched->active_count = sched->count;

    /* Reset all coroutines to ready */
    for (uint32_t i = 0; i < sched->count; i++) {
        sched->coroutines[i].state = LC_COROUTINE_READY;
    }

    /* Start the first coroutine */
    sched->current = 0;
    sched->coroutines[0].state = LC_COROUTINE_RUNNING;
    lc_coroutine_switch(&sched->main_context, &sched->coroutines[0].context);

    /*
     * We return here when all coroutines have finished.
     * (lc_coroutine_yield switches back to main_context when active_count == 0)
     */
}

void lc_coroutine_yield(void) {
    lc_scheduler *sched = get_current_scheduler();
    uint32_t current_idx = sched->current;
    lc_coroutine *current_co = &sched->coroutines[current_idx];

    /* Mark current as ready (unless it just finished) */
    if (current_co->state == LC_COROUTINE_RUNNING) {
        current_co->state = LC_COROUTINE_READY;
    }

    /* Find next non-finished coroutine (round-robin) */
    uint32_t next_idx = current_idx;
    bool found = false;
    for (uint32_t i = 1; i <= sched->count; i++) {
        uint32_t candidate = (current_idx + i) % sched->count;
        if (sched->coroutines[candidate].state != LC_COROUTINE_FINISHED) {
            next_idx = candidate;
            found = true;
            break;
        }
    }

    if (!found) {
        /* No more coroutines to run — return to lc_scheduler_run */
        lc_coroutine_switch(&current_co->context, &sched->main_context);
        return;
    }

    /* Switch to the next coroutine */
    sched->current = next_idx;
    sched->coroutines[next_idx].state = LC_COROUTINE_RUNNING;
    lc_coroutine_switch(&current_co->context, &sched->coroutines[next_idx].context);
}

void lc_scheduler_destroy(lc_scheduler *sched) {
    for (uint32_t i = 0; i < sched->count; i++) {
        if (sched->coroutines[i].stack_base != NULL) {
            lc_kernel_unmap_memory(sched->coroutines[i].stack_base,
                                   sched->coroutines[i].stack_size);
            sched->coroutines[i].stack_base = NULL;
        }
    }
    lc_heap_free(sched->coroutines);
    sched->coroutines = NULL;
    sched->count = 0;
    sched->active_count = 0;
    sched->capacity = 0;
}
