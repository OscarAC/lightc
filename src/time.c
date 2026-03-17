/*
 * Date and time — calendar math + clock_gettime syscall.
 */
#include <lightc/time.h>
#include <lightc/syscall.h>
#include <lightc/format.h>

/* ------------------------------------------------------------------ */
/* Internal: civil date algorithms (Howard Hinnant)                   */
/* ------------------------------------------------------------------ */

/*
 * Convert days since 1970-01-01 to year/month/day.
 * Handles all dates from year 0 to year 9999+.
 */
static void days_to_date(int64_t days, int32_t *year, int32_t *month, int32_t *day) {
    /* Shift to March epoch (March 1, 0000) */
    days += 719468;  /* days from 0000-03-01 to 1970-01-01 */

    int64_t era = (days >= 0 ? days : days - 146096) / 146097;  /* 400-year era */
    int64_t doe = days - era * 146097;                           /* day of era [0, 146096] */
    int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  /* year of era */
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);            /* day of year [0, 365] */
    int64_t mp = (5*doy + 2) / 153;                              /* month [0, 11] from March */
    int64_t d = doy - (153*mp + 2) / 5 + 1;                     /* day [1, 31] */
    int64_t m = mp + (mp < 10 ? 3 : -9);                        /* month [1, 12] */

    *year  = (int32_t)(y + (m <= 2));
    *month = (int32_t)m;
    *day   = (int32_t)d;
}

/*
 * Convert year/month/day to days since 1970-01-01.
 * Inverse of days_to_date.
 */
static int64_t date_to_days(int32_t year, int32_t month, int32_t day) {
    int64_t y = year;
    int64_t m = month;
    if (m <= 2) { y--; m += 9; } else { m -= 3; }

    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * m + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe/4 - yoe/100 + doy;
    return era * 146097 + doe - 719468;
}

/*
 * Is this a leap year?
 */
static bool is_leap_year(int32_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/*
 * Compute day-of-year (0 = January 1).
 */
static int32_t compute_day_of_year(int32_t year, int32_t month, int32_t day) {
    static const int32_t cumulative_days[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    int32_t doy = cumulative_days[month - 1] + day - 1;
    if (month > 2 && is_leap_year(year)) {
        doy++;
    }
    return doy;
}

/* ------------------------------------------------------------------ */
/* Getting the current time                                           */
/* ------------------------------------------------------------------ */

lc_date_time lc_time_now(void) {
    lc_timespec ts;
    if (lc_kernel_get_clock(LC_CLOCK_REALTIME, &ts) < 0) {
        lc_date_time zero = {0};
        return zero;
    }
    return lc_time_from_unix(ts.seconds);
}

int64_t lc_time_now_unix(void) {
    lc_timespec ts;
    if (lc_kernel_get_clock(LC_CLOCK_REALTIME, &ts) < 0) {
        return 0;
    }
    return ts.seconds;
}

int64_t lc_time_now_monotonic(void) {
    lc_timespec ts;
    if (lc_kernel_get_clock(LC_CLOCK_MONOTONIC, &ts) < 0) {
        return 0;
    }
    return ts.seconds * 1000000000LL + ts.nanoseconds;
}

/* ------------------------------------------------------------------ */
/* Conversion                                                         */
/* ------------------------------------------------------------------ */

lc_date_time lc_time_from_unix(int64_t epoch_seconds) {
    lc_date_time dt;

    int64_t days = epoch_seconds / 86400;
    int64_t rem  = epoch_seconds % 86400;

    /* Handle negative remainder (dates before 1970) */
    if (rem < 0) {
        rem += 86400;
        days--;
    }

    dt.hour   = (int32_t)(rem / 3600);
    dt.minute = (int32_t)((rem % 3600) / 60);
    dt.second = (int32_t)(rem % 60);

    /* Day of week: Jan 1, 1970 was Thursday (4) */
    int64_t dow = (days % 7 + 4) % 7;
    if (dow < 0) dow += 7;
    dt.day_of_week = (int32_t)dow;

    days_to_date(days, &dt.year, &dt.month, &dt.day);
    dt.day_of_year = compute_day_of_year(dt.year, dt.month, dt.day);

    return dt;
}

int64_t lc_time_to_unix(const lc_date_time *dt) {
    int64_t days = date_to_days(dt->year, dt->month, dt->day);
    return days * 86400 + dt->hour * 3600 + dt->minute * 60 + dt->second;
}

/* ------------------------------------------------------------------ */
/* Measuring elapsed time                                             */
/* ------------------------------------------------------------------ */

int64_t lc_time_start_timer(void) {
    return lc_time_now_monotonic();
}

int64_t lc_time_elapsed_nanoseconds(int64_t start) {
    return lc_time_now_monotonic() - start;
}

int64_t lc_time_elapsed_microseconds(int64_t start) {
    return (lc_time_now_monotonic() - start) / 1000;
}

int64_t lc_time_elapsed_milliseconds(int64_t start) {
    return (lc_time_now_monotonic() - start) / 1000000;
}

/* ------------------------------------------------------------------ */
/* Formatting                                                         */
/* ------------------------------------------------------------------ */

size_t lc_time_format(const lc_date_time *dt, char *buf, size_t buf_size) {
    lc_format fmt = lc_format_start(buf, buf_size);
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->year, 4, '0');
    lc_format_add_char(&fmt, '-');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->month, 2, '0');
    lc_format_add_char(&fmt, '-');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->day, 2, '0');
    lc_format_add_char(&fmt, ' ');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->hour, 2, '0');
    lc_format_add_char(&fmt, ':');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->minute, 2, '0');
    lc_format_add_char(&fmt, ':');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->second, 2, '0');
    return lc_format_finish(&fmt);
}

size_t lc_time_format_date(const lc_date_time *dt, char *buf, size_t buf_size) {
    lc_format fmt = lc_format_start(buf, buf_size);
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->year, 4, '0');
    lc_format_add_char(&fmt, '-');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->month, 2, '0');
    lc_format_add_char(&fmt, '-');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->day, 2, '0');
    return lc_format_finish(&fmt);
}

size_t lc_time_format_time(const lc_date_time *dt, char *buf, size_t buf_size) {
    lc_format fmt = lc_format_start(buf, buf_size);
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->hour, 2, '0');
    lc_format_add_char(&fmt, ':');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->minute, 2, '0');
    lc_format_add_char(&fmt, ':');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->second, 2, '0');
    return lc_format_finish(&fmt);
}

size_t lc_time_format_iso8601(const lc_date_time *dt, char *buf, size_t buf_size) {
    lc_format fmt = lc_format_start(buf, buf_size);
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->year, 4, '0');
    lc_format_add_char(&fmt, '-');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->month, 2, '0');
    lc_format_add_char(&fmt, '-');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->day, 2, '0');
    lc_format_add_char(&fmt, 'T');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->hour, 2, '0');
    lc_format_add_char(&fmt, ':');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->minute, 2, '0');
    lc_format_add_char(&fmt, ':');
    lc_format_add_unsigned_padded(&fmt, (uint64_t)dt->second, 2, '0');
    lc_format_add_char(&fmt, 'Z');
    return lc_format_finish(&fmt);
}

/* ------------------------------------------------------------------ */
/* Sleep                                                              */
/* ------------------------------------------------------------------ */

void lc_time_sleep_milliseconds(int64_t ms) {
    lc_timespec ts;
    ts.seconds     = ms / 1000;
    ts.nanoseconds = (ms % 1000) * 1000000;

    /* Retry if interrupted by a signal (EINTR = -4) */
    while (lc_kernel_sleep(&ts) == -4) {
        /* ts is not modified by our wrapper, but the kernel would update
         * the remaining time in the second argument. We pass 0 for that,
         * so just retry with the original duration. For short sleeps
         * this is fine — worst case we sleep slightly longer. */
    }
}

void lc_time_sleep_microseconds(int64_t us) {
    lc_timespec ts;
    ts.seconds     = us / 1000000;
    ts.nanoseconds = (us % 1000000) * 1000;

    while (lc_kernel_sleep(&ts) == -4) {
        /* retry on EINTR */
    }
}
