/*
 * heapviz — interactive heap allocator visualizer.
 *
 * A CLI tool that lets you exercise the lightc heap allocator and
 * view live statistics with visual bar charts.
 *
 * Build: must compile with -DLC_STATS=1 to enable heap tracking.
 *
 * Commands:
 *   alloc <size>        Allocate <size> bytes, prints slot ID
 *   zalloc <size>       Allocate zeroed
 *   realloc <id> <size> Resize allocation
 *   free <id>           Free allocation by slot ID
 *   fill <n> <size>     Allocate n blocks of <size> bytes
 *   drain               Free all allocations
 *   stats               Show heap statistics dashboard
 *   reset               Reset cumulative counters
 *   slots               List all active allocations
 *   help                Show commands
 *   quit                Exit
 */

#include <lightc/heap.h>
#include <lightc/io.h>
#include <lightc/print.h>
#include <lightc/format.h>
#include <lightc/string.h>

#define OUT 1  /* stdout */
#define MAX_SLOTS 4096

static void *slots[MAX_SLOTS];
static size_t slot_sizes[MAX_SLOTS];
static uint32_t slot_count = 0;

/* --- Helpers --- */

static void put(const char *s) {
    lc_print_string(OUT, s, lc_string_length(s));
}

static void putln(const char *s) {
    lc_print_line(OUT, s, lc_string_length(s));
}

static void putnum(uint64_t n) {
    lc_print_unsigned(OUT, n);
}

/* Parse an unsigned integer from a string. Returns 0 on failure. */
static uint64_t parse_uint(const char *s, size_t len) {
    uint64_t val = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') break;
        val = val * 10 + (uint64_t)(s[i] - '0');
    }
    return val;
}

/* Skip leading whitespace, return pointer + remaining length */
static const char *skip_space(const char *s, size_t *len) {
    while (*len > 0 && (*s == ' ' || *s == '\t')) { s++; (*len)--; }
    return s;
}

/* Extract next whitespace-delimited token */
static const char *next_token(const char *s, size_t len, size_t *tok_len) {
    *tok_len = 0;
    while (*tok_len < len && s[*tok_len] != ' ' && s[*tok_len] != '\t' && s[*tok_len] != '\0') {
        (*tok_len)++;
    }
    return s;
}

/* Allocate a slot. Returns slot index or -1. */
static int32_t slot_alloc(void *ptr, size_t size) {
    if (ptr == NULL) return -1;
    /* Find first empty slot */
    for (uint32_t i = 0; i < slot_count; i++) {
        if (slots[i] == NULL) {
            slots[i] = ptr;
            slot_sizes[i] = size;
            return (int32_t)i;
        }
    }
    if (slot_count >= MAX_SLOTS) {
        lc_heap_free(ptr);
        return -1;
    }
    uint32_t id = slot_count++;
    slots[id] = ptr;
    slot_sizes[id] = size;
    return (int32_t)id;
}

/* --- Visual bar rendering --- */

#define BAR_WIDTH 40

static void print_bar(uint64_t value, uint64_t max_val) {
    if (max_val == 0) max_val = 1;
    uint32_t filled = (uint32_t)(value * BAR_WIDTH / max_val);
    if (filled > BAR_WIDTH) filled = BAR_WIDTH;

    put("[");
    for (uint32_t i = 0; i < BAR_WIDTH; i++) {
        if (i < filled) put("#");
        else put(".");
    }
    put("]");
}

/* Format a byte count with unit suffix */
static size_t format_bytes(char *buf, size_t cap, uint64_t bytes) {
    lc_format f = lc_format_start(buf, cap);
    if (bytes >= 1024 * 1024) {
        lc_format_add_unsigned(&f, bytes / (1024 * 1024));
        lc_format_add_char(&f, '.');
        lc_format_add_unsigned(&f, (bytes % (1024 * 1024)) * 10 / (1024 * 1024));
        lc_format_add_string(&f, " MiB", 4);
    } else if (bytes >= 1024) {
        lc_format_add_unsigned(&f, bytes / 1024);
        lc_format_add_char(&f, '.');
        lc_format_add_unsigned(&f, (bytes % 1024) * 10 / 1024);
        lc_format_add_string(&f, " KiB", 4);
    } else {
        lc_format_add_unsigned(&f, bytes);
        lc_format_add_string(&f, " B", 2);
    }
    return lc_format_finish(&f);
}

/* --- Stats dashboard --- */

static void show_stats(void) {
    lc_heap_stats s;
    lc_heap_get_stats(&s);

    char buf[128];

    putln("");
    putln("  ============= Heap Statistics =============");
    putln("");

    /* Active allocations */
    put("  Active allocs:   ");
    putnum(s.active_allocations);
    put(" / peak ");
    putnum(s.peak_active_allocations);
    lc_print_newline(OUT);

    /* Active bytes with bar */
    size_t len = format_bytes(buf, sizeof(buf), s.active_bytes);
    put("  Active bytes:    ");
    lc_print_string(OUT, buf, len);
    put(" / peak ");
    len = format_bytes(buf, sizeof(buf), s.peak_active_bytes);
    lc_print_string(OUT, buf, len);
    lc_print_newline(OUT);

    /* Bar: active vs peak */
    uint64_t bar_max = s.peak_active_bytes > 0 ? s.peak_active_bytes : 1;
    put("  Usage:           ");
    print_bar(s.active_bytes, bar_max);
    lc_print_newline(OUT);

    putln("");

    /* Cumulative counters */
    put("  Total allocs:    ");
    putnum(s.total_allocations);
    lc_print_newline(OUT);

    put("  Total frees:     ");
    putnum(s.total_frees);
    lc_print_newline(OUT);

    len = format_bytes(buf, sizeof(buf), s.total_bytes_allocated);
    put("  Total allocated: ");
    lc_print_string(OUT, buf, len);
    lc_print_newline(OUT);

    len = format_bytes(buf, sizeof(buf), s.total_bytes_freed);
    put("  Total freed:     ");
    lc_print_string(OUT, buf, len);
    lc_print_newline(OUT);

    putln("");

    /* Large allocation info */
    put("  Large allocs:    ");
    putnum(s.large_allocations);
    lc_print_newline(OUT);

    put("  Large cache:     ");
    putnum(s.large_cache_hits);
    put(" hits, ");
    putnum(s.large_cache_count);
    put("/16 entries");
    lc_print_newline(OUT);

    if (s.large_allocations > 0) {
        uint64_t hit_pct = s.large_cache_hits * 100 /
            (s.large_allocations > 0 ? s.large_allocations : 1);
        put("  Cache hit rate:  ");
        print_bar(hit_pct, 100);
        put(" ");
        putnum(hit_pct);
        putln("%");
    }

    /* Slot tracker */
    uint32_t active_slots = 0;
    for (uint32_t i = 0; i < slot_count; i++) {
        if (slots[i] != NULL) active_slots++;
    }
    putln("");
    put("  Tracked slots:   ");
    putnum(active_slots);
    put("/");
    putnum(MAX_SLOTS);
    lc_print_newline(OUT);

    putln("  ============================================");
    putln("");
}

/* --- Command handlers --- */

static void cmd_alloc(const char *arg, size_t arg_len) {
    uint64_t size = parse_uint(arg, arg_len);
    if (size == 0) { putln("  usage: alloc <size>"); return; }

    void *ptr = lc_heap_allocate((size_t)size);
    int32_t id = slot_alloc(ptr, (size_t)size);
    if (id < 0) {
        putln("  error: allocation failed or slots full");
        return;
    }

    put("  [");
    putnum((uint64_t)id);
    put("] allocated ");
    putnum(size);
    put(" bytes at ");
    lc_print_hex(OUT, (uint64_t)(uintptr_t)ptr);
    lc_print_newline(OUT);
}

static void cmd_zalloc(const char *arg, size_t arg_len) {
    uint64_t size = parse_uint(arg, arg_len);
    if (size == 0) { putln("  usage: zalloc <size>"); return; }

    void *ptr = lc_heap_allocate_zeroed((size_t)size);
    int32_t id = slot_alloc(ptr, (size_t)size);
    if (id < 0) {
        putln("  error: allocation failed or slots full");
        return;
    }

    put("  [");
    putnum((uint64_t)id);
    put("] zeroed ");
    putnum(size);
    put(" bytes at ");
    lc_print_hex(OUT, (uint64_t)(uintptr_t)ptr);
    lc_print_newline(OUT);
}

static void cmd_free(const char *arg, size_t arg_len) {
    uint64_t id = parse_uint(arg, arg_len);
    if (id >= slot_count || slots[id] == NULL) {
        putln("  error: invalid slot ID");
        return;
    }

    lc_heap_free(slots[id]);
    put("  [");
    putnum(id);
    put("] freed ");
    putnum(slot_sizes[id]);
    putln(" bytes");
    slots[id] = NULL;
    slot_sizes[id] = 0;
}

static void cmd_realloc(const char *args, size_t args_len) {
    /* Parse "id size" */
    size_t tok_len;
    const char *tok = next_token(args, args_len, &tok_len);
    uint64_t id = parse_uint(tok, tok_len);

    size_t rem = args_len - tok_len;
    const char *rest = skip_space(args + tok_len, &rem);
    uint64_t new_size = parse_uint(rest, rem);

    if (new_size == 0 || id >= slot_count || slots[id] == NULL) {
        putln("  usage: realloc <id> <size>");
        return;
    }

    void *ptr = lc_heap_reallocate(slots[id], (size_t)new_size);
    if (ptr == NULL) {
        putln("  error: realloc failed");
        return;
    }

    slots[id] = ptr;
    size_t old = slot_sizes[id];
    slot_sizes[id] = (size_t)new_size;

    put("  [");
    putnum(id);
    put("] resized ");
    putnum(old);
    put(" -> ");
    putnum(new_size);
    putln(" bytes");
}

static void cmd_fill(const char *args, size_t args_len) {
    size_t tok_len;
    const char *tok = next_token(args, args_len, &tok_len);
    uint64_t count = parse_uint(tok, tok_len);

    size_t rem = args_len - tok_len;
    const char *rest = skip_space(args + tok_len, &rem);
    uint64_t size = parse_uint(rest, rem);

    if (count == 0 || size == 0) {
        putln("  usage: fill <count> <size>");
        return;
    }

    uint32_t ok = 0, fail = 0;
    for (uint64_t i = 0; i < count; i++) {
        void *ptr = lc_heap_allocate((size_t)size);
        if (slot_alloc(ptr, (size_t)size) >= 0) ok++;
        else fail++;
    }

    put("  filled ");
    putnum(ok);
    put(" x ");
    putnum(size);
    put(" bytes");
    if (fail > 0) { put(" ("); putnum(fail); put(" failed)"); }
    lc_print_newline(OUT);
}

static void cmd_drain(void) {
    uint32_t freed = 0;
    for (uint32_t i = 0; i < slot_count; i++) {
        if (slots[i] != NULL) {
            lc_heap_free(slots[i]);
            slots[i] = NULL;
            slot_sizes[i] = 0;
            freed++;
        }
    }
    slot_count = 0;

    put("  drained ");
    putnum(freed);
    putln(" allocations");
}

static void cmd_slots(void) {
    uint32_t active = 0;
    char buf[64];
    for (uint32_t i = 0; i < slot_count; i++) {
        if (slots[i] != NULL) {
            put("  [");
            putnum(i);
            put("] ");
            size_t len = format_bytes(buf, sizeof(buf), slot_sizes[i]);
            lc_print_string(OUT, buf, len);
            put(" at ");
            lc_print_hex(OUT, (uint64_t)(uintptr_t)slots[i]);
            lc_print_newline(OUT);
            active++;
        }
    }
    if (active == 0) putln("  (no active allocations)");
}

static void cmd_help(void) {
    putln("");
    putln("  alloc <size>         allocate <size> bytes");
    putln("  zalloc <size>        allocate zeroed");
    putln("  realloc <id> <size>  resize allocation");
    putln("  free <id>            free by slot ID");
    putln("  fill <n> <size>      batch allocate n x size");
    putln("  drain                free all");
    putln("  stats                show statistics dashboard");
    putln("  reset                reset cumulative counters");
    putln("  slots                list active allocations");
    putln("  help                 show this");
    putln("  quit                 exit");
    putln("");
}

/* --- Main loop --- */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    putln("heapviz - lightc heap allocator visualizer");
    putln("type 'help' for commands\n");

    lc_reader reader = lc_reader_create(0, 1024);  /* stdin */
    char line[256];

    for (;;) {
        put("> ");

        int64_t len = lc_reader_read_line(&reader, line, sizeof(line));
        if (len < 0) break;  /* EOF */
        if (len == 0) continue;

        size_t slen = (size_t)len;
        const char *cmd = skip_space(line, &slen);

        /* Extract command token */
        size_t cmd_len;
        next_token(cmd, slen, &cmd_len);

        /* Argument starts after the command */
        size_t arg_len = slen - cmd_len;
        const char *arg = skip_space(cmd + cmd_len, &arg_len);

        if (lc_string_starts_with(cmd, cmd_len, "quit", 4) ||
            lc_string_starts_with(cmd, cmd_len, "exit", 4)) {
            break;
        } else if (lc_string_equal(cmd, cmd_len, "alloc", 5)) {
            cmd_alloc(arg, arg_len);
        } else if (lc_string_equal(cmd, cmd_len, "zalloc", 6)) {
            cmd_zalloc(arg, arg_len);
        } else if (lc_string_equal(cmd, cmd_len, "realloc", 7)) {
            cmd_realloc(arg, arg_len);
        } else if (lc_string_equal(cmd, cmd_len, "free", 4)) {
            cmd_free(arg, arg_len);
        } else if (lc_string_equal(cmd, cmd_len, "fill", 4)) {
            cmd_fill(arg, arg_len);
        } else if (lc_string_equal(cmd, cmd_len, "drain", 5)) {
            cmd_drain();
        } else if (lc_string_equal(cmd, cmd_len, "stats", 5)) {
            show_stats();
        } else if (lc_string_equal(cmd, cmd_len, "reset", 5)) {
            lc_heap_reset_stats();
            putln("  counters reset");
        } else if (lc_string_equal(cmd, cmd_len, "slots", 5)) {
            cmd_slots();
        } else if (lc_string_equal(cmd, cmd_len, "help", 4)) {
            cmd_help();
        } else if (cmd_len > 0) {
            put("  unknown command: ");
            lc_print_string(OUT, cmd, cmd_len);
            putln(" (try 'help')");
        }
    }

    lc_reader_destroy(&reader);
    putln("bye");
    return 0;
}
