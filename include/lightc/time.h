#ifndef LIGHTC_TIME_H
#define LIGHTC_TIME_H

#include "types.h"

/*
 * Date and time — built on clock_gettime syscall.
 *
 * All times are UTC. No timezone support (timezones are a libc mess).
 */

/* Broken-down date and time (UTC) */
typedef struct {
    int32_t year;        /* e.g., 2026 */
    int32_t month;       /* 1-12 */
    int32_t day;         /* 1-31 */
    int32_t hour;        /* 0-23 */
    int32_t minute;      /* 0-59 */
    int32_t second;      /* 0-59 */
    int32_t day_of_week; /* 0=Sunday, 6=Saturday */
    int32_t day_of_year; /* 0-365 */
} lc_date_time;

/* Duration in seconds + nanoseconds */
typedef struct {
    int64_t seconds;
    int64_t nanoseconds;
} lc_duration;

/* --- Getting the current time --- */

/* Get current UTC date and time. */
lc_date_time lc_time_now(void);

/* Get seconds since Unix epoch (Jan 1, 1970). */
int64_t lc_time_now_unix(void);

/* Get a monotonic timestamp in nanoseconds (for measuring elapsed time). */
int64_t lc_time_now_monotonic(void);

/* --- Conversion --- */

/* Convert Unix epoch seconds to broken-down date/time (UTC). */
[[gnu::const]]
lc_date_time lc_time_from_unix(int64_t epoch_seconds);

/* Convert broken-down date/time to Unix epoch seconds. */
[[gnu::pure, gnu::nonnull(1)]]
int64_t lc_time_to_unix(const lc_date_time *dt);

/* --- Measuring elapsed time --- */

/* Start a timer (returns monotonic timestamp). */
int64_t lc_time_start_timer(void);

/* Get elapsed time since a timer start, in nanoseconds. */
int64_t lc_time_elapsed_nanoseconds(int64_t start);

/* Get elapsed time in microseconds. */
int64_t lc_time_elapsed_microseconds(int64_t start);

/* Get elapsed time in milliseconds. */
int64_t lc_time_elapsed_milliseconds(int64_t start);

/* --- Formatting --- */

/* Format date/time as "YYYY-MM-DD HH:MM:SS" into buf. Returns length written. */
[[gnu::nonnull(1, 2)]]
size_t lc_time_format(const lc_date_time *dt, char *buf, size_t buf_size);

/* Format date only as "YYYY-MM-DD". Returns length written. */
[[gnu::nonnull(1, 2)]]
size_t lc_time_format_date(const lc_date_time *dt, char *buf, size_t buf_size);

/* Format time only as "HH:MM:SS". Returns length written. */
[[gnu::nonnull(1, 2)]]
size_t lc_time_format_time(const lc_date_time *dt, char *buf, size_t buf_size);

/* Format as ISO 8601: "2026-03-15T21:30:00Z". Returns length written. */
[[gnu::nonnull(1, 2)]]
size_t lc_time_format_iso8601(const lc_date_time *dt, char *buf, size_t buf_size);

/* --- Sleep --- */

/* Sleep for the given number of milliseconds. */
void lc_time_sleep_milliseconds(int64_t ms);

/* Sleep for the given number of microseconds. */
void lc_time_sleep_microseconds(int64_t us);

#endif /* LIGHTC_TIME_H */
