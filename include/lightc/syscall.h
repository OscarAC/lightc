#ifndef LIGHTC_SYSCALL_H
#define LIGHTC_SYSCALL_H

#include "types.h"

/*
 * Arch-specific syscall numbers
 */
#if defined(__x86_64__)
#include "arch/x86_64/syscall_nr.h"
#elif defined(__aarch64__)
#include "arch/aarch64/syscall_nr.h"
#else
#error "Unsupported architecture"
#endif

/*
 * Generic syscall — implemented in arch-specific assembly.
 * Supports up to 6 arguments (the Linux syscall maximum).
 */
lc_sysret lc_syscall0(int64_t nr);
lc_sysret lc_syscall1(int64_t nr, int64_t a0);
lc_sysret lc_syscall2(int64_t nr, int64_t a0, int64_t a1);
lc_sysret lc_syscall3(int64_t nr, int64_t a0, int64_t a1, int64_t a2);
lc_sysret lc_syscall4(int64_t nr, int64_t a0, int64_t a1, int64_t a2, int64_t a3);
lc_sysret lc_syscall5(int64_t nr, int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4);
lc_sysret lc_syscall6(int64_t nr, int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5);

/*
 * Constants
 */

/* openat: use current working directory as base */
#define AT_FDCWD  (-100)

/* open flags */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0100
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_NONBLOCK  04000
#define O_CLOEXEC   02000000

/* mmap prot */
#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

/* mmap flags */
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void *)-1)

/* lseek whence */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* standard file descriptors */
#define STDIN   0
#define STDOUT  1
#define STDERR  2

/* waitpid options */
#define WNOHANG  1

/* signal numbers */
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGKILL   9
#define SIGUSR1  10
#define SIGUSR2  12
#define SIGTERM  15
#define SIGCHLD  17

/* signal dispositions */
#define SIG_DFL  ((void *)0)
#define SIG_IGN  ((void *)1)

/* sigprocmask how */
#define LC_SIG_BLOCK    0
#define LC_SIG_UNBLOCK  1
#define LC_SIG_SETMASK  2

/* signalfd flags */
#define SFD_CLOEXEC   02000000
#define SFD_NONBLOCK  04000

/*
 * Typed kernel wrappers
 *
 * lc_kernel_* = talks directly to the Linux kernel.
 * All inlined — zero overhead over raw syscalls.
 */

/* --- Process --- */

static inline _Noreturn void lc_kernel_exit(int32_t code) {
    lc_syscall1(SYS_exit_group, code);
    __builtin_unreachable();
}

static inline int32_t lc_kernel_get_process_id(void) {
    return (int32_t)lc_syscall0(SYS_getpid);
}

/* Fork the current process. Returns 0 in child, child pid in parent. */
static inline lc_sysret lc_kernel_fork(void) {
    return lc_syscall5(SYS_clone, 17 /* SIGCHLD */, 0, 0, 0, 0);
}

/*
 * Clone a new process with the given flags and stack.
 *
 * Unlike raw SYS_clone, this is safe to use from C: fn and arg are placed
 * on the child's stack before the syscall, so the child never accesses the
 * parent's stack frame (which is unreachable after clone changes %rsp/sp).
 *
 * The child calls fn(arg) and then _exit_group — it never returns.
 * The parent receives the child's PID (positive) or -errno (negative).
 *
 * The caller must allocate the stack (e.g., via lc_kernel_map_memory)
 * and pass stack_top = base + size (stacks grow downward).
 */
lc_sysret lc_kernel_clone(int64_t flags, void *stack_top,
                           int (*fn)(void *), void *arg);

static inline lc_sysret lc_kernel_execute(const char *path, char *const argv[], char *const envp[]) {
    return lc_syscall3(SYS_execve, (int64_t)path, (int64_t)argv, (int64_t)envp);
}

static inline lc_sysret lc_kernel_wait_for_child(int32_t pid, int32_t *status, int32_t options) {
    return lc_syscall4(SYS_wait4, pid, (int64_t)status, options, 0);
}

static inline lc_sysret lc_kernel_create_pipe(int32_t fds[2], int32_t flags) {
    return lc_syscall2(SYS_pipe2, (int64_t)fds, flags);
}

static inline lc_sysret lc_kernel_duplicate_fd(int32_t old_fd, int32_t new_fd, int32_t flags) {
    return lc_syscall3(SYS_dup3, old_fd, new_fd, flags);
}

static inline lc_sysret lc_kernel_create_session(void) {
    return lc_syscall0(SYS_setsid);
}

/* --- File I/O --- */

static inline lc_sysret lc_kernel_open_file(const char *path, int32_t flags, int32_t mode) {
    return lc_syscall4(SYS_openat, AT_FDCWD, (int64_t)path, flags, mode);
}

static inline lc_sysret lc_kernel_open_file_at(int32_t dirfd, const char *path, int32_t flags, int32_t mode) {
    return lc_syscall4(SYS_openat, dirfd, (int64_t)path, flags, mode);
}

static inline lc_sysret lc_kernel_close_file(int32_t fd) {
    return lc_syscall1(SYS_close, fd);
}

static inline lc_sysret lc_kernel_read_bytes(int32_t fd, void *buf, size_t count) {
    return lc_syscall3(SYS_read, fd, (int64_t)buf, (int64_t)count);
}

static inline lc_sysret lc_kernel_write_bytes(int32_t fd, const void *buf, size_t count) {
    return lc_syscall3(SYS_write, fd, (int64_t)buf, (int64_t)count);
}

static inline lc_sysret lc_kernel_seek_position(int32_t fd, int64_t offset, int32_t whence) {
    return lc_syscall3(SYS_lseek, fd, offset, whence);
}

static inline lc_sysret lc_kernel_device_control(int32_t fd, uint64_t request, void *arg) {
    return lc_syscall3(SYS_ioctl, fd, (int64_t)request, (int64_t)arg);
}

/* --- Directories --- */

static inline lc_sysret lc_kernel_read_directory(int32_t fd, void *buf, size_t count) {
    return lc_syscall3(SYS_getdents64, fd, (int64_t)buf, (int64_t)count);
}

/* --- Memory --- */

static inline void *lc_kernel_map_memory(void *addr, size_t len, int32_t prot, int32_t flags,
                                         int32_t fd, int64_t offset) {
    return (void *)lc_syscall6(SYS_mmap, (int64_t)addr, (int64_t)len,
                               prot, flags, fd, offset);
}

static inline lc_sysret lc_kernel_unmap_memory(void *addr, size_t len) {
    return lc_syscall2(SYS_munmap, (int64_t)addr, (int64_t)len);
}

static inline lc_sysret lc_kernel_protect_memory(void *addr, size_t len, int32_t prot) {
    return lc_syscall3(SYS_mprotect, (int64_t)addr, (int64_t)len, prot);
}

static inline lc_sysret lc_kernel_set_heap_end(void *addr) {
    return lc_syscall1(SYS_brk, (int64_t)addr);
}

/* madvise advice flags */
#define MADV_DONTNEED  4

static inline lc_sysret lc_kernel_advise_memory(void *addr, size_t len, int32_t advice) {
    return lc_syscall3(SYS_madvise, (int64_t)addr, (int64_t)len, advice);
}

/* --- Threading --- */

static inline int32_t lc_kernel_get_thread_id(void) {
    return (int32_t)lc_syscall0(SYS_gettid);
}

/* futex operations */
#define FUTEX_WAIT  0
#define FUTEX_WAKE  1

static inline lc_sysret lc_kernel_futex_wait(int32_t *addr, int32_t expected) {
    return lc_syscall4(SYS_futex, (int64_t)addr, FUTEX_WAIT, expected, 0);
}

static inline lc_sysret lc_kernel_futex_wake(int32_t *addr, int32_t count) {
    return lc_syscall3(SYS_futex, (int64_t)addr, FUTEX_WAKE, count);
}

/* --- Time --- */

#define LC_CLOCK_REALTIME  0
#define LC_CLOCK_MONOTONIC 1

typedef struct {
    int64_t seconds;
    int64_t nanoseconds;
} lc_timespec;

static inline lc_sysret lc_kernel_get_clock(int32_t clock_id, lc_timespec *ts) {
    return lc_syscall2(SYS_clock_gettime, clock_id, (int64_t)ts);
}

static inline lc_sysret lc_kernel_sleep(const lc_timespec *duration) {
    return lc_syscall2(SYS_nanosleep, (int64_t)duration, 0);
}

/* --- Signals --- */

/*
 * Kernel sigaction struct — arch-conditional.
 * x86_64 has a restorer field; aarch64 does not.
 * We only use this for resetting signals to SIG_DFL (no restorer needed).
 */
typedef struct {
    void    *handler;
    uint64_t flags;
#if defined(__x86_64__)
    void    *restorer;
#endif
    uint64_t mask;
} lc_kernel_sigaction;

/*
 * signalfd_siginfo — 128 bytes, read from a signalfd.
 * We only use the first few fields; the rest is padding.
 */
typedef struct {
    uint32_t signal;
    int32_t  error;
    int32_t  code;
    uint32_t pid;
    uint32_t uid;
    int32_t  fd;
    uint32_t tid;
    uint32_t band;
    uint32_t overrun;
    uint32_t trapno;
    int32_t  status;
    int32_t  int_val;
    uint64_t ptr_val;
    uint64_t utime;
    uint64_t stime;
    uint64_t addr;
    uint8_t  _pad[48];
} lc_signal_info;

_Static_assert(sizeof(lc_signal_info) == 128, "lc_signal_info must be 128 bytes");

/* Reset a signal to SIG_DFL. Safe to call in child after fork (no restorer needed). */
static inline lc_sysret lc_kernel_reset_signal(int32_t signum) {
    lc_kernel_sigaction sa;
    sa.handler = SIG_DFL;
    sa.flags = 0;
#if defined(__x86_64__)
    sa.restorer = NULL;
#endif
    sa.mask = 0;
    return lc_syscall4(SYS_rt_sigaction, signum, (int64_t)&sa, 0, 8);
}

/* Set signal mask. how: LC_SIG_BLOCK/UNBLOCK/SETMASK. mask is a 64-bit bitmask. */
static inline lc_sysret lc_kernel_set_signal_mask(int32_t how, const uint64_t *new_mask,
                                                    uint64_t *old_mask) {
    return lc_syscall4(SYS_rt_sigprocmask, how, (int64_t)new_mask, (int64_t)old_mask, 8);
}

/* Create or update a signalfd. fd=-1 to create new. mask is a 64-bit bitmask. */
static inline lc_sysret lc_kernel_create_signal_fd(int32_t fd, const uint64_t *mask,
                                                     int32_t flags) {
    return lc_syscall4(SYS_signalfd4, fd, (int64_t)mask, 8, flags);
}

/* Send a signal to a process. */
static inline lc_sysret lc_kernel_send_signal(int32_t pid, int32_t signal) {
    return lc_syscall2(SYS_kill, pid, signal);
}

/* --- Filesystem --- */

/* mount flags */
#define MS_RDONLY       1
#define MS_NOSUID       2
#define MS_NODEV        4
#define MS_NOEXEC       8
#define MS_REMOUNT      32
#define MS_BIND         4096
#define MS_REC          16384
#define MS_PRIVATE      (1 << 18)
#define MS_SLAVE        (1 << 19)

/* umount flags */
#define MNT_DETACH  2

/* unlinkat flags */
#define AT_REMOVEDIR  0x200

/* faccessat2 flags */
#define AT_EACCESS  0x200

/* access modes */
#define F_OK  0
#define R_OK  4
#define W_OK  2
#define X_OK  1

static inline lc_sysret lc_kernel_mount(const char *source, const char *target,
                                         const char *fstype, uint64_t flags, const void *data) {
    return lc_syscall5(SYS_mount, (int64_t)source, (int64_t)target,
                       (int64_t)fstype, (int64_t)flags, (int64_t)data);
}

static inline lc_sysret lc_kernel_umount(const char *target, int32_t flags) {
    return lc_syscall2(SYS_umount2, (int64_t)target, flags);
}

static inline lc_sysret lc_kernel_pivot_root(const char *new_root, const char *put_old) {
    return lc_syscall2(SYS_pivot_root, (int64_t)new_root, (int64_t)put_old);
}

static inline lc_sysret lc_kernel_chdir(const char *path) {
    return lc_syscall1(SYS_chdir, (int64_t)path);
}

static inline lc_sysret lc_kernel_mkdirat(int32_t dirfd, const char *path, int32_t mode) {
    return lc_syscall3(SYS_mkdirat, dirfd, (int64_t)path, mode);
}

static inline lc_sysret lc_kernel_mknodat(int32_t dirfd, const char *path, int32_t mode, uint64_t dev) {
    return lc_syscall4(SYS_mknodat, dirfd, (int64_t)path, mode, (int64_t)dev);
}

static inline lc_sysret lc_kernel_symlinkat(const char *target, int32_t dirfd, const char *linkpath) {
    return lc_syscall3(SYS_symlinkat, (int64_t)target, dirfd, (int64_t)linkpath);
}

static inline lc_sysret lc_kernel_unlinkat(int32_t dirfd, const char *path, int32_t flags) {
    return lc_syscall3(SYS_unlinkat, dirfd, (int64_t)path, flags);
}

static inline lc_sysret lc_kernel_readlinkat(int32_t dirfd, const char *path, char *buf, size_t bufsiz) {
    return lc_syscall4(SYS_readlinkat, dirfd, (int64_t)path, (int64_t)buf, (int64_t)bufsiz);
}

static inline lc_sysret lc_kernel_fchownat(int32_t dirfd, const char *path,
                                            uint32_t uid, uint32_t gid, int32_t flags) {
    return lc_syscall5(SYS_fchownat, dirfd, (int64_t)path, uid, gid, flags);
}

static inline lc_sysret lc_kernel_fchmodat(int32_t dirfd, const char *path, int32_t mode, int32_t flags) {
    return lc_syscall4(SYS_fchmodat, dirfd, (int64_t)path, mode, flags);
}

static inline lc_sysret lc_kernel_faccessat2(int32_t dirfd, const char *path, int32_t mode, int32_t flags) {
    return lc_syscall4(SYS_faccessat2, dirfd, (int64_t)path, mode, flags);
}

/* --- Namespaces / Containers --- */

/* clone flags for namespaces */
#define CLONE_NEWNS     0x00020000
#define CLONE_NEWUTS    0x04000000
#define CLONE_NEWIPC    0x08000000
#define CLONE_NEWUSER   0x10000000
#define CLONE_NEWPID    0x20000000
#define CLONE_NEWNET    0x40000000

static inline lc_sysret lc_kernel_unshare(int32_t flags) {
    return lc_syscall1(SYS_unshare, flags);
}

static inline lc_sysret lc_kernel_setns(int32_t fd, int32_t nstype) {
    return lc_syscall2(SYS_setns, fd, nstype);
}

static inline lc_sysret lc_kernel_sethostname(const char *name, size_t len) {
    return lc_syscall2(SYS_sethostname, (int64_t)name, (int64_t)len);
}

static inline lc_sysret lc_kernel_prctl(int32_t option, uint64_t arg2, uint64_t arg3,
                                         uint64_t arg4, uint64_t arg5) {
    return lc_syscall5(SYS_prctl, option, (int64_t)arg2, (int64_t)arg3,
                       (int64_t)arg4, (int64_t)arg5);
}

static inline lc_sysret lc_kernel_seccomp(uint32_t op, uint32_t flags, void *args) {
    return lc_syscall3(SYS_seccomp, op, flags, (int64_t)args);
}

/* --- Credentials --- */

static inline uint32_t lc_kernel_getuid(void) {
    return (uint32_t)lc_syscall0(SYS_getuid);
}

static inline lc_sysret lc_kernel_setuid(uint32_t uid) {
    return lc_syscall1(SYS_setuid, uid);
}

static inline lc_sysret lc_kernel_setgid(uint32_t gid) {
    return lc_syscall1(SYS_setgid, gid);
}

static inline lc_sysret lc_kernel_setgroups(size_t size, const uint32_t *list) {
    return lc_syscall2(SYS_setgroups, (int64_t)size, (int64_t)list);
}

/* --- Async I/O --- */

static inline lc_sysret lc_kernel_io_ring_setup(uint32_t entries, void *params) {
    return lc_syscall2(SYS_io_uring_setup, entries, (int64_t)params);
}

static inline lc_sysret lc_kernel_io_ring_enter(int32_t fd, uint32_t to_submit,
                                                 uint32_t min_complete, uint32_t flags) {
    return lc_syscall6(SYS_io_uring_enter, fd, to_submit, min_complete, flags,
                       0 /* sig */, 0 /* sigsz */);
}

#endif /* LIGHTC_SYSCALL_H */
