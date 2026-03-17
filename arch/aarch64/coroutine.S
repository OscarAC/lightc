/*
 * lightc — coroutine context switch for aarch64
 *
 * void lc_coroutine_switch(lc_coroutine_context *save, lc_coroutine_context *restore);
 *
 * Saves callee-saved registers (AAPCS64) into *save,
 * restores them from *restore, and returns to the restored context.
 *
 * Callee-saved registers:
 *   x19-x28, x29 (fp), x30 (lr), sp
 *
 * Arguments:
 *   x0 = pointer to save context
 *   x1 = pointer to restore context
 *
 * Layout of lc_coroutine_context (13 x uint64_t = 104 bytes):
 *   offset  0: x19, x20
 *   offset 16: x21, x22
 *   offset 32: x23, x24
 *   offset 48: x25, x26
 *   offset 64: x27, x28
 *   offset 80: x29 (fp), x30 (lr)
 *   offset 96: sp
 */

.section .text
.global lc_coroutine_switch
.type lc_coroutine_switch, %function

lc_coroutine_switch:
    /* Save callee-saved registers into save context */
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]
    mov     x2, sp
    str     x2, [x0, #96]

    /* Restore callee-saved registers from restore context */
    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]
    ldr     x2, [x1, #96]
    mov     sp, x2

    /* Return to lr (x30) of the restored context */
    ret

.size lc_coroutine_switch, . - lc_coroutine_switch
