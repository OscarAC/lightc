/* test_plugin.c — a simple shared library for testing lc_library_*
 *
 * Compile with: gcc -shared -fPIC -nostdlib -o build/test_plugin.so examples/test_plugin.c
 */

static int internal_value = 100;

int plugin_add(int a, int b) {
    return a + b;
}

int plugin_multiply(int a, int b) {
    return a * b;
}

int plugin_get_value(void) {
    return internal_value;
}

void plugin_set_value(int v) {
    internal_value = v;
}

const char *plugin_name(void) {
    return "test_plugin";
}

int plugin_global = 42;
