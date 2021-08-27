/*
 * Copyright (C) 2021 Santiago León O.
 */

#include "common.h"
#include "binary_tree.c"
#include "test_logger.c"
#include "cli_parser.c"

#define MARKUP_PARSER_IMPL
#include "note_runtime.h"
#include "markup_parser.h"

#include "note_runtime.c"

#define TESTS_DIR "tests"

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

            LINKED_LIST_PUSH_NEW (&rt->pool, struct note_t, rt->notes, new_note);
            new_note->id = pom_strndup (&rt->pool, p, basename_len - 1/*.*/ - strlen(PSPLX_EXTENSION));
            id_to_note_tree_insert (&rt->notes_by_id, new_note->id, new_note);

            str_pool (&rt->pool, &new_note->error_msg);
            str_set_pooled (&rt->pool, &new_note->path, fname);

            size_t source_len;
            char *source = full_file_read (NULL, fname, &source_len);
            str_pool (&rt->pool, &new_note->psplx);
            strn_set (&new_note->psplx, source, source_len);
            free (source);

            str_pool (&rt->pool, &new_note->title);
            if (!parse_note_title (fname, str_data(&new_note->psplx), &new_note->title, &new_note->error_msg)) {
                new_note->error = true;
                printf ("%s", str_data(&new_note->error_msg));

            } else {
                title_to_note_tree_insert (&rt->notes_by_title, &new_note->title, new_note);
            }

            str_pool (&rt->pool, &new_note->html);
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

char* get_expected_html (mem_pool_t *pool, char *note_id, size_t *fsize)
{
    char *expected_html = NULL;

    string_t expected_html_path = {0};
    str_set_printf (&expected_html_path, TESTS_DIR "/%s.html", note_id);

    if (path_exists (str_data(&expected_html_path))) {
        expected_html = full_file_read (pool, str_data(&expected_html_path), fsize);
    }

    str_free (&expected_html_path);
    return expected_html;
}

int main(int argc, char** argv)
{
    mem_pool_t pool = {0};

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

    rt_process_notes (rt, NULL);

    if (note_id == NULL) {
        LINKED_LIST_FOR (struct note_t*, note, rt->notes) {
            // Don't allocate these files into pool so we free them on each test
            // execution. This avoids keeping all in memory until the end.
            char *expected_html = get_expected_html (NULL, note->id, NULL);

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
                if (str_len(&note->error_msg) > 0) printf ("\n");

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

                    printf (ECMA_MAGENTA("RESULTING HTML") "\n");
                    prnt_debug_string (str_data(&note->html));

                    char *expected_html = get_expected_html (&pool, note->id, NULL);
                    if (expected_html != NULL && strcmp(str_data(&note->html), expected_html) != 0) {

                        char *tmp_fname = "html_test.html";
                        full_file_write (str_data(&note->html), str_len(&note->html), tmp_fname);

                        string_t cmd = {0};

                        printf (ECMA_MAGENTA("\nHTML DIFF TO EXPECTED") "\n");
                        str_set_printf (&cmd, "git diff --no-index tests/%s.html %s", note->id, tmp_fname);
                        printf ("%s\n", str_data(&cmd));
                        system (str_data(&cmd));

                        unlink (tmp_fname);
                    }

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
    return 0;
}
