/*
 * lightc — coroutine context switch for x86_64
 *
 * void lc_coroutine_switch(lc_coroutine_context *save, lc_coroutine_context *restore);
 *
 * Saves callee-saved registers (System V AMD64 ABI) into *save,
 * restores them from *restore, and returns to the restored context.
 *
 * Callee-saved registers:
 *   rbx, rbp, r12, r13, r14, r15, rsp
 *
 * Arguments (C calling convention):
 *   rdi = pointer to save context
 *   rsi = pointer to restore context
 *
 * Layout of lc_coroutine_context (7 x uint64_t = 56 bytes):
 *   offset  0: rbx
 *   offset  8: rbp
 *   offset 16: r12
 *   offset 24: r13
 *   offset 32: r14
 *   offset 40: r15
 *   offset 48: rsp
 */

.section .text
.global lc_coroutine_switch
.type lc_coroutine_switch, @function

lc_coroutine_switch:
    /* Save callee-saved registers into save context */
    movq    %rbx,  0(%rdi)
    movq    %rbp,  8(%rdi)
    movq    %r12, 16(%rdi)
    movq    %r13, 24(%rdi)
    movq    %r14, 32(%rdi)
    movq    %r15, 40(%rdi)
    movq    %rsp, 48(%rdi)

    /* Restore callee-saved registers from restore context */
    movq     0(%rsi), %rbx
    movq     8(%rsi), %rbp
    movq    16(%rsi), %r12
    movq    24(%rsi), %r13
    movq    32(%rsi), %r14
    movq    40(%rsi), %r15
    movq    48(%rsi), %rsp

    /* Return to wherever the restored rsp's return address points */
    ret

.size lc_coroutine_switch, . - lc_coroutine_switch
