/*
 * lightc — thread start trampoline for aarch64
 *
 * Called from C:
 *   int64_t lc_thread_start(uint64_t flags, uint8_t *stack_top,
 *                           int32_t *parent_tid, int32_t *child_tid,
 *                           void *tls,
 *                           lc_thread_func func, void *arg);
 *
 * AAPCS64 calling convention (7 args, all in registers):
 *   x0 = flags
 *   x1 = stack_top
 *   x2 = parent_tid
 *   x3 = child_tid
 *   x4 = tls
 *   x5 = func
 *   x6 = arg
 */

.section .text
.global lc_thread_start
.type lc_thread_start, %function

lc_thread_start:
    /* Store func and arg on the child's stack */
    and     x1, x1, #~15
    stp     x5, x6, [x1, #-16]!

    /* clone syscall:
     *   x8 = SYS_clone (220)
     *   x0-x4 already set (flags, stack, parent_tid, child_tid, tls) */
    mov     x8, #220
    svc     #0

    cbz     x0, .Lchild
    ret

.Lchild:
    mov     x29, #0
    mov     x30, #0
    ldp     x1, x0, [sp], #16
    blr     x1

    mov     x8, #93
    svc     #0

.size lc_thread_start, . - lc_thread_start
