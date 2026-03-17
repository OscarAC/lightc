#ifndef LIGHTC_SYSCALL_NR_X86_64_H
#define LIGHTC_SYSCALL_NR_X86_64_H

/* x86_64 syscall numbers — from arch/x86/entry/syscalls/syscall_64.tbl */

#define SYS_read        0
#define SYS_write       1
#define SYS_open        2
#define SYS_close       3
#define SYS_lseek       8
#define SYS_mmap        9
#define SYS_mprotect    10
#define SYS_munmap      11
#define SYS_brk         12
#define SYS_ioctl       16
#define SYS_writev      20
#define SYS_openat      257
#define SYS_getpid      39
#define SYS_madvise     28
#define SYS_clone       56
#define SYS_arch_prctl  158
#define SYS_exit        60
#define SYS_gettid      186
#define SYS_futex       202
#define SYS_exit_group  231
#define SYS_getdents64  217

/* socket */
#define SYS_socket      41
#define SYS_connect     42
#define SYS_sendto      44
#define SYS_recvfrom    45
#define SYS_shutdown    48
#define SYS_bind        49
#define SYS_listen      50
#define SYS_setsockopt  54
#define SYS_accept4     288

/* time */
#define SYS_clock_gettime 228
#define SYS_nanosleep     35

/* process */
#define SYS_execve      59
#define SYS_wait4       61
#define SYS_pipe2       293
#define SYS_dup3        292
#define SYS_setsid      112

/* signals */
#define SYS_rt_sigaction   13
#define SYS_rt_sigprocmask 14
#define SYS_kill           62
#define SYS_signalfd4      289

/* io_uring (unified numbers across all architectures) */
#define SYS_io_uring_setup    425
#define SYS_io_uring_enter    426
#define SYS_io_uring_register 427

#endif /* LIGHTC_SYSCALL_NR_X86_64_H */
