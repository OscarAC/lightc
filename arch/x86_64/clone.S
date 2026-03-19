/*
 * lightc — process clone trampoline for x86_64
 *
 * Safely creates a new process via the clone syscall, handling the
 * stack-switch pitfall: after clone, the child's %rsp points to the
 * new stack, so parent stack locals are inaccessible.  This trampoline
 * places fn and arg on the child's stack before the syscall, so the
 * child pops them and calls fn(arg) without touching the parent frame.
 *
 * Called from C:
 *   lc_sysret lc_kernel_clone(int64_t flags, void *stack_top,
 *                              int (*fn)(void *), void *arg);
 *
 * C calling convention (4 args):
 *   rdi = flags
 *   rsi = stack_top
 *   rdx = fn
 *   rcx = arg
 *
 * Returns:
 *   parent: child PID (positive) or -errno (negative)
 *   child:  never returns — calls fn(arg), then _exit_group
 */

.section .text
.global lc_kernel_clone
.type lc_kernel_clone, @function

lc_kernel_clone:
    /* Store fn and arg on the child's stack */
    andq    $-16, %rsi          /* 16-byte align */
    movq    %rcx, -8(%rsi)      /* arg  at [stack_top - 8]  */
    movq    %rdx, -16(%rsi)     /* fn   at [stack_top - 16] */
    subq    $16, %rsi           /* child_stack = stack_top - 16 */

    /* clone syscall:
     *   rax = SYS_clone (56)
     *   rdi = flags       (already set)
     *   rsi = child_stack (already set)
     *   rdx = parent_tid  → 0
     *   r10 = child_tid   → 0
     *   r8  = tls         → 0 */
    xorl    %edx, %edx
    xorl    %r10d, %r10d
    xorl    %r8d, %r8d
    movl    $56, %eax
    syscall

    testq   %rax, %rax
    jz      .Lclone_child
    ret                          /* parent: return child PID or -errno */

.Lclone_child:
    xorl    %ebp, %ebp          /* clear frame pointer */
    popq    %rax                /* fn */
    popq    %rdi                /* arg */
    call    *%rax               /* fn(arg) */

    /* _exit_group(retval) */
    movl    %eax, %edi
    movl    $231, %eax
    syscall

.size lc_kernel_clone, . - lc_kernel_clone
