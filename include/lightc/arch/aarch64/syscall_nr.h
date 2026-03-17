#ifndef LIGHTC_SYSCALL_NR_AARCH64_H
#define LIGHTC_SYSCALL_NR_AARCH64_H

/* aarch64 syscall numbers — from include/uapi/asm-generic/unistd.h */
/* Note: aarch64 has no SYS_open, only openat */

#define SYS_read        63
#define SYS_write       64
#define SYS_close       57
#define SYS_lseek       62
#define SYS_mmap        222
#define SYS_mprotect    226
#define SYS_munmap      215
#define SYS_brk         214
#define SYS_ioctl       29
#define SYS_writev      66
#define SYS_openat      56
#define SYS_getpid      172
#define SYS_futex       98
#define SYS_madvise     233
#define SYS_exit        93
#define SYS_exit_group  94
#define SYS_clone       220
#define SYS_gettid      178
#define SYS_getdents64  61

/* socket */
#define SYS_socket      198
#define SYS_bind        200
#define SYS_listen      201
#define SYS_connect     203
#define SYS_sendto      206
#define SYS_recvfrom    207
#define SYS_setsockopt  208
#define SYS_shutdown    210
#define SYS_accept4     242

/* time */
#define SYS_clock_gettime 113
#define SYS_nanosleep     101

/* process */
#define SYS_execve      221
#define SYS_wait4       260
#define SYS_pipe2       59
#define SYS_dup3        24
#define SYS_setsid      157

/* signals */
#define SYS_rt_sigaction   134
#define SYS_rt_sigprocmask 135
#define SYS_kill           129
#define SYS_signalfd4      74

/* io_uring (unified numbers across all architectures) */
#define SYS_io_uring_setup    425
#define SYS_io_uring_enter    426
#define SYS_io_uring_register 427

#endif /* LIGHTC_SYSCALL_NR_AARCH64_H */
