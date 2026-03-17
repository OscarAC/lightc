/* Minimal program — just exits. Measures pure startup + teardown. */
#include <lightc/syscall.h>

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    return 0;
}
