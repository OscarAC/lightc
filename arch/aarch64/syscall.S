/*
 * lightc — syscall wrappers for aarch64
 *
 * Linux aarch64 syscall convention:
 *   syscall number: x8
 *   arguments:      x0, x1, x2, x3, x4, x5
 *   return value:   x0
 *
 * C calling convention (AAPCS64):
 *   arguments:      x0, x1, x2, x3, x4, x5, x6, x7
 *
 * Lucky: args x0-x5 are the same for both conventions.
 * We just need to move the syscall number from x0 to x8
 * and shift the arguments down.
 */

.section .text

.global lc_syscall0
.type lc_syscall0, %function
lc_syscall0:
    mov     x8, x0
    svc     #0
    ret
.size lc_syscall0, . - lc_syscall0

.global lc_syscall1
.type lc_syscall1, %function
lc_syscall1:
    mov     x8, x0
    mov     x0, x1
    svc     #0
    ret
.size lc_syscall1, . - lc_syscall1

.global lc_syscall2
.type lc_syscall2, %function
lc_syscall2:
    mov     x8, x0
    mov     x0, x1
    mov     x1, x2
    svc     #0
    ret
.size lc_syscall2, . - lc_syscall2

.global lc_syscall3
.type lc_syscall3, %function
lc_syscall3:
    mov     x8, x0
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    svc     #0
    ret
.size lc_syscall3, . - lc_syscall3

.global lc_syscall4
.type lc_syscall4, %function
lc_syscall4:
    mov     x8, x0
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    mov     x3, x4
    svc     #0
    ret
.size lc_syscall4, . - lc_syscall4

.global lc_syscall5
.type lc_syscall5, %function
lc_syscall5:
    mov     x8, x0
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    mov     x3, x4
    mov     x4, x5
    svc     #0
    ret
.size lc_syscall5, . - lc_syscall5

.global lc_syscall6
.type lc_syscall6, %function
lc_syscall6:
    mov     x8, x0
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    mov     x3, x4
    mov     x4, x5
    mov     x5, x6
    svc     #0
    ret
.size lc_syscall6, . - lc_syscall6
