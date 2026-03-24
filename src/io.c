#include <lightc/io.h>
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/heap.h>

/* ========================================================================
 * Buffered Writer
 * ======================================================================== */

lc_writer lc_writer_create(int32_t fd, size_t buffer_size) {
    lc_writer w;
    w.fd       = fd;
    lc_result_ptr alloc = lc_heap_allocate(buffer_size);
    w.buffer   = alloc.value;
    w.capacity = w.buffer ? buffer_size : 0;
    w.used     = 0;
    return w;
}

void lc_writer_destroy(lc_writer *writer) {
    if (writer->buffer == NULL) return;
    lc_writer_flush(writer);
    lc_heap_free(writer->buffer);
    writer->buffer   = NULL;
    writer->capacity = 0;
    writer->used     = 0;
}

void lc_writer_flush(lc_writer *writer) {
    if (writer->used == 0) return;

    size_t written = 0;
    while (written < writer->used) {
        lc_sysret ret = lc_kernel_write_bytes(writer->fd,
                                              writer->buffer + written,
                                              writer->used - written);
        if (ret <= 0) break;  /* error or zero bytes — stop */
        written += (size_t)ret;
    }
    writer->used = 0;
}

void lc_writer_put_byte(lc_writer *writer, uint8_t byte) {
    if (writer->buffer == NULL) return;
    if (writer->used >= writer->capacity) {
        lc_writer_flush(writer);
    }
    writer->buffer[writer->used++] = byte;
}

void lc_writer_put_string(lc_writer *writer, const char *str, size_t length) {
    size_t remaining = length;
    const char *src = str;

    while (remaining > 0) {
        size_t space = writer->capacity - writer->used;
        if (space == 0) {
            lc_writer_flush(writer);
            space = writer->capacity;
        }

        size_t chunk = remaining < space ? remaining : space;
        lc_bytes_copy(writer->buffer + writer->used, src, chunk);
        writer->used += chunk;
        src       += chunk;
        remaining -= chunk;
    }
}

void lc_writer_put_char(lc_writer *writer, char c) {
    lc_writer_put_byte(writer, (uint8_t)c);
}

void lc_writer_put_line(lc_writer *writer, const char *str, size_t length) {
    lc_writer_put_string(writer, str, length);
    lc_writer_put_char(writer, '\n');
}

void lc_writer_put_unsigned(lc_writer *writer, uint64_t value) {
    /* max uint64_t is 20 digits */
    char buf[20];
    char *p = buf + sizeof(buf);

    if (value == 0) {
        lc_writer_put_char(writer, '0');
        return;
    }

    while (value > 0) {
        *--p = '0' + (char)(value % 10);
        value /= 10;
    }

    lc_writer_put_string(writer, p, (size_t)(buf + sizeof(buf) - p));
}

void lc_writer_put_signed(lc_writer *writer, int64_t value) {
    if (value < 0) {
        lc_writer_put_char(writer, '-');
        /* Handle INT64_MIN: -(INT64_MIN) overflows, cast first */
        lc_writer_put_unsigned(writer, (uint64_t)(-(value + 1)) + 1);
    } else {
        lc_writer_put_unsigned(writer, (uint64_t)value);
    }
}

static const char io_hex_digits[] = "0123456789abcdef";

void lc_writer_put_hex(lc_writer *writer, uint64_t value) {
    char buf[18]; /* "0x" + up to 16 hex digits */
    char *p = buf + sizeof(buf);

    if (value == 0) {
        lc_writer_put_string(writer, "0x0", 3);
        return;
    }

    while (value > 0) {
        *--p = io_hex_digits[value & 0xf];
        value >>= 4;
    }

    *--p = 'x';
    *--p = '0';

    lc_writer_put_string(writer, p, (size_t)(buf + sizeof(buf) - p));
}

void lc_writer_put_newline(lc_writer *writer) {
    lc_writer_put_char(writer, '\n');
}

/* ========================================================================
 * Buffered Reader
 * ======================================================================== */

lc_reader lc_reader_create(int32_t fd, size_t buffer_size) {
    lc_reader r;
    r.fd          = fd;
    lc_result_ptr alloc = lc_heap_allocate(buffer_size);
    r.buffer      = alloc.value;
    r.capacity    = r.buffer ? buffer_size : 0;
    r.filled      = 0;
    r.position    = 0;
    r.end_of_file = false;
    return r;
}

void lc_reader_destroy(lc_reader *reader) {
    if (reader->buffer == NULL) return;
    lc_heap_free(reader->buffer);
    reader->buffer   = NULL;
    reader->capacity = 0;
    reader->filled   = 0;
    reader->position = 0;
}

/* Refill the internal buffer from the file descriptor. */
static void reader_refill(lc_reader *reader) {
    if (reader->end_of_file) return;

    reader->position = 0;
    reader->filled   = 0;

    lc_sysret ret = lc_kernel_read_bytes(reader->fd, reader->buffer, reader->capacity);
    if (ret <= 0) {
        reader->end_of_file = true;
    } else {
        reader->filled = (size_t)ret;
    }
}

int32_t lc_reader_read_byte(lc_reader *reader) {
    if (reader->buffer == NULL) return -1;
    if (reader->position >= reader->filled) {
        reader_refill(reader);
        if (reader->end_of_file) return -1;
    }
    return (int32_t)reader->buffer[reader->position++];
}

size_t lc_reader_read_bytes(lc_reader *reader, void *buf, size_t count) {
    if (reader->buffer == NULL) return 0;
    uint8_t *dst = (uint8_t *)buf;
    size_t total = 0;

    while (total < count) {
        /* If buffer is exhausted, refill */
        if (reader->position >= reader->filled) {
            reader_refill(reader);
            if (reader->end_of_file) break;
        }

        size_t available = reader->filled - reader->position;
        size_t want      = count - total;
        size_t chunk     = want < available ? want : available;

        lc_bytes_copy(dst + total, reader->buffer + reader->position, chunk);
        reader->position += chunk;
        total            += chunk;
    }

    return total;
}

int64_t lc_reader_read_line(lc_reader *reader, char *buf, size_t buf_size) {
    if (reader->buffer == NULL) return -1;
    if (buf_size == 0) return -1;

    size_t length = 0;
    size_t max    = buf_size - 1; /* leave room for null terminator */

    while (length < max) {
        int32_t byte = lc_reader_read_byte(reader);
        if (byte < 0) {
            /* EOF */
            if (length == 0) {
                buf[0] = '\0';
                return -1;
            }
            break;
        }
        if ((char)byte == '\n') {
            break;
        }
        buf[length++] = (char)byte;
    }

    /* If we stopped because buffer is full, consume rest of line */
    if (length == max) {
        for (;;) {
            int32_t byte = lc_reader_read_byte(reader);
            if (byte < 0 || (char)byte == '\n') break;
        }
    }

    buf[length] = '\0';
    return (int64_t)length;
}

bool lc_reader_is_end(const lc_reader *reader) {
    return reader->end_of_file && reader->position >= reader->filled;
}

/* ========================================================================
 * File Utilities
 * ======================================================================== */

lc_result lc_file_read_all(const char *path, uint8_t **out_data, size_t *out_size) {
    *out_data = NULL;
    *out_size = 0;

    lc_sysret fd_ret = lc_kernel_open_file(path, O_RDONLY, 0);
    if (fd_ret < 0) return lc_err((int32_t)(-fd_ret));
    int32_t fd = (int32_t)fd_ret;

    /* Get file size via seek to end */
    lc_sysret end = lc_kernel_seek_position(fd, 0, SEEK_END);
    if (end < 0) {
        lc_kernel_close_file(fd);
        return lc_err((int32_t)(-end));
    }
    size_t file_size = (size_t)end;

    /* Seek back to start */
    lc_sysret start = lc_kernel_seek_position(fd, 0, SEEK_SET);
    if (start < 0) {
        lc_kernel_close_file(fd);
        return lc_err((int32_t)(-start));
    }

    /* Handle empty files */
    if (file_size == 0) {
        lc_kernel_close_file(fd);
        *out_data = NULL;
        *out_size = 0;
        return lc_ok(0);
    }

    /* Allocate buffer */
    lc_result_ptr alloc = lc_heap_allocate(file_size);
    uint8_t *data = alloc.value;
    if (data == NULL) {
        lc_kernel_close_file(fd);
        return lc_err(LC_ERR_NOMEM);
    }

    /* Read the entire file */
    size_t total = 0;
    while (total < file_size) {
        lc_sysret ret = lc_kernel_read_bytes(fd, data + total, file_size - total);
        if (ret <= 0) break;
        total += (size_t)ret;
    }

    lc_kernel_close_file(fd);

    if (total != file_size) {
        lc_heap_free(data);
        return lc_err(LC_ERR_IO);
    }

    *out_data = data;
    *out_size = file_size;
    return lc_ok((int64_t)file_size);
}

lc_result lc_file_write_all(const char *path, const void *data, size_t size) {
    lc_sysret fd_ret = lc_kernel_open_file(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ret < 0) return lc_err((int32_t)(-fd_ret));
    int32_t fd = (int32_t)fd_ret;

    size_t written = 0;
    while (written < size) {
        lc_sysret ret = lc_kernel_write_bytes(fd, (const uint8_t *)data + written,
                                              size - written);
        if (ret <= 0) {
            lc_kernel_close_file(fd);
            return lc_err(ret < 0 ? (int32_t)(-ret) : LC_ERR_IO);
        }
        written += (size_t)ret;
    }

    lc_kernel_close_file(fd);
    return lc_ok((int64_t)size);
}

lc_result lc_file_get_size(const char *path) {
    lc_sysret fd_ret = lc_kernel_open_file(path, O_RDONLY, 0);
    if (fd_ret < 0) return lc_err((int32_t)(-fd_ret));
    int32_t fd = (int32_t)fd_ret;

    lc_sysret end = lc_kernel_seek_position(fd, 0, SEEK_END);
    lc_kernel_close_file(fd);

    if (end < 0) return lc_err((int32_t)(-end));
    return lc_ok(end);
}

/* ========================================================================
 * Directory Listing
 * ======================================================================== */

/*
 * The kernel's getdents64 returns entries in this format.
 * We define it here rather than importing kernel headers.
 */
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

lc_result lc_directory_open(lc_directory *dir, const char *path) {
    lc_sysret fd_ret = lc_kernel_open_file(path, O_RDONLY, 0);
    if (fd_ret < 0) return lc_err((int32_t)(-fd_ret));

    dir->fd       = (int32_t)fd_ret;
    dir->filled   = 0;
    dir->position = 0;
    dir->done     = false;
    return lc_ok((int64_t)dir->fd);
}

bool lc_directory_next(lc_directory *dir, lc_directory_entry *entry) {
    for (;;) {
        /* If we have data in the buffer, return the next entry */
        if (dir->position < dir->filled) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(dir->buffer + dir->position);
            entry->name = d->d_name;
            entry->type = d->d_type;
            dir->position += d->d_reclen;
            return true;
        }

        /* Buffer exhausted — try to read more */
        if (dir->done) return false;

        lc_sysret ret = lc_kernel_read_directory(dir->fd, dir->buffer, sizeof(dir->buffer));
        if (ret <= 0) {
            dir->done = true;
            return false;
        }

        dir->filled   = (size_t)ret;
        dir->position = 0;
    }
}

void lc_directory_close(lc_directory *dir) {
    if (dir->fd >= 0) {
        lc_kernel_close_file(dir->fd);
        dir->fd = -1;
    }
    dir->done = true;
}
