#ifndef LIGHTC_RESULT_H
#define LIGHTC_RESULT_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Result types for error handling.
 *
 * Both structs are exactly 16 bytes, returned in two registers on
 * x86_64 (rax+rdx) and aarch64 (x0+x1).  Zero overhead versus
 * returning a single value.
 *
 * Convention: error == 0 means success, error > 0 is a Linux errno.
 * Library-specific error codes start at 4096.
 */

typedef struct {
    int64_t value;
    int32_t error;
} lc_result;

typedef struct {
    void   *value;
    int32_t error;
} lc_result_ptr;

/* --- Error codes (Linux errno values) --- */

enum {
    LC_OK              = 0,
    LC_ERR_PERM        = 1,    /* EPERM    — operation not permitted */
    LC_ERR_NOENT       = 2,    /* ENOENT   — no such file or directory */
    LC_ERR_INTR        = 4,    /* EINTR    — interrupted system call */
    LC_ERR_IO          = 5,    /* EIO      — I/O error */
    LC_ERR_AGAIN       = 11,   /* EAGAIN   — try again (EWOULDBLOCK) */
    LC_ERR_NOMEM       = 12,   /* ENOMEM   — out of memory */
    LC_ERR_ACCES       = 13,   /* EACCES   — permission denied */
    LC_ERR_EXIST       = 17,   /* EEXIST   — file exists */
    LC_ERR_INVAL       = 22,   /* EINVAL   — invalid argument */
    LC_ERR_NOSPC       = 28,   /* ENOSPC   — no space left on device */
    LC_ERR_PIPE        = 32,   /* EPIPE    — broken pipe */
    LC_ERR_ADDRINUSE   = 98,   /* EADDRINUSE */
    LC_ERR_CONNREFUSED = 111,  /* ECONNREFUSED */
    LC_ERR_CONNRESET   = 104,  /* ECONNRESET */
    LC_ERR_NOTCONN     = 107,  /* ENOTCONN */
    LC_ERR_TIMEDOUT    = 110,  /* ETIMEDOUT */
    LC_ERR_OVERFLOW    = 75,   /* EOVERFLOW */

    /* Library-specific (4096+) */
    LC_ERR_BAD_ELF_MAGIC  = 4096,
    LC_ERR_NOT_64BIT      = 4097,
    LC_ERR_BAD_MACHINE    = 4098,
    LC_ERR_NO_LOAD_SEG    = 4099,
    LC_ERR_NO_DYNAMIC     = 4100,
    LC_ERR_RELOC_FAILED   = 4101,
    LC_ERR_FULL           = 4102,  /* capacity exceeded (atexit table, etc.) */
};

/* --- Constructors --- */

[[gnu::const]]
static inline lc_result lc_ok(int64_t value) {
    return (lc_result){ .value = value, .error = LC_OK };
}

[[gnu::const]]
static inline lc_result lc_err(int32_t error) {
    return (lc_result){ .value = 0, .error = error };
}

[[gnu::const]]
static inline lc_result_ptr lc_ok_ptr(void *ptr) {
    return (lc_result_ptr){ .value = ptr, .error = LC_OK };
}

[[gnu::const]]
static inline lc_result_ptr lc_err_ptr(int32_t error) {
    return (lc_result_ptr){ .value = NULL, .error = error };
}

/* Convert from lc_sysret (negative-errno convention) to lc_result. */
[[gnu::const]]
static inline lc_result lc_result_from_sysret(int64_t ret) {
    if (ret < 0) return (lc_result){ .value = 0, .error = (int32_t)(-ret) };
    return (lc_result){ .value = ret, .error = LC_OK };
}

/* Convert from lc_sysret to lc_result_ptr (for mmap-style calls). */
[[gnu::const]]
static inline lc_result_ptr lc_result_ptr_from_sysret(int64_t ret) {
    if (ret < 0) return (lc_result_ptr){ .value = NULL, .error = (int32_t)(-ret) };
    return (lc_result_ptr){ .value = (void *)ret, .error = LC_OK };
}

/* --- Queries --- */

[[gnu::const]]
static inline bool lc_is_ok(lc_result r) { return r.error == 0; }

[[gnu::const]]
static inline bool lc_is_err(lc_result r) { return r.error != 0; }

[[gnu::const]]
static inline bool lc_ptr_is_ok(lc_result_ptr r) { return r.error == 0; }

[[gnu::const]]
static inline bool lc_ptr_is_err(lc_result_ptr r) { return r.error != 0; }

/* --- Error name lookup --- */

/* Returns a short string for the error code (e.g. "ENOMEM"). */
[[gnu::const]]
static inline const char *lc_error_name(int32_t error) {
    switch (error) {
    case LC_OK:              return "OK";
    case LC_ERR_PERM:        return "EPERM";
    case LC_ERR_NOENT:       return "ENOENT";
    case LC_ERR_INTR:        return "EINTR";
    case LC_ERR_IO:          return "EIO";
    case LC_ERR_AGAIN:       return "EAGAIN";
    case LC_ERR_NOMEM:       return "ENOMEM";
    case LC_ERR_ACCES:       return "EACCES";
    case LC_ERR_EXIST:       return "EEXIST";
    case LC_ERR_INVAL:       return "EINVAL";
    case LC_ERR_NOSPC:       return "ENOSPC";
    case LC_ERR_PIPE:        return "EPIPE";
    case LC_ERR_OVERFLOW:    return "EOVERFLOW";
    case LC_ERR_ADDRINUSE:   return "EADDRINUSE";
    case LC_ERR_CONNREFUSED: return "ECONNREFUSED";
    case LC_ERR_CONNRESET:   return "ECONNRESET";
    case LC_ERR_NOTCONN:     return "ENOTCONN";
    case LC_ERR_TIMEDOUT:    return "ETIMEDOUT";
    case LC_ERR_BAD_ELF_MAGIC: return "BAD_ELF_MAGIC";
    case LC_ERR_NOT_64BIT:     return "NOT_64BIT";
    case LC_ERR_BAD_MACHINE:   return "BAD_MACHINE";
    case LC_ERR_NO_LOAD_SEG:   return "NO_LOAD_SEG";
    case LC_ERR_NO_DYNAMIC:    return "NO_DYNAMIC";
    case LC_ERR_RELOC_FAILED:  return "RELOC_FAILED";
    case LC_ERR_FULL:          return "FULL";
    default:                   return "UNKNOWN";
    }
}

#endif /* LIGHTC_RESULT_H */
