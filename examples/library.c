#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/library.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed) lc_print_line(STDOUT, S(" PASS"));
    else lc_print_line(STDOUT, S(" FAIL"));
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* --- Open library --- */
    lc_print_string(STDOUT, S("library_open"));
    lc_library *lib = lc_library_open("build/test_plugin.so");
    if (!lib) {
        lc_print_string(STDOUT, S(" FAIL: "));
        const char *err = lc_library_error();
        if (err) lc_print_line(STDOUT, err, lc_string_length(err));
        else lc_print_line(STDOUT, S("unknown error"));
        return 1;
    }
    say_pass_fail(lib != NULL);

    /* --- Find function symbols --- */
    lc_print_string(STDOUT, S("library_find_add"));
    typedef int (*add_func)(int, int);
    add_func add = (add_func)lc_library_find_symbol(lib, "plugin_add");
    say_pass_fail(add != NULL);

    lc_print_string(STDOUT, S("library_find_multiply"));
    typedef int (*mul_func)(int, int);
    mul_func mul = (mul_func)lc_library_find_symbol(lib, "plugin_multiply");
    say_pass_fail(mul != NULL);

    lc_print_string(STDOUT, S("library_find_name"));
    typedef const char *(*name_func)(void);
    name_func get_name = (name_func)lc_library_find_symbol(lib, "plugin_name");
    say_pass_fail(get_name != NULL);

    /* --- Call functions --- */
    lc_print_string(STDOUT, S("library_call_add"));
    if (add) say_pass_fail(add(2, 3) == 5);
    else say_pass_fail(false);

    lc_print_string(STDOUT, S("library_call_multiply"));
    if (mul) say_pass_fail(mul(4, 5) == 20);
    else say_pass_fail(false);

    lc_print_string(STDOUT, S("library_call_name"));
    if (get_name) {
        const char *name = get_name();
        say_pass_fail(name != NULL && lc_string_equal(name, lc_string_length(name),
                      S("test_plugin")));
    } else say_pass_fail(false);

    /* --- Find and use global variable --- */
    lc_print_string(STDOUT, S("library_find_global"));
    int *global = (int *)lc_library_find_symbol(lib, "plugin_global");
    say_pass_fail(global != NULL && *global == 42);

    /* --- Stateful functions (test data segment works) --- */
    lc_print_string(STDOUT, S("library_stateful"));
    typedef int (*get_val_func)(void);
    typedef void (*set_val_func)(int);
    get_val_func get_val = (get_val_func)lc_library_find_symbol(lib, "plugin_get_value");
    set_val_func set_val = (set_val_func)lc_library_find_symbol(lib, "plugin_set_value");
    bool ok = false;
    if (get_val && set_val) {
        ok = (get_val() == 100);
        set_val(999);
        ok = ok && (get_val() == 999);
    }
    say_pass_fail(ok);

    /* --- Symbol not found --- */
    lc_print_string(STDOUT, S("library_not_found"));
    void *missing = lc_library_find_symbol(lib, "nonexistent_symbol");
    say_pass_fail(missing == NULL);

    /* --- Close --- */
    lc_print_string(STDOUT, S("library_close"));
    lc_library_close(lib);
    say_pass_fail(true);

    /* --- Open nonexistent --- */
    lc_print_string(STDOUT, S("library_open_nonexistent"));
    lc_library *bad = lc_library_open("/nonexistent/path.so");
    say_pass_fail(bad == NULL);

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
