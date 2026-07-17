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
     *   x0-x2 already set (flags, stack, parent_tid).
     *
     * aarch64 selects CONFIG_CLONE_BACKWARDS, so the kernel signature is
     *   clone(flags, newsp, parent_tidptr, tls, child_tidptr)
     * i.e. x3 = tls, x4 = child_tid. The C ABI delivers child_tid in x3 and
     * tls in x4, so swap them before the syscall (x9 is a caller-saved temp). */
    mov     x9, x3          /* x9 = child_tid */
    mov     x3, x4          /* x3 = tls       */
    mov     x4, x9          /* x4 = child_tid */
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
