/*
 * lightc — process clone trampoline for aarch64
 *
 * Safely creates a new process via the clone syscall, handling the
 * stack-switch pitfall: after clone, the child's sp points to the
 * new stack, so parent stack locals are inaccessible.  This trampoline
 * places fn and arg on the child's stack before the syscall, so the
 * child loads them and calls fn(arg) without touching the parent frame.
 *
 * Called from C:
 *   lc_sysret lc_kernel_clone(int64_t flags, void *stack_top,
 *                              int (*fn)(void *), void *arg);
 *
 * AAPCS64 calling convention (4 args, all in registers):
 *   x0 = flags
 *   x1 = stack_top
 *   x2 = fn
 *   x3 = arg
 *
 * Returns:
 *   parent: child PID (positive) or -errno (negative)
 *   child:  never returns — calls fn(arg), then _exit_group
 */

.section .text
.global lc_kernel_clone
.type lc_kernel_clone, %function

lc_kernel_clone:
    /* Store fn and arg on the child's stack */
    and     x1, x1, #~15           /* 16-byte align */
    stp     x2, x3, [x1, #-16]!    /* fn at [sp], arg at [sp+8]; sp -= 16 */

    /* clone syscall:
     *   x8 = SYS_clone (220)
     *   x0 = flags       (already set)
     *   x1 = child_stack (already set)
     *   x2 = parent_tid  → 0
     *   x3 = child_tid   → 0
     *   x4 = tls         → 0 */
    mov     x2, #0
    mov     x3, #0
    mov     x4, #0
    mov     x8, #220
    svc     #0

    cbz     x0, .Lclone_child
    ret                              /* parent: return child PID or -errno */

.Lclone_child:
    mov     x29, #0                 /* clear frame pointer */
    mov     x30, #0                 /* clear link register */
    ldp     x1, x0, [sp], #16      /* x1=fn, x0=arg */
    blr     x1                      /* fn(arg) */

    /* _exit_group(retval) */
    mov     x8, #94
    svc     #0

.size lc_kernel_clone, . - lc_kernel_clone
