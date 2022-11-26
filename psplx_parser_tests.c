/*
 * Copyright (C) 2021 Santiago León O.
 */

#include "common.h"
#include "binary_tree.c"
#include "test_logger.c"
#include "cli_parser.c"
#include "automacros.h"

#include "file_utility.h"
#include "block.h"
#include "note_runtime.h"

#define ATTRIBUTE_PLACEHOLDER
#include "psplx_parser.c"

#include "note_runtime.c"

#include "testing.c"

struct note_t* push_test_note (struct note_runtime_t *rt, char *source, size_t source_len, char *note_id, size_t note_id_len, char *path)
{
    struct note_t *new_note = rt_new_note (rt, note_id, note_id_len);

    str_pool (&rt->pool, &new_note->error_msg);
    if (path != NULL) str_set_pooled (&rt->pool, &new_note->path, path);

    str_pool (&rt->pool, &new_note->psplx);
    strn_set (&new_note->psplx, source, source_len);

    str_pool (&rt->pool, &new_note->title);
    if (!parse_note_title (path, str_data(&new_note->psplx), &new_note->title, &rt->sd, &new_note->error_msg)) {
        new_note->error = true;
        printf ("%s", str_data(&new_note->error_msg));

    } else {
        title_to_note_insert (&rt->notes_by_title, &new_note->title, new_note);
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
            push_test_note (rt, source, source_len, p, basename_len - 1/*.*/ - strlen(PSPLX_EXTENSION), fname);
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
        struct note_t *test_note = push_test_note (rt, source, strlen(source), test_id, strlen(test_id), test_id);
        rt_process_note (&rt->pool, NULL, test_note);

        if (test_note->error || str_len (&test_note->error_msg) > 0) {
            *errored_successfuly = true;

#if !defined TEST_NO_SUBPROCESS
            str_set (&__subprocess_output, str_data(&test_note->error_msg));
#endif

        }
    );

    UNLINK_SHARED_VARIABLE_NAMED ("NEGATIVE_TEST_subprocess_success");
    if (!(*errored_successfuly)) {
        test_error_current (t, "Expected an error but note parsed successfully.");
    }
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

    test_id = "broken_note_link()";
    source =
        "# Broken Note Link\n"
        "\n"
        "Weaver should warn whenever a tag points to a \\note{Non-existent Note} \n";
    negative_programmatic_test (t, rt, test_id, source);
}

void set_expected_path (string_t *str, char *note_id, char *extension)
{
    str_set_printf (str, TESTS_DIR "/%s%s", note_id, extension);
}

void set_expected_tsplx_path (string_t *str, char *note_id)
{
    set_expected_path (str, note_id, ".canonical.tsplx");
}

void set_expected_html_path (string_t *str, char *note_id)
{
    set_expected_path (str, note_id, ".html");
}

// TODO: Right now we only attaxha SPLX data node to each block, this just
// concatenates all data nodes og a block tree. Ideally the SPLX data itself
// would mimmic the text hierarchy structure of the note, such that just
// serializing the single root node of the page reaches all data contained in
// it. That's not the case at the moment.
#define cat_note_tsplx(str,sd,root) cat_note_tsplx_full(str,sd,root,NULL)
void cat_note_tsplx_full (string_t *str, struct splx_data_t *sd, struct psx_block_t *root, struct psx_block_t *block)
{
    if (block == NULL) block = root;

    if (block->data != NULL) {
        str_cat_splx_canonical (str, sd, block->data);
        str_cat_c (str, "\n");
    }

    if (block->block_content != NULL) {
        LINKED_LIST_FOR(struct psx_block_t*, curr_block, block->block_content) {
            cat_note_tsplx_full (str, sd, root, curr_block);
        }
    }
}

int main(int argc, char** argv)
{
    mem_pool_t pool = {0};

    string_t buff = {0};

    STACK_ALLOCATE (struct test_ctx_t , t);

    __g_note_runtime = ZERO_INIT (struct note_runtime_t);
    struct note_runtime_t *rt = &__g_note_runtime;

    rt_init_from_dir (rt, TESTS_DIR);

    STACK_ALLOCATE (struct cli_ctx_t, cli_ctx);
    bool tsplx_out = get_cli_bool_opt_ctx (cli_ctx, "--tsplx", argv, argc);
    bool html_out = get_cli_bool_opt_ctx (cli_ctx, "--html", argv, argc);
    bool blocks_out = get_cli_bool_opt_ctx (cli_ctx, "--blocks", argv, argc);
    bool no_output = get_cli_bool_opt_ctx (cli_ctx, "--none", argv, argc);
    t->show_all_children = get_cli_bool_opt_ctx (cli_ctx, "--full", argv, argc);
    char *note_id = get_cli_no_opt_arg (cli_ctx, argv, argc);

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

            set_expected_tsplx_path (&buff, note->id);
            char *expected_tsplx = NULL;
            if (path_exists (str_data(&buff))) {
                expected_tsplx = full_file_read (NULL, str_data(&buff), NULL);
            }
            bool warn_missing_tsplx = (expected_tsplx == NULL && note->tree != NULL && note->tree->data != NULL) && tsplx_out;

            if (expected_html == NULL || warn_missing_tsplx) {
                string_t missing_info = {0};

                if (expected_html == NULL) {
                    str_cat_c (&missing_info, "!html");
                }

                if (warn_missing_tsplx) {
                    if (str_len (&missing_info) && str_last (&missing_info) != ' ') {
                        str_cat_c (&missing_info, " ");
                    }

                    str_cat_c (&missing_info, "!tsplx");
                }

                test_push (t, "%s " ECMA_YELLOW("(%s)"), note->id, str_data(&missing_info));
                str_free (&missing_info);
            } else {
                test_push (t, "%s", note->id);
            }

            test_push (t, "Source parsing");
            if (!test_bool (t, !note->error && str_len(&note->error_msg) == 0)) {
                test_error_c (t, str_data(&note->error_msg));
            }

            if (expected_html != NULL) {
                test_push (t, "Matches expected HTML");

                string_t html_str = {0};
                str_cat_html (&html_str, note->html, 2);
                test_str (t, str_data(&html_str), expected_html);
                str_free(&html_str);

                free (expected_html);
            }



            if (path_exists (str_data(&buff)) && note->tree != NULL && note->tree->data != NULL) {
                string_t tsplx_str = {0};
                cat_note_tsplx (&tsplx_str, &rt->sd, note->tree);

                test_push (t, "Matches expected TSPLX");
                test_str (t, str_data(&tsplx_str), expected_tsplx);

                free (expected_tsplx);
                str_free (&tsplx_str);
            }


            test_pop_parent(t);
        }

    } else {
        struct note_t *note = id_to_note_get (&rt->notes_by_id, note_id);

        if (note != NULL) {
            if (!note->error) {
                if (html_out) {
                    string_t html_str = {0};
                    str_cat_html (&html_str, note->html, 2);
                    printf ("%s", str_data(&html_str));
                    str_free(&html_str);

                } else if (blocks_out) {
                    STACK_ALLOCATE(struct psx_parser_ctx_t, ctx);
                    ctx->path = str_data(&note->path);
                    ctx->error_msg = &note->error_msg;
                    ctx->sd = &rt->sd;

                    struct psx_block_t *root_block = parse_note_text (&pool, ctx, str_data(&note->psplx));
                    printf_block_tree (root_block, 4);

                } else if (no_output) {
                    // nothing

                } else {
                    printf (ECMA_MAGENTA("PSPLX") "\n");
                    prnt_debug_string (str_data(&note->psplx));
                    printf ("\n");

                    string_t html_str = {0};
                    str_cat_html (&html_str, note->html, 2);
                    printf (ECMA_MAGENTA("HTML") "\n");
                    prnt_debug_string (str_data(&html_str));
                    set_expected_html_path (&buff, note->id);
                    print_diff_str_to_expected_file (&html_str, str_data (&buff));
                    str_free(&html_str);

                    if (note->tree->data != NULL) {
                        string_t tsplx_str = {0};
                        cat_note_tsplx (&tsplx_str, &rt->sd, note->tree);

                        printf ("\n");
                        printf (ECMA_MAGENTA("TSPLX") "\n");
                        prnt_debug_string (str_data(&tsplx_str));
                        set_expected_tsplx_path (&buff, note->id);
                        print_diff_str_to_expected_file (&tsplx_str, str_data (&buff));

                        str_free (&tsplx_str);
                    }

                    printf ("\n");
                }

                if (str_len(&note->error_msg) > 0) {
                    // FIXME: Why using --blocks flag duplicates error printing
                    // here?...
                    printf (ECMA_MAGENTA("\nERRORS") "\n");
                    printf ("%s", str_data (&note->error_msg));
                    printf ("\n");
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
