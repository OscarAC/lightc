#ifndef LIGHTC_LIBRARY_H
#define LIGHTC_LIBRARY_H

#include "types.h"

/*
 * Dynamic library loader — parse ELF, map segments, resolve symbols.
 * Our own dlopen/dlsym, built from scratch.
 *
 *   lc_library *lib = lc_library_open("./plugin.so");
 *   int (*add)(int, int) = lc_library_find_symbol(lib, "add");
 *   int result = add(2, 3);  // 5
 *   lc_library_close(lib);
 */

typedef struct lc_library lc_library;

/* Load a shared library (.so file). Returns NULL on failure. */
lc_library *lc_library_open(const char *path);

/* Find a symbol (function or variable) by name. Returns NULL if not found. */
void *lc_library_find_symbol(lc_library *lib, const char *name);

/* Unload the library and free resources. */
void lc_library_close(lc_library *lib);

/* Get the last error message (or NULL if no error). */
const char *lc_library_error(void);

#endif /* LIGHTC_LIBRARY_H */
