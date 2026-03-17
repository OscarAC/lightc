/*
 * Exercise every print function.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>

#define S(literal) literal, sizeof(literal) - 1

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* char */
    lc_print_string(STDOUT, S("char: "));
    lc_print_char(STDOUT, 'A');
    lc_print_newline(STDOUT);

    /* string + line */
    lc_print_string(STDOUT, S("string: "));
    lc_print_line(STDOUT, S("hello lightc"));

    /* unsigned */
    lc_print_string(STDOUT, S("unsigned: "));
    lc_print_unsigned(STDOUT, 0);
    lc_print_char(STDOUT, ' ');
    lc_print_unsigned(STDOUT, 42);
    lc_print_char(STDOUT, ' ');
    lc_print_unsigned(STDOUT, 18446744073709551615ULL); /* UINT64_MAX */
    lc_print_newline(STDOUT);

    /* signed */
    lc_print_string(STDOUT, S("signed: "));
    lc_print_signed(STDOUT, -1);
    lc_print_char(STDOUT, ' ');
    lc_print_signed(STDOUT, 0);
    lc_print_char(STDOUT, ' ');
    lc_print_signed(STDOUT, 9223372036854775807LL); /* INT64_MAX */
    lc_print_char(STDOUT, ' ');
    /* INT64_MIN: -9223372036854775808, but C can't express that as a literal */
    lc_print_signed(STDOUT, -9223372036854775807LL - 1);
    lc_print_newline(STDOUT);

    /* hex */
    lc_print_string(STDOUT, S("hex: "));
    lc_print_hex(STDOUT, 0);
    lc_print_char(STDOUT, ' ');
    lc_print_hex(STDOUT, 255);
    lc_print_char(STDOUT, ' ');
    lc_print_hex(STDOUT, 0xdeadbeef);
    lc_print_char(STDOUT, ' ');
    lc_print_hex(STDOUT, 0x4000f0); /* our _start address */
    lc_print_newline(STDOUT);

    /* byte hex — dump the ELF magic of our own binary */
    lc_print_string(STDOUT, S("byte_hex: "));
    uint8_t elf_magic[] = {0x7f, 'E', 'L', 'F'};
    for (size_t i = 0; i < sizeof(elf_magic); i++) {
        lc_print_byte_hex(STDOUT, elf_magic[i]);
        lc_print_char(STDOUT, ' ');
    }
    lc_print_newline(STDOUT);

    /* composable — pid display like we'd actually use it */
    lc_print_string(STDOUT, S("pid: "));
    lc_print_signed(STDOUT, lc_kernel_get_process_id());
    lc_print_newline(STDOUT);

    return 0;
}
