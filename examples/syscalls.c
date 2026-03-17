/*
 * Exercise the kernel wrappers:
 *   - write bytes to stdout
 *   - get process id
 *   - map/unmap anonymous memory
 *   - open/write/close a temp file, read it back
 */
#include <lightc/syscall.h>

/* Tiny helper until we have proper string/print functions */
static void say(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    lc_kernel_write_bytes(STDOUT, s, len);
}

/* Convert uint64_t to decimal string, return pointer into buf */
static char *u64_to_str(uint64_t val, char *buf, size_t size) {
    char *p = buf + size - 1;
    *p = '\0';
    if (val == 0) { *--p = '0'; return p; }
    while (val > 0) {
        *--p = '0' + (val % 10);
        val /= 10;
    }
    return p;
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    char numbuf[24];

    /* 1. get process id */
    int32_t pid = lc_kernel_get_process_id();
    say("pid: ");
    say(u64_to_str((uint64_t)pid, numbuf, sizeof(numbuf)));
    say("\n");

    /* 2. map anonymous memory */
    size_t page_size = 4096;
    void *mem = lc_kernel_map_memory(NULL, page_size,
                                     PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS,
                                     -1, 0);
    if (mem == MAP_FAILED) {
        say("map memory failed\n");
        lc_kernel_exit(1);
    }

    /* write into mapped memory */
    const char test[] = "lightc mapped memory works!";
    char *p = (char *)mem;
    for (size_t i = 0; i < sizeof(test); i++) p[i] = test[i];

    say("mmap: ");
    say(p);
    say("\n");

    /* unmap */
    lc_sysret ret = lc_kernel_unmap_memory(mem, page_size);
    say("munmap: ");
    say(ret == 0 ? "ok" : "fail");
    say("\n");

    /* 3. file I/O — write a temp file and read it back */
    const char *path = "/tmp/lightc_test.txt";
    const char content[] = "written by lightc\n";

    int32_t fd = (int32_t)lc_kernel_open_file(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        say("open file failed\n");
        lc_kernel_exit(1);
    }
    lc_kernel_write_bytes(fd, content, sizeof(content) - 1);
    lc_kernel_close_file(fd);

    /* read it back */
    fd = (int32_t)lc_kernel_open_file(path, O_RDONLY, 0);
    if (fd < 0) {
        say("open file (read) failed\n");
        lc_kernel_exit(1);
    }
    char readbuf[64];
    lc_sysret n = lc_kernel_read_bytes(fd, readbuf, sizeof(readbuf));
    lc_kernel_close_file(fd);

    say("file: ");
    if (n > 0) lc_kernel_write_bytes(STDOUT, readbuf, (size_t)n);

    say("all good\n");
    return 0;
}
