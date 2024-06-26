/*======================================================================
 FILE: regression-utils.c

 SPDX-FileCopyrightText: 1999 Eric Busboom <eric@civicknowledge.com>

 SPDX-License-Identifier: LGPL-2.1-only OR MPL-2.0

 The original author is Eric Busboom
======================================================================*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "libical/ical.h"

#include "test-malloc.h"
#include "regression.h"

#include <stdlib.h>

int QUIET = 0;
int VERBOSE = 1;

static char ictt_str[1024];

const char *ical_timet_string(const icaltime_t t)
{
    struct tm tmp, stm;

    memset(&tmp, 0, sizeof(tmp));
    if (icalgmtime_r(&t, &tmp)) {
        stm = tmp;
    } else {
        memset(&stm, 0, sizeof(stm));
    }

    snprintf(ictt_str, sizeof(ictt_str),
             "%02d-%02d-%02d %02d:%02d:%02d Z",
             stm.tm_year + 1900, stm.tm_mon + 1, stm.tm_mday, stm.tm_hour, stm.tm_min, stm.tm_sec);

    return ictt_str;
}

const char *ictt_as_string(struct icaltimetype t)
{
    const char *zone = icaltimezone_get_tzid((icaltimezone *)t.zone);

    if (icaltime_is_utc(t)) {
        snprintf(ictt_str, sizeof(ictt_str),
                 "%02d-%02d-%02d %02d:%02d:%02d Z UTC",
                 t.year, t.month, t.day, t.hour, t.minute, t.second);
    } else {
        snprintf(ictt_str, sizeof(ictt_str),
                 "%02d-%02d-%02d %02d:%02d:%02d %s",
                 t.year, t.month, t.day, t.hour, t.minute, t.second,
                 zone == NULL ? "(floating)" : zone);
    }

    return ictt_str;
}

char *icaltime_as_ctime(struct icaltimetype t)
{
    icaltime_t tt;

    tt = icaltime_as_timet(t);
    snprintf(ictt_str, sizeof(ictt_str), "%s", icalctime(&tt));

    return ictt_str;
}

/** This variable is used to control whether we core dump on errors **/
static int die_on_errors = 0;
static int testnumber = 0;
static int failed = 0;
static int current_set = 0;

static struct failed_tests {
    int set;
    int test;
} failed_tests[1024];

void die_on_errors_set(int val)
{
    die_on_errors = val;
}

void _ok(const char *test_name, int success, const char *file, int linenum, const char *test)
{
    testnumber++;

    if (!QUIET || (QUIET && !success))
        printf("%sok %d - %s\n", (success) ? "" : "not ", testnumber, test_name);

    if (!success) {
        failed_tests[failed].set = current_set;
        failed_tests[failed].test = testnumber;
        failed++;

        printf("# test failed: \"%s\"\n", test);
        printf("#          at: %s:%-d\n", file, linenum);
    }

    if (die_on_errors == 1 && !success) {
        abort();
    }
}

void _is(const char *test_name, const char *str1, const char *str2, const char *file, int linenum)
{
    int diff;

    if (str1 == NULL || str2 == NULL) {
        diff = 1;
    } else {
        diff = strcmp(str1, str2);
    }

    if (!test_name)
        test_name = "()";

    _ok(test_name, (diff == 0), file, linenum, "");

    if (diff) {
        printf("#      got: %s\n", str1 ? str1 : "(null)");
        printf("# expected: %s\n", str2 ? str2 : "(null)");
    }
}

void _int_is(const char *test_name, int i1, int i2, const char *file, int linenum)
{
    _ok(test_name, (i1 == i2), file, linenum, "");

    if (i1 != i2) {
        printf("#      got: %d\n", i1);
        printf("# expected: %d\n", i2);
    }
}

void verbose(int newval)
{
    VERBOSE = newval;
}

void test_start(int numtests)
{
    if (numtests) {
        if (!QUIET)
            printf("1..%-d\n", numtests);
    } else {
        if (!QUIET)
            printf("1..\n");
    }
}

void test_header(const char *header, int set)
{
    if (!QUIET)
        printf("########## %-40s (%d) ##########\n", header, set);

    current_set = set;
}

int test_end(void)
{
    int pct;

    if (testnumber < 1) {
        printf("\n        No Tests Run.\n");
        return 0;
    }

    if (failed) {
        int i, oldset = 0;

        pct = ((testnumber - failed) * 100) / testnumber;
        printf("\n        Failed %d/%d tests, %2d%% okay\n", failed, testnumber, pct);
        printf("\n        Failed tests:\n          ");
        for (i = 0; i < failed; i++) {
            int this_set = failed_tests[i].set;
            const char *prefix = "";

            if (this_set != oldset) {
                prefix = "\n          ";
                oldset = this_set;
            }

            printf("%s%d/%d ", prefix, this_set, failed_tests[i].test);
        }
        printf("\n");
    } else {
        printf("\n        All Tests Successful.\n");
    }

    return failed;
}

/**
  * Bring all memory that is allocated as side effect into a stable state, so we can calculate
  * stable memory allocation statistics.
  */
static void cleanup_nondeterministic_memory(void)
{
    // icalerrno_return() allocates a buffer on it's first use per thread. Let's allocate it
    // now, so it doesn't disturb our test statistics.
    icalerrno_return();

    // Built-in timezones are cached by libical. By freeing them, they don't influence our statistics.
    icaltimezone_free_builtin_timezones();

    // Memory that was added to the ring buffer is not required to be freed by the caller, so
    // we free it here to keep our statistics clean.
    icalmemory_free_ring();
}

void test_run(const char *test_name, void (*test_fcn)(void), int do_test, int headeronly)
{
    static int test_set = 1;

    if (headeronly || do_test == 0 || do_test == test_set)
        test_header(test_name, test_set);

    if (!headeronly && (do_test == 0 || do_test == test_set)) {
        struct testmalloc_statistics mem_statistics;

        // Clean up cached and other kind of non-deterministic memory.
        cleanup_nondeterministic_memory();

        // Now that we are in a stable state, reset the memory statistics and start counting.
        testmalloc_reset();

        // Run the test.
        (*test_fcn)();

        // Before getting the statistics, clean up any non-deterministic memory again, so it
        // doesn't influence the statistics.
        cleanup_nondeterministic_memory();

        // Now we should get clean statistics.
        testmalloc_get_statistics(&mem_statistics);

        ok("no memory leaked",
           (mem_statistics.mem_allocated_current == 0) &&
               (mem_statistics.blocks_allocated == 0));

        if (!QUIET)
            printf("\n");
    }
    test_set++;
}
