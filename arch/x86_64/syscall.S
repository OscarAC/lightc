/*
 * lightc — syscall wrappers for x86_64
 *
 * Linux x86_64 syscall convention:
 *   syscall number: %rax
 *   arguments:      %rdi, %rsi, %rdx, %r10, %r8, %r9
 *   return value:   %rax
 *   clobbered:      %rcx, %r11
 *
 * C calling convention (System V AMD64 ABI):
 *   arguments:      %rdi, %rsi, %rdx, %rcx, %r8, %r9
 *
 * Note: arg4 moves from %rcx (C ABI) to %r10 (syscall ABI).
 */

.section .text

.global lc_syscall0
.type lc_syscall0, @function
lc_syscall0:
    movq    %rdi, %rax
    syscall
    ret
.size lc_syscall0, . - lc_syscall0

.global lc_syscall1
.type lc_syscall1, @function
lc_syscall1:
    movq    %rdi, %rax
    movq    %rsi, %rdi
    syscall
    ret
.size lc_syscall1, . - lc_syscall1

.global lc_syscall2
.type lc_syscall2, @function
lc_syscall2:
    movq    %rdi, %rax
    movq    %rsi, %rdi
    movq    %rdx, %rsi
    syscall
    ret
.size lc_syscall2, . - lc_syscall2

.global lc_syscall3
.type lc_syscall3, @function
lc_syscall3:
    movq    %rdi, %rax
    movq    %rsi, %rdi
    movq    %rdx, %rsi
    movq    %rcx, %rdx
    syscall
    ret
.size lc_syscall3, . - lc_syscall3

.global lc_syscall4
.type lc_syscall4, @function
lc_syscall4:
    movq    %rdi, %rax
    movq    %rsi, %rdi
    movq    %rdx, %rsi
    movq    %rcx, %rdx
    movq    %r8,  %r10
    syscall
    ret
.size lc_syscall4, . - lc_syscall4

.global lc_syscall5
.type lc_syscall5, @function
lc_syscall5:
    movq    %rdi, %rax
    movq    %rsi, %rdi
    movq    %rdx, %rsi
    movq    %rcx, %rdx
    movq    %r8,  %r10
    movq    %r9,  %r8
    syscall
    ret
.size lc_syscall5, . - lc_syscall5

.global lc_syscall6
.type lc_syscall6, @function
lc_syscall6:
    movq    %rdi, %rax
    movq    %rsi, %rdi
    movq    %rdx, %rsi
    movq    %rcx, %rdx
    movq    %r8,  %r10
    movq    %r9,  %r8
    movq    8(%rsp), %r9
    syscall
    ret
.size lc_syscall6, . - lc_syscall6
