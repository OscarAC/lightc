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
    lc_result_ptr lib_r = lc_library_open("build/test_plugin.so");
    if (lc_ptr_is_err(lib_r)) {
        lc_print_line(STDOUT, S(" FAIL: unknown error"));
        return 1;
    }
    lc_library *lib = lib_r.value;
    say_pass_fail(lib != NULL);

    /* --- Find function symbols --- */
    lc_print_string(STDOUT, S("library_find_add"));
    typedef int (*add_func)(int, int);
    lc_result_ptr add_r = lc_library_find_symbol(lib, "plugin_add");
    add_func add = lc_ptr_is_err(add_r) ? NULL : (add_func)add_r.value;
    say_pass_fail(add != NULL);

    lc_print_string(STDOUT, S("library_find_multiply"));
    typedef int (*mul_func)(int, int);
    lc_result_ptr mul_r = lc_library_find_symbol(lib, "plugin_multiply");
    mul_func mul = lc_ptr_is_err(mul_r) ? NULL : (mul_func)mul_r.value;
    say_pass_fail(mul != NULL);

    lc_print_string(STDOUT, S("library_find_name"));
    typedef const char *(*name_func)(void);
    lc_result_ptr name_r = lc_library_find_symbol(lib, "plugin_name");
    name_func get_name = lc_ptr_is_err(name_r) ? NULL : (name_func)name_r.value;
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
    lc_result_ptr global_r = lc_library_find_symbol(lib, "plugin_global");
    int *global = lc_ptr_is_err(global_r) ? NULL : (int *)global_r.value;
    say_pass_fail(global != NULL && *global == 42);

    /* --- Stateful functions (test data segment works) --- */
    lc_print_string(STDOUT, S("library_stateful"));
    typedef int (*get_val_func)(void);
    typedef void (*set_val_func)(int);
    lc_result_ptr get_val_r = lc_library_find_symbol(lib, "plugin_get_value");
    get_val_func get_val = lc_ptr_is_err(get_val_r) ? NULL : (get_val_func)get_val_r.value;
    lc_result_ptr set_val_r = lc_library_find_symbol(lib, "plugin_set_value");
    set_val_func set_val = lc_ptr_is_err(set_val_r) ? NULL : (set_val_func)set_val_r.value;
    bool ok = false;
    if (get_val && set_val) {
        ok = (get_val() == 100);
        set_val(999);
        ok = ok && (get_val() == 999);
    }
    say_pass_fail(ok);

    /* --- Symbol not found --- */
    lc_print_string(STDOUT, S("library_not_found"));
    lc_result_ptr missing_r = lc_library_find_symbol(lib, "nonexistent_symbol");
    say_pass_fail(lc_ptr_is_err(missing_r));

    /* --- Close --- */
    lc_print_string(STDOUT, S("library_close"));
    lc_library_close(lib);
    say_pass_fail(true);

    /* --- Open nonexistent --- */
    lc_print_string(STDOUT, S("library_open_nonexistent"));
    lc_result_ptr bad_r = lc_library_open("/nonexistent/path.so");
    say_pass_fail(lc_ptr_is_err(bad_r));

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
