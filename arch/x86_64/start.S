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

    /* Point the main thread's FS at the heap TLS null sentinel while the
     * process is still single-threaded, so the first %fs:0 read never faults
     * on FS base 0. Done here (not lazily in the allocator) to avoid a race
     * with a child thread's first allocation. Clobbers caller-saved regs only;
     * %rsp is preserved, so argc/argv/envp are read correctly afterwards. */
    call    lc_heap_bootstrap_main_tls

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
