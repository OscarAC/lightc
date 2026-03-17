# lightc

A freestanding C runtime for Linux. No libc, no linker scripts from the system, no external dependencies. Everything from `_start` to `exit` is implemented from scratch using raw Linux syscalls.

Targets x86_64 and aarch64. A tiny hello-world binary is 415 bytes.

## Building

Requires GCC and ninja.

```sh
./configure debug    # -O0, debug symbols, safety checks on
./configure release  # -O2 -DNDEBUG, stripped
./configure tiny     # -Os, minimal binary size

ninja                # build everything
ninja test           # run all tests (171 tests across 10 suites)
ninja bench          # run benchmarks
```

## Modules

### Core (`include/lightc/`)

| Header | Description |
|---|---|
| `syscall.h` | Raw Linux syscall wrappers (inline assembly) |
| `types.h` | `uint8_t`, `size_t`, `bool`, etc. with no `<stdint.h>` |
| `string.h` | String ops (`length`, `find`, `compare`) and byte ops (`copy`, `fill`, `compare`) via `__builtin_*` |
| `memory.h` | Arena allocator (bump allocation, reset, inline fast path) and raw page allocation |
| `heap.h` | General-purpose heap allocator with bucket allocator, large block cache, per-thread pages, optional statistics |
| `format.h` | Type-safe format builder (no format strings, no varargs) |
| `print.h` | Direct-to-fd printing (char, string, integer, hex) |
| `io.h` | Buffered reader/writer, file utilities, directory listing |
| `thread.h` | Threads via `clone` syscall, spinlocks |
| `coroutine.h` | Stackful coroutines with cooperative scheduling (~5ns context switch) |
| `async.h` | Async I/O via `io_uring` |
| `socket.h` | TCP/UDP sockets, IPv4 and IPv6 |
| `time.h` | Monotonic clock, wall clock, date/time conversion, formatting |
| `log.h` | Leveled logging with plain text and JSON output |
| `signal.h` | Signal handling (portable across x86_64/aarch64) |
| `lifecycle.h` | `atexit` handler chain with SIGTERM integration |
| `library.h` | Runtime shared library loading (ELF parser) |

### Data structures (`include/lightdata/`)

| Header | Description |
|---|---|
| `array.h` | Dynamic array (push, pop, insert, remove, sort) |
| `hashmap.h` | Open-addressing hash map (string keys) |
| `ringbuf.h` | Fixed-capacity ring buffer |
| `list.h` | Intrusive doubly-linked list |

### Heap allocator

The heap uses a segment/page architecture with thread-local ownership:

- **Small allocations** (<=4096 bytes): bitmap-based bucket allocator across 29 size classes with ~1.25x geometric spacing. Each thread owns pages exclusively -- allocation and same-thread free require no locks. Cross-thread free uses a lock-free atomic push to a per-page remote free list.
- **Large allocations** (>4096 bytes): direct `mmap`/`munmap` with a 16-entry LRU cache to avoid repeated syscalls.
- **Debug mode** (`LC_HEAP_DEBUG`): double-free detection via magic validation, use-after-free detection via 0xCC poison fill.
- **Statistics** (`LC_STATS`): optional per-allocation tracking with atomic counters (allocation counts, active bytes, peaks, large cache hit rate).

### Coroutines

Stackful coroutines with per-coroutine `mmap`'d stacks and a guard page at the bottom. Context switching saves/restores only callee-saved registers (7 on x86_64, 13 on aarch64) via hand-written assembly. A yield between two coroutines takes ~5ns.

## Build modes and compile-time flags

| Flag | Default (debug) | Default (release) | Effect |
|---|---|---|---|
| `LC_HEAP_DEBUG` | 1 | 0 | Double-free detection, use-after-free poisoning |
| `LC_STATS` | 1 | 0 | Heap/arena allocation statistics tracking |
| `NDEBUG` | not set | set | Controls defaults of the above |

Override any flag with `-DLC_STATS=1` or `-DLC_STATS=0` at compile time.


