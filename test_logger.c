/*
 * Copyright (C) 2020 Santiago León O.
 */

// Required flags
//  gcc: -lrt

// These are required for the CRASH_TEST() and CRASH_TEST_AND_RUN() macros.
// They also require adding -lrt as build flag.
// TODO: How can we make this detail easily discoverable by users? Where to
// document it?
#include <sys/wait.h>
#include <sys/mman.h>

struct test_t {
    string_t success_string;
    string_t output;
    string_t error;
    string_t children;

    bool children_success;

    struct test_t *next;
};

// TODO: Put these inside struct test_ctx_t while still supporting zero
// initialization of the structure.
#define TEST_NAME_WIDTH 40
#define TEST_INDENT 4

struct test_ctx_t {
    mem_pool_t pool;
    string_t result;

    string_t *error;
    bool last_test_success;
    struct test_t *last_test;

    // By default results of child tests are only shown if the parent test
    // failed. If the following is true, then all child test results will be
    // show.
    bool show_all_children;

    bool disable_colors;

    struct test_t *test_stack;

    struct test_t *test_fl;
};

void test_ctx_destroy (struct test_ctx_t *tc)
{
    str_free (&tc->result);
    mem_pool_destroy (&tc->pool);
}

GCC_PRINTF_FORMAT(2, 3)
void test_push (struct test_ctx_t *tc, char *fmt, ...)
{
    if (tc->last_test != NULL) {
        if (tc->show_all_children || !tc->last_test_success) {
            str_cat_indented (&tc->last_test->output, &tc->last_test->error, TEST_INDENT);
            // TODO: Should this be indented?
            // :indented_children_cat
            str_cat (&tc->last_test->output, &tc->last_test->children);
        }

        if (tc->test_stack) {
            str_cat_indented (&tc->test_stack->children, &tc->last_test->output, TEST_INDENT);
        }
    }

    tc->last_test = NULL;

    struct test_t *test;
    if (tc->test_fl == NULL) {
        LINKED_LIST_PUSH_NEW (&tc->pool, struct test_t, tc->test_stack, new_test);
        str_set_pooled (&tc->pool, &new_test->success_string, "OK");
        str_set_pooled (&tc->pool, &new_test->error, "");
        str_set_pooled (&tc->pool, &new_test->output, "");
        str_set_pooled (&tc->pool, &new_test->children, "");

        test = new_test;

    } else {
        test = tc->test_fl;
        tc->test_fl = tc->test_fl->next;
        test->next = NULL;

        LINKED_LIST_PUSH (tc->test_stack, test);
    }

    test->children_success = true;
    str_set (&test->children, "");
    str_set (&test->error, "");
    tc->error = &test->error;

    {
        PRINTF_INIT(fmt, final_size, vargs);
        str_maybe_grow (&test->output, final_size-1, false);
        char *dst = str_data(&test->output);
        PRINTF_SET (dst, final_size, fmt, vargs);

        str_cat_c (&test->output, " ");
        // TODO: Don't count ECMA escape sequences in the length.
        while (str_len(&test->output) < TEST_NAME_WIDTH-1) {
            str_cat_c (&test->output, ".");
        }
        str_cat_c (&test->output, " ");
    }
}

void test_push_c (struct test_ctx_t *tc, char *test_name)
{
    test_push (tc, "%s", test_name);
}

// TODO: Maybe move to common.h?
#define OPT_COLOR(runtime_condition,color_macro,string) \
    ((runtime_condition) ? color_macro(string) : string)

void _test_pop (struct test_ctx_t *tc, bool success)
{
    if (tc->last_test != NULL) {
        if (tc->show_all_children || !tc->last_test_success) {
            str_cat_indented (&tc->last_test->output, &tc->last_test->error, TEST_INDENT);
            // TODO: Should this be indented?
            // :indented_children_cat
            str_cat (&tc->last_test->output, &tc->last_test->children);
        }

        if (tc->test_stack) {
            str_cat_indented (&tc->test_stack->children, &tc->last_test->output, TEST_INDENT);
        }
    }

    struct test_t *curr_test = tc->test_stack;
    tc->test_stack = tc->test_stack->next;
    tc->last_test_success = success;

    if (tc->last_test != NULL) {
        curr_test->next = NULL;
        LINKED_LIST_PUSH (tc->test_fl, tc->last_test);
    }
    tc->last_test = curr_test;

    if (tc->test_stack != NULL) {
        tc->test_stack->children_success = tc->test_stack->children_success && success;
    }

    if (success) {
        if (!tc->disable_colors) {
            str_cat_c (&curr_test->output, ESC_COLOR_BEGIN(1, 32 /*GREEN*/));
        }

        str_cat (&curr_test->output, &curr_test->success_string);

        if (!tc->disable_colors) {
            str_cat_c (&curr_test->output, ESC_COLOR_END);
        }

        str_cat_c (&curr_test->output, "\n");

    } else {
        str_cat_printf (&curr_test->output, "%s\n",
                        OPT_COLOR(!tc->disable_colors, ECMA_RED, "FAILED"));
    }

    if (tc->show_all_children || !success) {
        str_cat_indented (&curr_test->output, &curr_test->error, TEST_INDENT);
        // TODO: Should this be indented?
        // :indented_children_cat
        str_cat (&curr_test->output, &curr_test->children);
    }

    if (!tc->test_stack) {
        str_cat (&tc->result, &curr_test->output);
    }
}

GCC_PRINTF_FORMAT(2, 3)
void test_error_current (struct test_ctx_t *t, char *fmt, ...)
{
    PRINTF_INIT(fmt, final_size, vargs);
    str_maybe_grow (t->error, final_size, false);
    char *dst = str_data(t->error);
    PRINTF_SET (dst, final_size, fmt, vargs);

    // If there is a missing newline add it.
    // NOTE: str_maybe_grow() was passed final_size which is oversized by 1 to
    // account for the possibility of adding a '\n' character here. Normally we
    // would've called it with 'final_size-1' because final_size does count the
    // null byte and str_maybe_grow() expects length withoug counting it.
    // :oversized_allocation_for_newline
    if (dst[final_size-2] != '\n') {
        dst[final_size-1] = '\n';
        dst[final_size] = '\0';
    }
}

GCC_PRINTF_FORMAT(2, 3)
void test_error (struct test_ctx_t *t, char *fmt, ...)
{
    PRINTF_INIT(fmt, final_size, vargs);
    str_maybe_grow (&t->last_test->error, final_size, false);
    char *dst = str_data(&t->last_test->error);
    PRINTF_SET (dst, final_size, fmt, vargs);

    // :oversized_allocation_for_newline
    if (dst[final_size-2] != '\n') {
        dst[final_size-1] = '\n';
        dst[final_size] = '\0';
    }
}

void test_error_c (struct test_ctx_t *t, char *error_msg)
{
    test_error (t, "%s", error_msg);
}

bool test_bool (struct test_ctx_t *t, bool result)
{
    _test_pop (t, result);
    return result;
}

bool test_str (struct test_ctx_t *t, char *result, char *expected)
{
    return test_bool(t, strcmp(result, expected) == 0);
}

bool test_int (struct test_ctx_t *t, int result, int expected)
{
    return test_bool(t, result == expected);
}

// For the case where it makes sense to use the expected string as start of the test name.
void test_str_small (struct test_ctx_t *t, char *test_name, char *result, char *expected)
{
    test_push (t, "%s (%s)", expected, test_name);
    if (test_bool (t, strcmp(result,expected) != 0)) {
        test_error (t, "Failed string comparison got '%s', expected '%s'", result, expected);
    }
}

// TODO: Remove...
void string_test (struct test_ctx_t *t, char *test_name, char *result, char *expected)
{
    bool success = true;
    test_push (t, "%s (%s)", expected, test_name);
    if (strcmp (result, expected) != 0) {
        str_cat_printf (t->error, "Failed string comparison got '%s', expected '%s'\n", result, expected);
        success = false;
    }
    _test_pop (t, success);
}

// TODO: Remove...
void int_test (struct test_ctx_t *t, char *test_name, int result, int expected)
{
    bool success = true;
    test_push (t, "%s", test_name);
    if (result != expected) {
        str_cat_printf (t->error, "Failed int comparison got %d, expected %d\n", result, expected);
        success = false;
    }
    _test_pop (t, success);
}

// Frontend of _test_pop(). Pops a test but fails if any child test also failed
// if there were no children or all child tests passed then the fail/sucess of
// the test being popped is determined by the passed _success_ parameter.
//
// When popping a test whose fail/success only depends on that of its children,
// use the shorthand macro test_pop_parent().
#define test_pop_parent(tc) test_pop(tc,true)
void test_pop (struct test_ctx_t *tc, bool success)
{
    _test_pop (tc, tc->test_stack->children_success && success);
}

#define CRASH_SAFE_TEST_SHARED_VARIABLE_NAME "TEST_CRASH_SAFE_success"

bool __crash_safe_wait_and_output (mem_pool_t *pool, bool *success,
                                   char *output_fname, char *stdout_fname, char *stderr_fname,
                                   string_t *result)
{
    bool res = false;

    int child_status;
    wait (&child_status);

    if (path_exists(output_fname)) {
        char *output_str = full_file_read (pool, output_fname, NULL);
        if (*output_str != '\0') {
            str_cat_c (result, output_str);
        }
    }

    if (!WIFEXITED(child_status)) {
        *success = false;
        str_cat_printf (result, "Exited abnormally with status: %d\n", child_status);
    }

    if (!*success) {
        char *stdout_str = full_file_read (pool, stdout_fname, NULL);
        if (*stdout_str != '\0') {
            str_cat_c (result, ECMA_CYAN("stdout:\n"));
            str_cat_indented_c (result, stdout_str, 2);
        }

        char *stderr_str = full_file_read (pool, stderr_fname, NULL);
        if (*stderr_str != '\0') {
            str_cat_c (result, ECMA_CYAN("stderr:\n"));
            str_cat_indented_c (result, stderr_str, 2);
        }
    }

    res = *success;

    unlink (output_fname);
    unlink (stdout_fname);
    unlink (stderr_fname);

    UNLINK_SHARED_VARIABLE_NAMED (CRASH_SAFE_TEST_SHARED_VARIABLE_NAME);

    return res;
}

#if !defined TEST_NO_SUBPROCESS
#define CRASH_TEST(SUCCESS,OUTPUT,CODE)                                                                             \
{                                                                                                                   \
    char *__crash_safe_output_fname = "tmp_output";                                                                 \
    char *__crash_safe_stdout_fname = "tmp_stdout";                                                                 \
    char *__crash_safe_stderr_fname = "tmp_stderr";                                                                 \
    mem_pool_t __crash_safe_pool = {0};                                                                             \
    NEW_SHARED_VARIABLE_NAMED (bool, __crash_safe_success, true, CRASH_SAFE_TEST_SHARED_VARIABLE_NAME);             \
    if (fork() == 0) {                                                                                              \
        freopen (__crash_safe_stdout_fname, "w", stdout);                                                           \
        setvbuf (stdout, NULL, _IONBF, 0);                                                                          \
                                                                                                                    \
        freopen (__crash_safe_stderr_fname, "w", stderr);                                                           \
        setvbuf (stderr, NULL, _IONBF, 0);                                                                          \
                                                                                                                    \
        string_t __subprocess_output = {0};                                                                         \
                                                                                                                    \
        CODE                                                                                                        \
                                                                                                                    \
        full_file_write (str_data(&__subprocess_output), str_len(&__subprocess_output), __crash_safe_output_fname); \
                                                                                                                    \
        exit(0);                                                                                                    \
    }                                                                                                               \
    SUCCESS = __crash_safe_wait_and_output (&__crash_safe_pool, __crash_safe_success,                               \
                                            __crash_safe_output_fname,                                              \
                                            __crash_safe_stdout_fname, __crash_safe_stderr_fname,                   \
                                            OUTPUT);                                                                \
}

#define CRASH_TEST_AND_RUN(SUCCESS,OUTPUT,CODE)  \
    CRASH_TEST(SUCCESS,OUTPUT,CODE)              \
    if (SUCCESS) {                               \
        CODE                                     \
    }

#else
#define CRASH_TEST(SUCCESS,OUTPUT,CODE) CODE
#define CRASH_TEST_AND_RUN(SUCCESS,OUTPUT,CODE) CODE

#endif
