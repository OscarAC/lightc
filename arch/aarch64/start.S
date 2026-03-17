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
    /* argc = *sp */
    ldr     x0, [sp]

    /* argv = sp + 8 */
    add     x1, sp, #8

    /* envp = argv + (argc + 1) * 8 */
    add     x2, x0, #1
    lsl     x2, x2, #3
    add     x2, x1, x2

    /* Clear frame pointer */
    mov     x29, #0
    mov     x30, #0

    bl      main

    /* exit(retval) — retval is in w0 */
    mov     x8, #93        /* SYS_exit */
    svc     #0

.size _start, . - _start
