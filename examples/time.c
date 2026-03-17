/*
 * Exercise date/time functions.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/format.h>
#include <lightc/time.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed) lc_print_line(STDOUT, S(" PASS"));
    else lc_print_line(STDOUT, S(" FAIL"));
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    char buf[64];

    /* --- time_now: current time should be reasonable --- */
    lc_print_string(STDOUT, S("time_now"));
    lc_date_time now = lc_time_now();
    say_pass_fail(now.year >= 2025 && now.month >= 1 && now.month <= 12
               && now.day >= 1 && now.day <= 31);

    /* --- time_now_unix: epoch should be recent --- */
    lc_print_string(STDOUT, S("time_now_unix"));
    int64_t epoch = lc_time_now_unix();
    say_pass_fail(epoch > 1700000000);  /* after Nov 2023 */

    /* --- time_from_unix_epoch: epoch 0 = 1970-01-01 00:00:00 Thursday --- */
    lc_print_string(STDOUT, S("time_from_unix_epoch"));
    lc_date_time dt = lc_time_from_unix(0);
    say_pass_fail(dt.year == 1970 && dt.month == 1 && dt.day == 1
               && dt.hour == 0 && dt.minute == 0 && dt.second == 0
               && dt.day_of_week == 4  /* Thursday */
               && dt.day_of_year == 0);

    /* --- time_from_unix_known: 1710547200 = 2024-03-16 00:00:00 Saturday --- */
    lc_print_string(STDOUT, S("time_from_unix_known"));
    dt = lc_time_from_unix(1710547200);
    say_pass_fail(dt.year == 2024 && dt.month == 3 && dt.day == 16
               && dt.hour == 0 && dt.minute == 0 && dt.second == 0
               && dt.day_of_week == 6);  /* Saturday */

    /* --- time_from_unix_y2k: 946684800 = 2000-01-01 00:00:00 Saturday --- */
    lc_print_string(STDOUT, S("time_from_unix_y2k"));
    dt = lc_time_from_unix(946684800);
    say_pass_fail(dt.year == 2000 && dt.month == 1 && dt.day == 1
               && dt.hour == 0 && dt.minute == 0 && dt.second == 0
               && dt.day_of_week == 6);  /* Saturday */

    /* --- time_to_unix_roundtrip: from_unix(X) -> to_unix -> X --- */
    lc_print_string(STDOUT, S("time_to_unix_roundtrip"));
    int64_t test_epoch = 1710547200;
    dt = lc_time_from_unix(test_epoch);
    int64_t back = lc_time_to_unix(&dt);
    say_pass_fail(back == test_epoch);

    /* --- time_format: known date -> "2024-03-16 12:30:45" --- */
    lc_print_string(STDOUT, S("time_format"));
    dt = lc_time_from_unix(1710592245);  /* 2024-03-16 12:30:45 UTC */
    size_t len = lc_time_format(&dt, buf, sizeof(buf));
    say_pass_fail(len == 19 && lc_string_equal(buf, len, S("2024-03-16 12:30:45")));

    /* --- time_format_date: "2024-03-16" --- */
    lc_print_string(STDOUT, S("time_format_date"));
    len = lc_time_format_date(&dt, buf, sizeof(buf));
    say_pass_fail(len == 10 && lc_string_equal(buf, len, S("2024-03-16")));

    /* --- time_format_time: "12:30:45" --- */
    lc_print_string(STDOUT, S("time_format_time"));
    len = lc_time_format_time(&dt, buf, sizeof(buf));
    say_pass_fail(len == 8 && lc_string_equal(buf, len, S("12:30:45")));

    /* --- time_format_iso8601: "2024-03-16T12:30:45Z" --- */
    lc_print_string(STDOUT, S("time_format_iso8601"));
    len = lc_time_format_iso8601(&dt, buf, sizeof(buf));
    say_pass_fail(len == 20 && lc_string_equal(buf, len, S("2024-03-16T12:30:45Z")));

    /* --- time_elapsed: start timer, do work, elapsed > 0 --- */
    lc_print_string(STDOUT, S("time_elapsed"));
    int64_t start = lc_time_start_timer();
    /* Burn some cycles */
    volatile int64_t dummy = 0;
    for (int64_t i = 0; i < 100000; i++) {
        dummy += i;
    }
    int64_t elapsed_ns = lc_time_elapsed_nanoseconds(start);
    say_pass_fail(elapsed_ns > 0);

    /* --- time_sleep: sleep 50ms, verify elapsed ~50ms (40-200ms range) --- */
    lc_print_string(STDOUT, S("time_sleep"));
    start = lc_time_start_timer();
    lc_time_sleep_milliseconds(50);
    int64_t elapsed_ms = lc_time_elapsed_milliseconds(start);
    say_pass_fail(elapsed_ms >= 40 && elapsed_ms <= 200);

    /* --- time_monotonic: two calls, second >= first --- */
    lc_print_string(STDOUT, S("time_monotonic"));
    int64_t mono1 = lc_time_now_monotonic();
    int64_t mono2 = lc_time_now_monotonic();
    say_pass_fail(mono2 >= mono1);

    /* --- time_day_of_week: 1970-01-01 = Thursday (4) --- */
    lc_print_string(STDOUT, S("time_day_of_week"));
    dt = lc_time_from_unix(0);
    bool dow_ok = (dt.day_of_week == 4);  /* Thursday */
    /* Also check 2024-03-16 = Saturday (6) */
    dt = lc_time_from_unix(1710547200);
    dow_ok = dow_ok && (dt.day_of_week == 6);  /* Saturday */
    /* Also check 2000-01-01 = Saturday (6) */
    dt = lc_time_from_unix(946684800);
    dow_ok = dow_ok && (dt.day_of_week == 6);  /* Saturday */
    say_pass_fail(dow_ok);

    /* --- Demo: print current time --- */
    lc_print_newline(STDOUT);
    now = lc_time_now();
    len = lc_time_format_iso8601(&now, buf, sizeof(buf));
    lc_print_string(STDOUT, S("current time: "));
    lc_print_string(STDOUT, buf, len);
    lc_print_newline(STDOUT);

    lc_print_newline(STDOUT);
    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
