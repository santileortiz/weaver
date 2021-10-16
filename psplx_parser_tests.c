/*
 * Copyright (C) 2021 Santiago León O.
 */

#include "common.h"
#include "binary_tree.c"
#include "test_logger.c"
#include "cli_parser.c"
#include "automacros.h"

#include "block.h"
#include "note_runtime.h"

#include "psplx_parser.c"
#include "note_runtime.c"

#include "testing.c"

#define push_test_note(rt,source) push_test_note_full(rt,source,strlen(source),(char*)__func__,strlen(__func__),NULL)
struct note_t* push_test_note_full (struct note_runtime_t *rt, char *source, size_t source_len, char *note_id, size_t note_id_len, char *path)
{
    LINKED_LIST_PUSH_NEW (&rt->pool, struct note_t, rt->notes, new_note);
    new_note->id = pom_strndup (&rt->pool, note_id, note_id_len);
    id_to_note_tree_insert (&rt->notes_by_id, new_note->id, new_note);

    str_pool (&rt->pool, &new_note->error_msg);
    if (path != NULL) str_set_pooled (&rt->pool, &new_note->path, path);

    str_pool (&rt->pool, &new_note->psplx);
    strn_set (&new_note->psplx, source, source_len);

    str_pool (&rt->pool, &new_note->title);
    if (!parse_note_title (path, str_data(&new_note->psplx), &new_note->title, &new_note->error_msg)) {
        new_note->error = true;
        printf ("%s", str_data(&new_note->error_msg));

    } else {
        title_to_note_tree_insert (&rt->notes_by_title, &new_note->title, new_note);
    }

    str_pool (&rt->pool, &new_note->html);

    return new_note;
}

//////////////////////////////////////
// Platform functions for testing
#define PSPLX_EXTENSION "psplx"

ITERATE_DIR_CB(test_dir_iter)
{
    struct note_runtime_t *rt = (struct note_runtime_t*)data;

    if (!is_dir) {
        char *extension = get_extension (fname);
        if (extension != NULL && strcmp (extension, PSPLX_EXTENSION) == 0) {
            size_t basename_len = 0;
            char *p = fname + strlen (fname) - 1;
            while (p != fname && *p != '/') {
                basename_len++;
                p--;
            }
            p++; // advance '/'

            size_t source_len;
            char *source = full_file_read (NULL, fname, &source_len);
            push_test_note_full (rt, source, source_len, p, basename_len - 1/*.*/ - strlen(PSPLX_EXTENSION), fname);
            free (source);
        }
    }
}

void rt_init_from_dir (struct note_runtime_t *rt, char *path)
{
    rt->notes_by_id.pool = &rt->pool;
    rt->notes_by_title.pool = &rt->pool;

    iterate_dir (path, test_dir_iter, rt);
}
//
//////////////////////////////////////

void negative_programmatic_test (struct test_ctx_t *t, struct note_runtime_t *rt, char *test_id, char *source)
{
    bool no_crash = false;

    // TODO: Abstract this out in a more generic and usable way into
    // test_logger.c
    //
    // Negative testing is somewhat different from normal testing. When this is
    // abstracted out, take into account the following to provide a user
    // firiendly experience:
    //
    //   - In the common case, negative testing makes a limited subset of
    //     assertions. Normally we will only test that we successfuly produced
    //     an error. In a more sophisticated case where we have error codes, we
    //     may want to compare and check we got the correct error. Or, if there
    //     are different levels of failures info(?)/warning/error, we just want
    //     to check we got the correct level.
    //
    //   - It's always confusing how to pass the result to the API. Is the
    //     negation of the result boolean expected to be done by the user, or is
    //     it done internally?. A good fix for this is to use mnemonic naming of
    //     the API, for example, I think the variable name
    //     "errored_successfully" clearly conveys the correct semantics.
    //
    //   - The current test suite receives as parameter a test ID (as printed in the
    //     execution output), then it runs this single test. Because negative
    //     tests may produce a different output in such case, it's good to
    //     somehow show in the test execution output that these are negative
    //     tests. I settled for useing the "-OK" string as success string.
    //
    //   - We most likely want to run negative tests in a separate process,
    //     mainly because:
    //
    //       - User most likely doesn't want negative tests to have sideffects
    //         in the execution of the rest of the test suite.
    //
    //       - Negative tests will probably crash more often than normal tests (?).
    //
    //     This has a copule of consequences:
    //
    //       - We need to use a shared variable to report the result of the
    //         test. This is different than the success variable passed to
    //         CRASH_TEST macro, whose only purpose is to report if the
    //         subprocess crashed or exited successfully.
    //
    //       - To unconditionally report a message to the parent process I
    //         implemented a mechanism where writing to a string in the child
    //         process will pass the data to the parent through a temporary file
    //         (like we do for stdout and stderr).
    //
    test_push (t, "%s", test_id);
    str_set (&t->test_stack->success_string, "-OK");
    NEW_SHARED_VARIABLE_NAMED (bool, errored_successfuly, false, "NEGATIVE_TEST_subprocess_success");
    CRASH_TEST(no_crash, t->error,
        struct note_t *test_note = push_test_note_full (rt, source, strlen(source), test_id, strlen(test_id), test_id);
        rt_process_note (&rt->pool, test_note);

        if (test_note->error) {
            *errored_successfuly = true;
            str_set (&__subprocess_output, str_data(&test_note->error_msg));
        }
    );

    UNLINK_SHARED_VARIABLE_NAMED ("NEGATIVE_TEST_subprocess_success");

    test_pop (t, no_crash && *errored_successfuly);
}

void negative_tests (struct test_ctx_t *t, struct note_runtime_t *rt)
{
    char *test_id = "single_hash_test()";
    char *source =
        "# Single Hash Test\n"
        "\n"
        "A note can't have single hash titles besides its title\n"
        "\n"
        "# This should fail (but not crash)\n";
    negative_programmatic_test (t, rt, test_id, source);
}

void set_expected_html_path (string_t *str, char *note_id)
{
    str_set_printf (str, TESTS_DIR "/%s.html", note_id);
}

int main(int argc, char** argv)
{
    mem_pool_t pool = {0};

    string_t buff = {0};

    struct test_ctx_t _t = {0};
    struct test_ctx_t *t = &_t;

    __g_note_runtime = ZERO_INIT (struct note_runtime_t);
    struct note_runtime_t *rt = &__g_note_runtime;

    rt_init_from_dir (rt, TESTS_DIR);

    char *note_id = get_cli_no_opt_arg (argv, argc);
    bool html_out = get_cli_bool_opt ("--html", argv, argc);
    bool blocks_out = get_cli_bool_opt ("--blocks", argv, argc);
    bool no_output = get_cli_bool_opt ("--none", argv, argc);
    t->show_all_children = get_cli_bool_opt ("--full", argv, argc);

    bool notes_processed = true;
    test_push (t, "Process all notes");
    CRASH_TEST_AND_RUN (notes_processed, t->error,
        rt_process_notes (rt, NULL);
    );
    test_pop (t, notes_processed);

    if (notes_processed) {
        negative_tests (t, rt);
    }

    if (note_id == NULL) {
        LINKED_LIST_FOR (struct note_t*, note, rt->notes) {
            // Don't allocate these files into pool so we free them on each test
            // execution. This avoids keeping all in memory until the end.
            set_expected_html_path (&buff, note->id);
            char *expected_html = NULL;
            if (path_exists (str_data(&buff))) {
                expected_html = full_file_read (NULL, str_data(&buff), NULL);
            }

            if (expected_html == NULL) {
                test_push (t, "%s " ECMA_YELLOW("(No HTML)"), note->id);
            } else {
                test_push (t, "%s", note->id);
            }

            test_push (t, "Source parsing");
            if (!test_bool (t, !note->error && str_len(&note->error_msg) == 0)) {
                test_error_c (t, str_data(&note->error_msg));
            }

            if (expected_html != NULL) {
                test_push (t, "Matches expected HTML");
                test_str (t, str_data(&note->html), expected_html);
                free (expected_html);
            }

            parent_test_pop(t);
        }

    } else {
        struct note_t *note = id_to_note_get (&rt->notes_by_id, note_id);

        if (note != NULL) {
            if (!note->error) {
                if (html_out) {
                    printf ("%s", str_data(&note->html));

                } else if (blocks_out) {
                    struct psx_block_t *root_block = parse_note_text (&pool, str_data(&note->path), str_data(&note->psplx), NULL);
                    printf_block_tree (root_block, 4);

                } else if (no_output) {
                    // nothing

                } else {
                    printf (ECMA_MAGENTA("PSPLX") "\n");
                    prnt_debug_string (str_data(&note->psplx));
                    printf ("\n");

                    printf (ECMA_MAGENTA("HTML") "\n");
                    prnt_debug_string (str_data(&note->html));

                    if (str_len(&note->error_msg) > 0) {
                        printf (ECMA_MAGENTA("\nERRORS") "\n");
                        printf ("%s", str_data (&note->error_msg));
                    }

                    set_expected_html_path (&buff, note->id);
                    print_diff_str_to_expected_file (&note->html, str_data (&buff));
                }
            }

        } else {
            printf ("Note id not found\n");
        }
    }

    printf ("%s", str_data(&t->result));

    mem_pool_destroy (&rt->pool);
    test_ctx_destroy (t);
    mem_pool_destroy (&pool);
    js_destroy ();
    str_free (&buff);
    return 0;
}
