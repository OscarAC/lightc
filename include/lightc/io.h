#ifndef LIGHTC_IO_H
#define LIGHTC_IO_H

#include "types.h"

/*
 * Buffered I/O — readers, writers, file utilities, directory listing.
 *
 * Writers buffer output and flush to the kernel in batches.
 * Readers buffer input and refill from the kernel when exhausted.
 * File utilities handle common whole-file operations.
 * Directory listing wraps the getdents64 syscall.
 */

/* ========================================================================
 * Buffered Writer
 * ======================================================================== */

typedef struct {
    int32_t  fd;
    uint8_t *buffer;
    size_t   capacity;
    size_t   used;
} lc_writer;

/* Create a buffered writer for the given file descriptor.
 * Allocates an internal buffer of `buffer_size` bytes. */
lc_writer lc_writer_create(int32_t fd, size_t buffer_size);

/* Destroy a writer, flushing any remaining data and freeing the buffer. */
void lc_writer_destroy(lc_writer *writer);

/* Flush all buffered data to the file descriptor. */
void lc_writer_flush(lc_writer *writer);

/* Write a single byte. */
void lc_writer_put_byte(lc_writer *writer, uint8_t byte);

/* Write `length` bytes of `str`. */
void lc_writer_put_string(lc_writer *writer, const char *str, size_t length);

/* Write a single character. */
void lc_writer_put_char(lc_writer *writer, char c);

/* Write `length` bytes of `str`, followed by a newline. */
void lc_writer_put_line(lc_writer *writer, const char *str, size_t length);

/* Write a signed integer in decimal. */
void lc_writer_put_signed(lc_writer *writer, int64_t value);

/* Write an unsigned integer in decimal. */
void lc_writer_put_unsigned(lc_writer *writer, uint64_t value);

/* Write an unsigned integer in hexadecimal with 0x prefix. */
void lc_writer_put_hex(lc_writer *writer, uint64_t value);

/* Write a newline character. */
void lc_writer_put_newline(lc_writer *writer);

/* ========================================================================
 * Buffered Reader
 * ======================================================================== */

typedef struct {
    int32_t  fd;
    uint8_t *buffer;
    size_t   capacity;
    size_t   filled;       /* how many bytes currently in buffer */
    size_t   position;     /* current read position in buffer */
    bool     end_of_file;
} lc_reader;

/* Create a buffered reader for the given file descriptor.
 * Allocates an internal buffer of `buffer_size` bytes. */
lc_reader lc_reader_create(int32_t fd, size_t buffer_size);

/* Destroy a reader, freeing the buffer. */
void lc_reader_destroy(lc_reader *reader);

/* Read a single byte. Returns -1 on EOF. */
int32_t lc_reader_read_byte(lc_reader *reader);

/* Read up to `count` bytes into `buf`. Returns the number of bytes actually read. */
size_t lc_reader_read_bytes(lc_reader *reader, void *buf, size_t count);

/* Read a line (up to '\n' or EOF) into `buf`. Stores at most `buf_size - 1`
 * bytes plus a null terminator. The newline is NOT included.
 * Returns the line length, or -1 if EOF was reached with no data. */
int64_t lc_reader_read_line(lc_reader *reader, char *buf, size_t buf_size);

/* Check if the reader has reached end-of-file. */
bool lc_reader_is_end(const lc_reader *reader);

/* ========================================================================
 * File Utilities
 * ======================================================================== */

/* Read an entire file into a heap-allocated buffer.
 * Caller must lc_heap_free(*out_data) when done.
 * value = bytes read on success. */
[[nodiscard]] lc_result lc_file_read_all(const char *path, uint8_t **out_data, size_t *out_size);

/* Write a buffer to a file (creates or truncates).
 * value = bytes written on success. */
[[nodiscard]] lc_result lc_file_write_all(const char *path, const void *data, size_t size);

/* Get the size of a file in bytes. value = size on success. */
[[nodiscard]] lc_result lc_file_get_size(const char *path);

/* ========================================================================
 * Directory Listing
 * ======================================================================== */

/* Linux directory entry types */
#define LC_DT_UNKNOWN  0
#define LC_DT_DIR      4
#define LC_DT_REG      8
#define LC_DT_LNK      10

typedef struct {
    int32_t fd;
    uint8_t buffer[4096];
    size_t  filled;
    size_t  position;
    bool    done;
} lc_directory;

typedef struct {
    const char *name;
    uint8_t     type;   /* LC_DT_REG, LC_DT_DIR, etc. */
} lc_directory_entry;

/* Open a directory for listing. */
[[nodiscard]] lc_result lc_directory_open(lc_directory *dir, const char *path);

/* Get the next entry. Returns true if an entry was read, false when done. */
bool lc_directory_next(lc_directory *dir, lc_directory_entry *entry);

/* Close the directory. */
void lc_directory_close(lc_directory *dir);

#endif /* LIGHTC_IO_H */
