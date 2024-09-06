/*
 * Copyright (C) 2019 Santiago León O.
 */

// These are utility functions to properly handle date and time information.
//
// As a format for date and time we will use sequences of ASCII characters
// somewhat based on what XML does, based on ISO 8601.
//
// In general we will have to always be careful about compatibility of date
// formats if we ever decide to export to real XML or JSON.
// 
// At the moment no compatibility guarantees are made about these functions
// with respect to other libraries or standards. First we want to guearantee
// we are consistent with ourselves.

#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif

#include <time.h>

#define DATE_TIMESTAMP_MAX_LEN 30

#define REFERENCE_TIME_DURATIONS_TABLE \
    REFERENCE_TIME_DURATIONS_ROW(D_YEAR,   "year",   12) \
    REFERENCE_TIME_DURATIONS_ROW(D_MONTH,  "month",  15) \
    REFERENCE_TIME_DURATIONS_ROW(D_DAY,    "day",    18) \
    REFERENCE_TIME_DURATIONS_ROW(D_HOUR,   "hour",   20) \
    REFERENCE_TIME_DURATIONS_ROW(D_MINUTE, "minute", 23) \
    REFERENCE_TIME_DURATIONS_ROW(D_SECOND, "second", DATE_TIMESTAMP_MAX_LEN) \

#define REFERENCE_TIME_DURATIONS_ROW(enum_name, string_name, date_str_max_len) enum_name,
enum reference_time_duration_t {
    REFERENCE_TIME_DURATIONS_TABLE

    D_REFERENCE_TIME_DURATION_LEN
};
#undef REFERENCE_TIME_DURATIONS_ROW

#define REFERENCE_TIME_DURATIONS_ROW(enum_name, string_name, date_str_max_len) string_name,
char* reference_time_duration_names[] = {
    REFERENCE_TIME_DURATIONS_TABLE
};
#undef REFERENCE_TIME_DURATIONS_ROW

#define REFERENCE_TIME_DURATIONS_ROW(enum_name, string_name, date_str_max_len) date_str_max_len,
size_t date_max_len[] = {
    REFERENCE_TIME_DURATIONS_TABLE
};
#undef REFERENCE_TIME_DURATIONS_ROW


#define DAYS_OF_WEEK_TABLE \
    DAYS_OF_WEEK_ROW(D_SUNDAY,    "Sunday")    \
    DAYS_OF_WEEK_ROW(D_MONDAY,    "Monday")    \
    DAYS_OF_WEEK_ROW(D_TUESDAY,   "Tuesday")   \
    DAYS_OF_WEEK_ROW(D_WEDNESDAY, "Wednesday") \
    DAYS_OF_WEEK_ROW(D_THURSDAY,  "Thursday")  \
    DAYS_OF_WEEK_ROW(D_FRIDAY,    "Friday")    \
    DAYS_OF_WEEK_ROW(D_SATURDAY,  "Saturday")  \

#define DAYS_OF_WEEK_ROW(enum_name, string_name) string_name,
char* day_names[] = {
    DAYS_OF_WEEK_TABLE
};
#undef DAYS_OF_WEEK_ROW

#define DAYS_OF_WEEK_ROW(enum_name, string_name) enum_name,
enum day_of_week_t {
    DAYS_OF_WEEK_TABLE
};
#undef DAYS_OF_WEEK_ROW


char *month_names[] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

struct date_element_t {
    union {
        struct {
            int32_t year;
            int32_t month; // Starts at 1 for January
            int32_t day; // Starts at 1
            int32_t hour;
            int32_t minute;
            int32_t second;
        };
        int32_t v[6];
    };
};

// Is endianess going to affect the order of these? They need to match so that
// date->year == date->v[D_YEAR]
struct date_t {
    union {
        struct {
            int32_t year;
            int32_t month;
            int32_t day;
            int32_t hour;
            int32_t minute;
            int32_t second;
        };
        int32_t v[6];
    };

    double second_fraction;

    bool is_set_utc_offset;
    int32_t utc_offset_hour;
    int32_t utc_offset_minute;
};

#define LEAP_SECOND_TABLE \
    LEAP_SECOND_ROW (1972, 1, 1) \
    LEAP_SECOND_ROW (1973, 0, 1) \
    LEAP_SECOND_ROW (1974, 0, 1) \
    LEAP_SECOND_ROW (1975, 0, 1) \
    LEAP_SECOND_ROW (1976, 0, 1) \
    LEAP_SECOND_ROW (1977, 0, 1) \
    LEAP_SECOND_ROW (1978, 0, 1) \
    LEAP_SECOND_ROW (1979, 0, 1) \
    LEAP_SECOND_ROW (1981, 1, 0) \
    LEAP_SECOND_ROW (1982, 1, 0) \
    LEAP_SECOND_ROW (1983, 1, 0) \
    LEAP_SECOND_ROW (1985, 1, 0) \
    LEAP_SECOND_ROW (1987, 0, 1) \
    LEAP_SECOND_ROW (1989, 0, 1) \
    LEAP_SECOND_ROW (1990, 0, 1) \
    LEAP_SECOND_ROW (1992, 1, 0) \
    LEAP_SECOND_ROW (1993, 1, 0) \
    LEAP_SECOND_ROW (1994, 1, 0) \
    LEAP_SECOND_ROW (1995, 0, 1) \
    LEAP_SECOND_ROW (1997, 1, 0) \
    LEAP_SECOND_ROW (1998, 0, 1) \
    LEAP_SECOND_ROW (2005, 0, 1) \
    LEAP_SECOND_ROW (2008, 0, 1) \
    LEAP_SECOND_ROW (2012, 1, 0) \
    LEAP_SECOND_ROW (2015, 1, 0) \
    LEAP_SECOND_ROW (2016, 0, 1) \


#define LEAP_SECOND_ROW(year,june_value, december_value) case year: return june_value;
int june_leap_second (int year)
{
    switch (year) {
        LEAP_SECOND_TABLE
        default: return 0;
    }
}
#undef LEAP_SECOND_ROW

#define LEAP_SECOND_ROW(year,june_value, december_value) case year: return december_value;
int december_leap_second (int year)
{
    switch (year) {
        LEAP_SECOND_TABLE
        default: return 0;
    }
}
#undef LEAP_SECOND_ROW

#define DATE(year, month, day, hour, minute, second, second_fraction, is_set_utc_offset, utc_offset_hour, utc_offset_minute) \
    ((struct date_t){{{year, month, day, hour, minute, second}}, second_fraction, is_set_utc_offset, utc_offset_hour, utc_offset_minute})

#define DATE_DAY(year, month, day) \
    (DATE(year, month, day, -1, -1, -1, 0.0, false, 0, 0))
#define DATE_MONTH(year, month) \
    (DATE(year, month, -1, -1, -1, -1, 0.0, false, 0, 0))
#define DATE_YEAR(year) \
    (DATE(year, -1, -1, -1, -1, -1, 0.0, false, 0, 0))

#define DATE_P(year, month, day, hour, minute, second, second_fraction, is_set_utc_offset, utc_offset_hour, utc_offset_minute) \
    &((struct date_t){{{year, month, day, hour, minute, second}}, second_fraction, is_set_utc_offset, utc_offset_hour, utc_offset_minute})

#define DATE_ELEMENT(year, month, day, hour, minute, second) \
    ((struct date_element_t){{{year, month, day, hour, minute, second}}})

#define DATE_ELEMENT_DAY(year, month, day) \
    (DATE_ELEMENT(year, month, day, -1, -1, -1))
#define DATE_ELEMENT_MONTH(year, month) \
    (DATE_ELEMENT(year, month, -1, -1, -1, -1))
#define DATE_ELEMENT_YEAR(year) \
    (DATE_ELEMENT(year, -1, -1, -1, -1, -1))

#define DATE_ELEMENT_P(year, month, day, hour, minute, second) \
    &((struct date_element_t){{{year, month, day, hour, minute, second}}})

void date_get_value_range (enum reference_time_duration_t precision, struct date_t *date, int *min, int *max);

// NOTE: Assumes the added values don't exceed the maximum range for the passed
// duration. For instance, adding 120 minutes instead of 2 hours will result in
// an error.
bool date_add_value_restricted (struct date_t *date, int value, enum reference_time_duration_t duration)
{
    bool success = true;

    struct date_t result = *date;
    int min, max;

    struct date_t relative = {0};
    enum reference_time_duration_t curr_duration = duration;
    while (curr_duration != D_YEAR && value != 0) {
        date_get_value_range (curr_duration, date, &min, &max);
        int new_val = date->v[curr_duration] + value;
        if (new_val > max) {
            int remainder = new_val - max;

            relative.v[curr_duration] = remainder;
            value = 1;

        } else if (new_val < min) {
            int remainder = min - new_val;

            relative.v[curr_duration] = -remainder;
            value = -1;

        } else {
            result.v[curr_duration] = date->v[curr_duration] + value;
            relative.v[curr_duration] = 0;
            value = 0;
        }

        curr_duration--;
    }
    result.year = date->year + value;

    curr_duration = D_MONTH;
    while (success && curr_duration <= duration) {
        date_get_value_range (curr_duration, &result, &min, &max);
        if (relative.v[curr_duration] > 0) {
            result.v[curr_duration] = min + relative.v[curr_duration] - 1;

        } else if (relative.v[curr_duration] < 0) {
            result.v[curr_duration] = max + relative.v[curr_duration] + 1;
        }

        if (result.v[curr_duration] < min || result.v[curr_duration] > max) {
            success = false;
        }

        curr_duration++;
    }

    if (success) {
        *date = result;
    }

    return success;
}

void date_add_value (struct date_t *date, int value, enum reference_time_duration_t duration)
{
    // TODO: Use date_add_value_restricted() to compute unrestricted values.
    // :unrestricted_date_addition
    date_add_value_restricted (date, value, duration);
}

void date_get_now_d (struct date_t *date)
{
    assert (date != NULL);

    time_t t = time(NULL);
    struct tm local_time = {0};
    // TODO: Instead of calling localtime_r() use date_add_value() once these
    // kinds of operations are implemented and are fast. This would remove a
    // dependency on an external library.
    tzset();
    if (!localtime_r (&t, &local_time)) {
        // We will assume this never happens.
        invalid_code_path;
    }

    date->year = 1900 + local_time.tm_year;
    date->month = 1 + local_time.tm_mon;
    date->day = local_time.tm_mday;
    date->hour = local_time.tm_hour;
    date->minute = local_time.tm_min;
    date->second = local_time.tm_sec;

    date->is_set_utc_offset = true;
    date->utc_offset_hour = local_time.tm_gmtoff/3600;
    date->utc_offset_minute = local_time.tm_gmtoff%3600;
}

void date_to_utc (struct date_t *date, struct date_t *res)
{
    assert (date != NULL && res != NULL);

    if (!date->is_set_utc_offset) return;

    *res = *date;

    res->is_set_utc_offset = true;
    res->utc_offset_hour = 0;
    res->utc_offset_minute = 0;

    date_add_value (res, -date->utc_offset_hour, D_HOUR);
    date_add_value (res, -SIGN(date->utc_offset_hour)*date->utc_offset_minute, D_MINUTE);

    res->utc_offset_hour = 0;
    res->utc_offset_minute = 0;
}

void date_get_value_range (enum reference_time_duration_t precision, struct date_t *date, int *min, int *max)
{
    int min_l = 0, max_l = 0;
    if (precision == D_YEAR) {
        min_l = 1582;
        max_l = INT32_MAX;

    } else if (precision == D_MONTH) {
        min_l = 1;
        max_l = 12;

    } else if (precision == D_DAY) {
        min_l = 1;

        int year = date->year;
        int month = date->month;
        assert (month >= 1 && month <=12);

        if (month == 4 || month == 6 || month == 9 || month == 11) {
            max_l = 30;

        } else if (month == 2) {
            if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
                max_l = 29;

            } else {
                max_l = 28;
            }

        } else {
            max_l = 31;
        }

    } else if (precision == D_HOUR) {
        min_l = 0;
        max_l = 23;

    } else if (precision == D_MINUTE) {
        min_l = 0;
        max_l = 59;

    } else if (precision == D_SECOND) {
        min_l = 0;

        max_l = 59;
        struct date_t _utc_date = {0};
        struct date_t *utc_date = &_utc_date;

        // If the UTC offset is unknown, assume it's 00:00, this way
        // computations that involve durations of time work fine, even in
        // the absence of the UTC offset. For example: 
        //
        //  - Compute the length of a day in seconds
        //  - Compute the amount of seconds between 2 dates
        // Store UTC offset
        bool was_is_set_utc_offset = date->is_set_utc_offset; int h = date->utc_offset_hour; int m = date->utc_offset_minute;
        if (!was_is_set_utc_offset) {
            date->is_set_utc_offset = true; date->utc_offset_hour = 0; date->utc_offset_minute = 0;
        }

        date_to_utc (date, utc_date);

        // Restore UTC offset
        date->is_set_utc_offset = was_is_set_utc_offset; date->utc_offset_hour = h; date->utc_offset_minute = m;

        int leap_second_value = 0;
        if (utc_date->day == 30 && utc_date->month == 6)  {
            leap_second_value = june_leap_second (utc_date->year);

        } else if (utc_date->day == 31 && utc_date->month == 12) {
            leap_second_value = december_leap_second (utc_date->year);
        }

        if (leap_second_value != 0 && utc_date->minute == 59 && utc_date->hour == 23) {
            max_l += leap_second_value;
        }
    }

    if (min != NULL) *min = min_l;
    if (max != NULL) *max = max_l;
}

bool date_is_valid_d (struct date_t *date, string_t *error)
{
    bool is_valid = true;

    enum reference_time_duration_t curr_duration = D_YEAR;
    while (is_valid && curr_duration < D_REFERENCE_TIME_DURATION_LEN && date->v[curr_duration] != -1) {
        int min, max;
        date_get_value_range (curr_duration, date, &min, &max);
        if (date->v[curr_duration] < min || date->v[curr_duration] > max) {
            is_valid = false;
        }

        if (!is_valid && error != NULL) {
            if (curr_duration == D_YEAR) {
                // TODO: Maybe this should only be a warning? technically we can
                // extend the calendar system arbitrarily back into the past, it
                // just wouldn't match historical dates found in records.
                str_cat_printf (error, "%d is not a valid year. Only the Gregorian calendar is supported. Dates before the Gregorian reform established on 15 October 1582 are most likely wrong.", date->v[curr_duration]);

            } else if (curr_duration == D_DAY) {
                if (date->day <= 31) {
                    str_cat_printf (error, "Day %d is not valid for %s.", date->day, month_names[date->month-1]);
                    if (date->day == 29) {
                        str_cat_printf (error, " %d isn't a leap year.", date->year);
                    }

                } else {
                    str_cat_printf (error, "%d is not a valid day.", date->day);
                }

            } else {
                str_cat_printf (error, "%d is not a valid %s.", date->v[curr_duration], reference_time_duration_names[curr_duration]);
            }
        }

        curr_duration++;
    }

    if (date->is_set_utc_offset) {
        if (date->utc_offset_hour < -23 || date->utc_offset_hour > 23) {
            is_valid = false;
            str_cat_printf (error, "%d is not a valid UTC offset hour.", ABS(date->utc_offset_hour));
        }

        if (date->utc_offset_minute < 0 || date->utc_offset_minute > 59) {
            is_valid = false;
            str_cat_printf (error, "%d is not a valid UTC offset minute.", date->utc_offset_minute);
        }
    }

    return is_valid;
}

int date_cmp (struct date_t *d1, struct date_t *d2)
{
    assert (d1 != NULL && d2 != NULL);

    struct date_t d1_l, d2_l;
    if (d1->is_set_utc_offset && d2->is_set_utc_offset &&
        (d1->utc_offset_hour != d2->utc_offset_hour || d1->utc_offset_minute != d2->utc_offset_minute) ) {
        date_to_utc (d1, &d1_l);
        d1 = &d1_l;

        date_to_utc (d2, &d2_l);
        d2 = &d2_l;
    }

    int diff = d1->year - d2->year;

    if (diff == 0) {
        diff = d1->month - d2->month;
    }

    if (diff == 0) {
        diff = d1->day - d2->day;
    }

    if (diff == 0) {
        diff = d1->hour - d2->hour;
    }

    if (diff == 0) {
        diff = d1->minute - d2->minute;
    }

    if (diff == 0) {
        diff = d1->second - d2->second;
    }

    if (diff == 0) {
        double ddiff = d1->second_fraction - d2->second_fraction;
        if (ddiff != 0) {
            diff = ddiff < 0 ? -1 : 1;
        }
    }

    if (diff == 0 && (d1->is_set_utc_offset || d2->is_set_utc_offset))  {
        if (d1->is_set_utc_offset == d2->is_set_utc_offset) {
            if (diff == 0) {
                diff = d1->utc_offset_hour - d2->utc_offset_hour;
            }

            if (diff == 0) {
                diff = d1->utc_offset_minute - d2->utc_offset_minute;
            }

        } else {
            diff = -1;
        }
    }

    return diff;
}

struct date_scanner_t {
    char *pos;
    bool is_eof;
    int len;

    bool error;
    char *error_message;
};

void date_scanner_set_error (struct date_scanner_t *scnr, char *error_message)
{
    // Only set this the first time the function is called. Knowing the first
    // error message is more useful than the last.
    if (!scnr->error) {
        scnr->error = true;
        scnr->error_message = error_message;
    }
}

void date_scanner_parse_error (struct date_scanner_t *scnr)
{
    date_scanner_set_error (scnr, "Date has invalid format.");
}

bool date_scanner_int (struct date_scanner_t *scnr, int32_t *value)
{
    assert (value != NULL);
    if (scnr->error) return false;

    if (!isdigit (*scnr->pos)) return false;

    char *start = scnr->pos;
    char *end;
    int res = strtol (scnr->pos, &end, 10);
    if (scnr->pos != end) {
        *value = res;
        scnr->pos = end;

        if (*scnr->pos == '\0') {
            scnr->is_eof = true;
        }

        scnr->len = end - start;
        return true;
    }

    return false;
}

bool date_scanner_double (struct date_scanner_t *scnr, double *value)
{
    assert (value != NULL);
    if (scnr->error) return false;

    char *start = scnr->pos;
    char *end;
    double res = strtod (scnr->pos, &end);
    if (scnr->pos != end) {
        *value = res;
        scnr->pos = end;

        if (*scnr->pos == '\0') {
            scnr->is_eof = true;
        }

        scnr->len = end - start;
        return true;
    }

    return false;
}

bool date_scanner_char (struct date_scanner_t *scnr, char c)
{
    if (scnr->error) return false;

    if (*scnr->pos == c) {
        scnr->pos++;

        if (*scnr->pos == '\0') {
            scnr->is_eof = true;
        }

        scnr->len = 1;
        return true;
    }

    return false;
}


#define date_reset(d) date_set(d,0,-1,-1,-1,-1,-1,0.0,false,0,0)
void date_set (struct date_t *d,
               int year, int month, int day,
               int hour, int minute, int second, double second_fraction,
               bool is_set_utc_offset, int utc_offset_hour, int utc_offset_minute)
{
    d->year = year;
    d->month = month;
    d->day = day;
    d->hour = hour;
    d->minute = minute;
    d->second = second;
    d->second_fraction = second_fraction;
    d->is_set_utc_offset = is_set_utc_offset;
    d->utc_offset_hour = utc_offset_hour;
    d->utc_offset_minute = utc_offset_minute;
}
#define BOOL_STR(value) (value) ? "true" : "false"

void str_date_internal (string_t *str, struct date_t *d, struct date_t *expected)
{
    str_cat_printf (str, " year: %d", d->year);
    (expected != NULL && d->year != expected->year) ?  str_cat_printf (str, " (expected %d)\n", expected->year) : str_cat_printf (str, "\n");
    str_cat_printf (str, " month: %d", d->month);
    (expected != NULL && d->month != expected->month) ?  str_cat_printf (str, " (expected %d)\n", expected->month) : str_cat_printf (str, "\n");
    str_cat_printf (str, " day: %d", d->day);
    (expected != NULL && d->day != expected->day) ?  str_cat_printf (str, " (expected %d)\n", expected->day) : str_cat_printf (str, "\n");
    str_cat_printf (str, " hour: %d", d->hour);
    (expected != NULL && d->hour != expected->hour) ?  str_cat_printf (str, " (expected %d)\n", expected->hour) : str_cat_printf (str, "\n");
    str_cat_printf (str, " minute: %d", d->minute);
    (expected != NULL && d->minute != expected->minute) ?  str_cat_printf (str, " (expected %d)\n", expected->minute) : str_cat_printf (str, "\n");
    str_cat_printf (str, " second: %d", d->second);
    (expected != NULL && d->second != expected->second) ?  str_cat_printf (str, " (expected %d)\n", expected->second) : str_cat_printf (str, "\n");
    str_cat_printf (str, " second_fraction: %f", d->second_fraction);
    (expected != NULL && d->second_fraction != expected->second_fraction) ?  str_cat_printf (str, " (expected %f)\n", expected->second_fraction) : str_cat_printf (str, "\n");
    str_cat_printf (str, " is_set_utc_offset: %s", BOOL_STR(d->is_set_utc_offset));
    (expected != NULL && d->is_set_utc_offset != expected->is_set_utc_offset) ?  str_cat_printf (str, " (expected %s)\n", BOOL_STR(expected->is_set_utc_offset)) : str_cat_printf (str, "\n");
    str_cat_printf (str, " utc_offset_hour: %d", d->utc_offset_hour);
    (expected != NULL && d->utc_offset_hour != expected->utc_offset_hour) ?  str_cat_printf (str, " (expected %d)\n", expected->utc_offset_hour) : str_cat_printf (str, "\n");
    str_cat_printf (str, " utc_offset_minute: %d", d->utc_offset_minute);
    (expected != NULL && d->utc_offset_minute != expected->utc_offset_minute) ?  str_cat_printf (str, " (expected %d)\n", expected->utc_offset_minute) : str_cat_printf (str, "\n");
}

bool date_scan_utc_offset (struct date_scanner_t *scnr,
                           bool *is_set_utc_offset, int *utc_offset_hour, int *utc_offset_minute)
{
    bool success = true;

    if (date_scanner_char (scnr, 'Z') || date_scanner_char (scnr, 'z')) {
        *is_set_utc_offset = true;

    } else if (date_scanner_char (scnr, '+') || date_scanner_char (scnr, '-')) {
        *is_set_utc_offset = true;

        bool is_negative = *(scnr->pos - 1) == '-';

        if (!date_scanner_int (scnr, utc_offset_hour)) {
            date_scanner_parse_error (scnr);

        } else if (scnr->len == 2 || scnr->len == 1) {
            if (!date_scanner_char (scnr, ':') || !date_scanner_int (scnr, utc_offset_minute)) {
                date_scanner_parse_error (scnr);
            }

            if (scnr->len > 2) {
                date_scanner_set_error (scnr, "Minute UTC offset must be at most 2 digits.");
            }

        } else if (scnr->len == 4) {
            // A common format for the UTC offset seems to be +hhmm or -hhmm,
            // this format without the ':' character isn't allowed by RFC3339
            // but is valid in ISO 8601. If we got a 4 digit number then assume
            // the date was using this no-colon format. We must support this
            // format because very common functions like strftime() only write
            // the UTC offset like this.
            *utc_offset_minute = (*utc_offset_hour)%100;
            *utc_offset_hour = (*utc_offset_hour)/100;

        } else {
            date_scanner_set_error (scnr, "UTC offset must be in hhmm or hh:mm format.");
        }

        if (is_negative) *utc_offset_hour = -(*utc_offset_hour);

        // Following section 4.3. of RFC3339, interpret -00:00 as unknown
        // UTC offset.
        if (is_negative && *utc_offset_hour == 0 && *utc_offset_minute == 0)
        {
            *is_set_utc_offset = false;
        }

    } else {
        // TODO: We allow dates with trailing time separators like "1900T",
        // "1900-08T" or "1900-08-07T". Is this okay?. The following line of
        // code  would block the first 2 cases.
        // :trailing_time_separator
        //
        // date_scanner_parse_error (scnr);
        success = false;
    }

    return success;
}

bool scan_time_separator (struct date_scanner_t *scnr)
{
    return date_scanner_char (scnr, ' ') || date_scanner_char (scnr, 'T') || date_scanner_char (scnr, 't');
}

bool date_read (char *date_time_str, struct date_t *date, string_t *message)
{
    assert (date_time_str != NULL && date != NULL);

    struct date_scanner_t scnr = {0};
    scnr.pos = date_time_str;

    bool is_set_utc_offset = false;
    int utc_offset_hour = 0;
    int utc_offset_minute = 0;

    int32_t year;
    if (!date_scanner_int (&scnr, &year)) {
        date_scanner_parse_error (&scnr);
    } else if (scnr.len != 4) {
        date_scanner_set_error (&scnr, "Year must be 4 digits long.");
    }

    int32_t month = -1;
    if (!scnr.is_eof) {
        if (date_scanner_char (&scnr, '-')) {
            if (!date_scanner_int (&scnr, &month)) {
                date_scanner_parse_error (&scnr);

            } else if (scnr.len > 2) {
                date_scanner_set_error (&scnr, "Month must be at most 2 digits.");
            }

        } else if (scan_time_separator (&scnr)) {
            date_scan_utc_offset (&scnr, &is_set_utc_offset, &utc_offset_hour, &utc_offset_minute);

        } else {
            date_scanner_parse_error (&scnr);
        }
    }

    int32_t day = -1;
    if (!scnr.is_eof) {
        if (date_scanner_char (&scnr, '-')) {
            if (!date_scanner_int (&scnr, &day)) {
                date_scanner_parse_error (&scnr);

            } else if (scnr.len > 2) {
                date_scanner_set_error (&scnr, "Day must be at most 2 digits.");
            }

        } else if (scan_time_separator (&scnr)) {
            date_scan_utc_offset (&scnr, &is_set_utc_offset, &utc_offset_hour, &utc_offset_minute);

        } else {
            date_scanner_parse_error (&scnr);
        }
    }

    int32_t hour = -1;
    if (!scnr.is_eof) {
        if (scan_time_separator (&scnr)) {
            if (!date_scanner_int (&scnr, &hour)) {
                // The "!scnr.is_eof" condition makes "1900-08-07T" valid.
                // :trailing_time_separator
                if (!scnr.is_eof && !date_scan_utc_offset (&scnr, &is_set_utc_offset, &utc_offset_hour, &utc_offset_minute)) {
                    date_scanner_parse_error (&scnr);
                }

            } else if (scnr.len > 2) {
                date_scanner_set_error (&scnr, "Hour must be at most 2 digits.");
            }

        } else {
            date_scanner_parse_error (&scnr);
        }
    }

    int32_t minute = -1;
    if (!scnr.is_eof) {
        if (date_scanner_char (&scnr, ':')) {
            if (!date_scanner_int (&scnr, &minute)) {
                date_scanner_parse_error (&scnr);

            } else if (scnr.len > 2) {
                date_scanner_set_error (&scnr, "Minute must be at most 2 digits.");
            }

        } else if (!date_scan_utc_offset (&scnr, &is_set_utc_offset, &utc_offset_hour, &utc_offset_minute)) {
            date_scanner_parse_error (&scnr);
        }
    }

    int32_t second = -1;
    if (!scnr.is_eof) {
        if (date_scanner_char (&scnr, ':')) {
            if (!date_scanner_int (&scnr, &second)) {
                date_scanner_parse_error (&scnr);
            } else if (scnr.len > 2) {
                date_scanner_set_error (&scnr, "Second must be at most 2 digits.");
            }

        } else if (!date_scan_utc_offset (&scnr, &is_set_utc_offset, &utc_offset_hour, &utc_offset_minute)) {
            date_scanner_parse_error (&scnr);
        }
    }

    double second_fraction = 0.0;
    if (!scnr.is_eof) {
        if (date_scanner_char (&scnr, '.')) {
            scnr.pos--;
            if (!date_scanner_double (&scnr, &second_fraction)) {
                date_scanner_set_error (&scnr, "Invalid second fraction.");
            }

        } else if (!date_scan_utc_offset (&scnr, &is_set_utc_offset, &utc_offset_hour, &utc_offset_minute)) {
            date_scanner_parse_error (&scnr);
        }
    }

    if (!scnr.is_eof) {
        date_scan_utc_offset (&scnr, &is_set_utc_offset, &utc_offset_hour, &utc_offset_minute);
    }

    bool success = true;
    if (scnr.error) {
        success = false;
        if (message != NULL) str_set (message, scnr.error_message);
    }

    if (success) {
        date_set (date, year, month, day, hour, minute, second, second_fraction, is_set_utc_offset, utc_offset_hour, utc_offset_minute);
        success = date_is_valid_d (date, message);
    }

    return success;
}

struct date_t date_read_unix(int64_t seconds)
{
    struct date_t res = {0};
    time_t timestamp = seconds;

    // NOTE: This isn't reentrant. Be careful of changing to localtime_r()
    // without ensuring tzset() has been called before [1].
    //
    // [1]: https://www.mail-archive.com/vim_dev@googlegroups.com/msg52941.html
    struct tm *t = localtime(&timestamp);

    res.year   = t->tm_year + 1900;
    res.month  = t->tm_mon + 1;
    res.day    = t->tm_mday;
    res.hour   = t->tm_hour;
    res.minute = t->tm_min;
    res.second = t->tm_sec;

    res.is_set_utc_offset = true;
    res.utc_offset_hour = -(timezone/60)/60;
    res.utc_offset_minute = -(timezone/60)%60;

    return res;
}

void date_write (struct date_t *date, enum reference_time_duration_t precision, bool no_utc_offset,
                 bool variable_precision, bool padding, bool optional_utc_offset,
                 char *buff)
{
    enum reference_time_duration_t curr_reference_duration = D_YEAR;

    struct date_t min_date = *date;
    char *pos = buff;
    while (curr_reference_duration <= precision) {
        if (curr_reference_duration == D_MONTH || curr_reference_duration == D_DAY) {
            *pos = '-';
            pos++;

        } else if (curr_reference_duration == D_HOUR) {
            *pos = 'T';
            pos++;

        } else if (curr_reference_duration > D_HOUR) {
            *pos = ':';
            pos++;
        }

        int value = date->v[curr_reference_duration];
        if (!variable_precision && value < 0) {
            date_get_value_range (curr_reference_duration, &min_date, &value, NULL);
            min_date.v[curr_reference_duration] = value;
        }

        int len = 0;
        if (!padding) {
            len = sprintf (pos, "%d", date->v[curr_reference_duration]);
        } else {
            len = sprintf (pos, "%02d", value);
        }

        pos += len;
        curr_reference_duration++;

        if (variable_precision && date->v[curr_reference_duration] < 0) break;
    }

    // NOTE: This assumes 0 <= date->second_fraction < 1
    if (precision == D_SECOND && date->second_fraction > 0) {
        pos--;
        char tmp = *pos;

        int len = sprintf (pos, "%.3f", date->second_fraction);

        *pos = tmp;
        pos += len;
    }

    if (!no_utc_offset && (!optional_utc_offset || date->is_set_utc_offset)) {
        if (curr_reference_duration <= D_HOUR) {
            sprintf (pos, "T");
            pos++;
        }

        if (date->is_set_utc_offset && date->utc_offset_hour == 0 && date->utc_offset_minute == 0) {
            sprintf (pos, "Z");

        } else if (!date->is_set_utc_offset) {
            sprintf (pos, "-00:00");

        } else {
            // NOTE(sleon): sprintf does not pad numbers with 0's numbers that have
            // signs, so we manually write signs here and use absolute value later.
            if (date->utc_offset_hour < 0) {
                *pos = '-';
            } else {
                *pos = '+';
            }
            pos++;

            sprintf (pos, "%02d:%02d", ABS(date->utc_offset_hour), date->utc_offset_minute);
        }
    }
}

static inline
void date_write_rfc3339 (struct date_t *date, char *buff)
{
    date_write (date, D_SECOND, false, false, true, false, buff);
}

static inline
void date_write_compact (struct date_t *date, enum reference_time_duration_t precision, char *buff)
{
    date_write (date, precision, false, true, false, true, buff);
}

// Taken from Apendix B of RFC3339. Implements Zeller's congruence.
int date_get_day_of_week(struct date_t *date)
{
    int year = date->year;
    int month = date->month;
    int day = date->day;

    /* adjust months so February is the last one */
    month -= 2;
    if (month < 1) {
        month += 12;
        --year;
    }

    /* split by century */
    int cent = year / 100;
    year %= 100;

    return ((26*month - 2)/10 + day + year + year/4 + cent/4 + 5*cent)%7;
}

// Based on the derivation of Zeller's congruence, this computes an absolute
// integer value for the day of the passed date. It considers variable month
// lengths as well as extra days of leap years.
int date_get_absolute_day_number(struct date_t *date)
{
    int year = date->year;
    int month = date->month;
    int day = date->day;

    month -= 2;
    if (month < 1) {
        month += 12;
        --year;
    }

    int cent = year/100;
    year %= 100;

    int result = 36524*cent + cent/4;
    result += 365*year + year/4;
    result += 153*((month-1)/5) + 31*(((month-1)%5+1)/2) + 30*(((month-1)%5)/2); /*careful with parenthesis!*/
    result += day;

    return result;
}

// Computes the equivalence class of the day of the passed date modulo n. It
// assumes the reference date modulo n is equivalence class 0. This is useful to
// determine if a day is part of some recurrent pattern of days like "every 15
// days".
int date_generic_zellers_congruence(struct date_t *reference, struct date_t *date, int n)
{
    int offset = date_cmp (date, reference) < 0 ? n : 0;
    return offset + (date_get_absolute_day_number(date) - date_get_absolute_day_number (reference))%n;
}

struct recurrent_event_t {
    // I think frequency can be stored inside date_element, will always have
    // spare int there that are not used due to the choice of scale.
    int frequency;
    enum reference_time_duration_t scale;
    struct date_element_t date_element;
    struct date_t start;

    int count;
    struct date_t end;
};

bool recurrent_event_set (struct recurrent_event_t *re,
                          int frequency, enum reference_time_duration_t scale,
                          struct date_element_t *date_element, struct date_t *start_date,
                          string_t *error)
{
    assert (re != NULL && start_date != NULL);

    string_t error_l;
    if (error == NULL) error = &error_l;

    bool success = true;

    if (frequency < 2) frequency = 1;
    re->frequency = frequency;
    re->scale = scale;

    if (date_element != NULL) {
        re->date_element = *date_element;
    } else {
        // NOTE: A zeroed out date element means unset. Checking year==0 can
        // detect this. A set date element will have year==-1.
        re->date_element = (struct date_element_t){0};
    }

    if (date_element != NULL) {
        enum reference_time_duration_t curr_duration = D_YEAR;
        while (date_element->v[curr_duration] == -1) curr_duration++;
        if (curr_duration != scale + 1) {
            success = false;
            str_cat_printf (error, "Date element's undefined must be up to scale's precision value");
        }
    }

    re->start = *start_date;

    return success;
}

bool recurrent_event_next (struct recurrent_event_t *re, struct date_t *curr_occurence, struct date_t *next)
{
    assert (re != NULL && next != NULL);

    struct date_t result;
    if (curr_occurence == NULL) {
        result = re->start;
    } else {
        result = *curr_occurence;
    }

    date_add_value (&result, re->frequency, re->scale);

    if (re->date_element.year != 0) {
        enum reference_time_duration_t curr_duration = D_YEAR;
        while (re->date_element.v[curr_duration] == -1) curr_duration++;

        while (curr_duration < D_REFERENCE_TIME_DURATION_LEN && re->date_element.v[curr_duration] != -1) {
            result.v[curr_duration] = re->date_element.v[curr_duration];
            curr_duration++;
        }
    }

    *next = result;

    return false;
}

//////////////////
// DATE STRING API
//
// Convenience functions that work on date strings instead of date_t structs
bool date_is_valid (char *date_str, string_t *error)
{
    struct date_t date;
    return date_read (date_str, &date, error);
}

int date_cmp_str (char *d1, char *d2, bool *success, string_t *error)
{
    bool success_l;
    if (success == NULL) success = &success_l;

    string_t error_l;
    if (error == NULL) error = &error_l;

    struct date_t date1, date2;

    *success = date_read (d1, &date1, error);
    if (*success) {
        *success = date_read (d2, &date2, error);
    }

    return date_cmp (&date1, &date2);
}

// NOTE: buff must have allocated at least DATE_TIMESTAMP_MAX_LEN bytes.
void date_get_now (char *buff)
{
    assert (buff != NULL);

    struct date_t now = {0};
    date_get_now_d (&now);
    date_write_rfc3339 (&now, buff);
}
