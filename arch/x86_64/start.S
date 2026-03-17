/*
 * lightc — _start for x86_64
 *
 * When the kernel loads an ELF, the stack looks like:
 *
 *   [top of stack]
 *   argc            <- %rsp
 *   argv[0]
 *   argv[1]
 *   ...
 *   argv[argc]      (NULL)
 *   envp[0]
 *   envp[1]
 *   ...
 *   envp[n]         (NULL)
 *   auxv[]          (ELF auxiliary vector)
 *
 * We extract argc, argv, envp and call main(argc, argv, envp).
 * Then we call exit(retval).
 */

.section .text
.global _start
.type _start, @function

_start:
    /* Clear frame pointer for clean backtraces */
    xorl    %ebp, %ebp

    /* argc = *rsp */
    movq    (%rsp), %rdi

    /* argv = rsp + 8 */
    leaq    8(%rsp), %rsi

    /* envp = argv + (argc + 1) * 8 */
    leaq    8(%rsi, %rdi, 8), %rdx

    /* Align stack to 16 bytes (ABI requirement) */
    andq    $-16, %rsp

    call    main

    /* exit(retval) — retval is in %eax */
    movl    %eax, %edi
    movl    $60, %eax       /* SYS_exit */
    syscall

.size _start, . - _start
