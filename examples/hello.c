#include <lightc/syscall.h>

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    const char msg[] = "hello from lightc — no libc, no problem\n";
    lc_kernel_write_bytes(STDOUT, msg, sizeof(msg) - 1);

    return 0;
}
