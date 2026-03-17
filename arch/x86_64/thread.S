/*
 * lightc — thread start trampoline for x86_64
 *
 * Called from C:
 *   int64_t lc_thread_start(uint64_t flags, uint8_t *stack_top,
 *                           int32_t *parent_tid, int32_t *child_tid,
 *                           void *tls,
 *                           lc_thread_func func, void *arg);
 *
 * C calling convention (7 args):
 *   rdi = flags
 *   rsi = stack_top
 *   rdx = parent_tid
 *   rcx = child_tid
 *   r8  = tls
 *   r9  = func
 *   8(%rsp) = arg    (7th arg is on the stack)
 */

.section .text
.global lc_thread_start
.type lc_thread_start, @function

lc_thread_start:
    /* Grab 7th arg (arg) from the stack before we modify anything */
    movq    8(%rsp), %rax       /* arg is at [rsp + 8] (after return addr) */

    /* Store func and arg on the child's stack */
    andq    $-16, %rsi          /* 16-byte align */
    movq    %rax, -8(%rsi)      /* arg  at [stack_top - 8]  */
    movq    %r9, -16(%rsi)      /* func at [stack_top - 16] */
    subq    $16, %rsi           /* child_stack = stack_top - 16 */

    /* clone syscall:
     *   rax = SYS_clone (56)
     *   rdi = flags       (already set)
     *   rsi = child_stack (already set)
     *   rdx = parent_tid  (already set)
     *   r10 = child_tid   (from rcx)
     *   r8  = tls         (already set — for CLONE_SETTLS) */
    movq    %rcx, %r10
    movl    $56, %eax
    syscall

    testq   %rax, %rax
    jz      .Lchild
    ret

.Lchild:
    xorl    %ebp, %ebp
    popq    %rax                /* func */
    popq    %rdi                /* arg  */
    call    *%rax

    /* Exit this thread (SYS_exit, not SYS_exit_group) */
    movl    %eax, %edi
    movl    $60, %eax
    syscall

.size lc_thread_start, . - lc_thread_start
