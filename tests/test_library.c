/*
 * test_library.c — tests for lightc dynamic library loading.
 *
 * Requires build/test_plugin.so to exist (built by the project).
 * The plugin exports: int plugin_add(int a, int b)
 */

#include "test.h"
#include <lightc/library.h>

#define PLUGIN_PATH "build/test_plugin.so"

/* ===== test_library_open_close ===== */

static void test_library_open_close(void) {
    lc_result_ptr r = lc_library_open(PLUGIN_PATH);
    TEST_ASSERT_PTR_OK(r);
    TEST_ASSERT_NOT_NULL(r.value);

    lc_library *lib = (lc_library *)r.value;
    lc_library_close(lib);
}

/* ===== test_library_find_symbol ===== */

typedef int (*plugin_add_fn)(int, int);

static void test_library_find_symbol(void) {
    lc_result_ptr lib_r = lc_library_open(PLUGIN_PATH);
    TEST_ASSERT_PTR_OK(lib_r);

    lc_library *lib = (lc_library *)lib_r.value;

    lc_result_ptr sym_r = lc_library_find_symbol(lib, "plugin_add");
    TEST_ASSERT_PTR_OK(sym_r);
    TEST_ASSERT_NOT_NULL(sym_r.value);

    plugin_add_fn add = (plugin_add_fn)sym_r.value;
    int result = add(2, 3);
    TEST_ASSERT_EQ(result, 5);

    result = add(-10, 10);
    TEST_ASSERT_EQ(result, 0);

    lc_library_close(lib);
}

/* ===== test_library_missing_symbol ===== */

static void test_library_missing_symbol(void) {
    lc_result_ptr lib_r = lc_library_open(PLUGIN_PATH);
    TEST_ASSERT_PTR_OK(lib_r);

    lc_library *lib = (lc_library *)lib_r.value;

    lc_result_ptr sym_r = lc_library_find_symbol(lib, "nonexistent_symbol_xyz");
    TEST_ASSERT_PTR_ERR(sym_r);

    lc_library_close(lib);
}

/* ===== test_library_open_nonexistent ===== */

static void test_library_open_nonexistent(void) {
    lc_result_ptr r = lc_library_open("/nonexistent.so");
    TEST_ASSERT_PTR_ERR(r);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    TEST_RUN(test_library_open_close);
    TEST_RUN(test_library_find_symbol);
    TEST_RUN(test_library_missing_symbol);
    TEST_RUN(test_library_open_nonexistent);

    return test_main();
}
