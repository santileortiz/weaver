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

            str_pool (&rt->pool, &new_note->title);

            size_t source_len;
            char *source = full_file_read (NULL, fname, &source_len);
            str_pool (&rt->pool, &new_note->psplx);
            strn_set (&new_note->psplx, source, source_len);
            free (source);

            str_pool (&rt->pool, &new_note->html);
        }
    }
}

void rt_init_from_dir (struct note_runtime_t *rt, char *path)
{
    rt->notes_by_id.pool = &rt->pool;
    iterate_dir (path, test_dir_iter, rt);
}
//
//////////////////////////////////////

char* full_file_read_no_trailing_newline (mem_pool_t *pool, const char *path, uint64_t *len)
{
    char *data = full_file_read (pool, path, len);

    char *p = data + *len;
    if (*p == '\0') p--;
    if (*p == '\n') *p = '\0';

    return data;
}

int main(int argc, char** argv)
{
    mem_pool_t pool = {0};
    string_t *error_msg = str_new_pooled(&pool, "");
    string_t *expected_html_path = str_new_pooled(&pool, "");

    struct test_ctx_t _t = {0};
    struct test_ctx_t *t = &_t;

    struct note_runtime_t _rt = {0};
    struct note_runtime_t *rt = &_rt;

    rt_init_from_dir (rt, TESTS_DIR);

    char *note_id = get_cli_arg_opt ("--show", argv, argc);
    char *html_out = get_cli_arg_opt ("--html", argv, argc);

    if (note_id == NULL) {
        LINKED_LIST_FOR (struct note_t*, note, rt->notes) {
            str_set_printf (expected_html_path, TESTS_DIR "/%s.html", note->id);
            bool has_expected_html = path_exists (str_data(expected_html_path));

            if (!has_expected_html) {
                test_push (t, "%s " ECMA_YELLOW("(No HTML)"), note->id);
            } else {
                test_push (t, "%s", note->id);
            }

            rt_parse_to_html (rt, note, error_msg);

            test_push (t, "Source parsing");
            if (!test_bool (t, note->is_html_valid)) {
                test_error_c (t, str_data(error_msg));
            }

            if (has_expected_html) {
                size_t expected_html_len;
                char *expected_html = full_file_read_no_trailing_newline (NULL, str_data(expected_html_path), &expected_html_len);

                test_push (t, "Matches expected HTML");
                test_str (t, str_data(&note->html), expected_html);

                free (expected_html);
            }

            parent_test_pop(t);
        }

    } else {
        struct note_t *note = id_to_note_get (&rt->notes_by_id, note_id);

        if (note != NULL) {
            rt_parse_to_html (rt, note, error_msg);

            if (html_out == NULL) {
                printf (ECMA_MAGENTA("PSPLX") "\n%s\n", str_data(&note->psplx));
                printf (ECMA_MAGENTA("HTML") "\n%s\n", str_data(&note->html));

            } else {
                full_file_write (str_data(&note->html), str_len(&note->html), html_out);
            }

        } else {
            printf ("Note id not found\n");
        }
    }

    printf ("%s", str_data(&t->result));

    mem_pool_destroy (&rt->pool);
    test_ctx_destroy (t);
    mem_pool_destroy (&pool);
    return 0;
}
