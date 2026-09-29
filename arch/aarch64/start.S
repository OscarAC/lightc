/*
 * lightc — _start for aarch64
 *
 * Same stack layout as x86_64:
 *   sp -> argc, argv[0], ..., argv[argc]=NULL, envp[0], ..., envp[n]=NULL, auxv[]
 */

.section .text
.global _start
.type _start, %function

_start:
    /* Clear frame pointer */
    mov     x29, #0
    mov     x30, #0

    /* Capture the kernel's real page size (AT_PAGESZ) from the auxv while the
     * initial stack is still intact. sp points at argc; the call preserves sp,
     * so the argument setup below is unaffected. */
    mov     x0, sp
    bl      lc_runtime_init

    /* argc = *sp */
    ldr     x0, [sp]

    /* argv = sp + 8 */
    add     x1, sp, #8

    /* envp = argv + (argc + 1) * 8 */
    add     x2, x0, #1
    lsl     x2, x2, #3
    add     x2, x1, x2

    bl      main

    /* exit(retval) — retval is in w0 */
    mov     x8, #93        /* SYS_exit */
    svc     #0

.size _start, . - _start
