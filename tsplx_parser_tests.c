/*
 * Copyright (C) 2021 Santiago León O.
 */

#include "common.h"
#include "binary_tree.c"
#include "test_logger.c"
#include "cli_parser.c"

#include "tsplx_parser.c"

#include "testing.c"

#define TSPLX_EXTENSION "tsplx"

struct char_ll_t {
    char *v;
    struct char_ll_t *next;
};

struct get_test_names_clsr_t {
    mem_pool_t *pool;
    struct char_ll_t *test_names;
};

ITERATE_DIR_CB(get_test_names)
{
    string_t buff = {0};
    struct get_test_names_clsr_t *clsr = (struct get_test_names_clsr_t*)data;

    if (!is_dir) {
        char *extension = get_extension (fname);
        if (extension != NULL && strcmp (extension, TSPLX_EXTENSION) == 0) {
            size_t basename_len = 0;
            char *p = fname + strlen (fname) - 1;
            while (p != fname && *p != '/') {
                basename_len++;
                p--;
            }
            p++; // advance '/'

            strn_set (&buff, p, basename_len - 1/*.*/ - strlen(TSPLX_EXTENSION));

            char *subtype = get_extension (str_data(&buff));
            if (subtype == NULL) {
                LINKED_LIST_PUSH_NEW (clsr->pool, struct char_ll_t, clsr->test_names, new_test_name);
                new_test_name->v = pom_strdup (clsr->pool, str_data(&buff));
            }
        }
    }

    str_free (&buff);
}

void set_expected_subtype_path (string_t *str, char *name, char *subtype)
{
    str_set_printf (str, TESTS_DIR "/%s.%s.tsplx", name, subtype);
}

char* get_subtype_file (mem_pool_t *pool, char *name, char *subtype, size_t *fsize)
{
    char *f = NULL;

    string_t path = {0};
    str_set_printf (&path, TESTS_DIR "/%s.%s.tsplx", name, subtype);

    if (path_exists (str_data(&path))) {
        f = full_file_read (pool, str_data(&path), fsize);
    }

    str_free (&path);
    return f;
}

#define TSPLX_EXPANDED "expanded"

int main(int argc, char** argv)
{
    mem_pool_t pool = {0};

    string_t buff = {0};
    str_pool (&pool, &buff);

    string_t error_msg = {0};

    struct test_ctx_t _t = {0};
    struct test_ctx_t *t = &_t;

    char *test_name = get_cli_no_opt_arg (argv, argc);
    bool expanded_out = get_cli_bool_opt ("--expanded", argv, argc);
    bool tokens_out = get_cli_bool_opt ("--tokens", argv, argc);
    bool no_output = get_cli_bool_opt ("--none", argv, argc);
    t->show_all_children = get_cli_bool_opt ("--full", argv, argc);

    if (test_name == NULL) {
        struct get_test_names_clsr_t clsr = {0};
        clsr.pool = &pool;
        iterate_dir (TESTS_DIR, get_test_names, &clsr);

        LINKED_LIST_FOR (struct char_ll_t*, test_name, clsr.test_names) {
            // Don't allocate these files into pool so we free them on each test
            // execution. This avoids keeping all in memory until the end.
            set_expected_subtype_path (&buff, test_name->v, TSPLX_EXPANDED);
            char *expected_expanded = NULL;
            if (path_exists (str_data(&buff))) {
                expected_expanded = full_file_read (NULL, str_data(&buff), NULL);
            }

            if (expected_expanded == NULL) {
                test_push (t, "%s " ECMA_YELLOW("(No expected)"), test_name->v);
            } else {
                test_push (t, "%s", test_name->v);
            }

            char *tsplx = get_test_file (&pool, test_name->v, TSPLX_EXTENSION, NULL);

            struct splx_data_t sd = {0};
            bool success = tsplx_parse_str_name (&sd, tsplx, "tsplx-test", &error_msg);
            test_push (t, "Parsing");
            if (!test_bool (t, success && str_len(&error_msg) == 0)) {
                test_error_c (t, str_data(&error_msg));
            }

            if (expected_expanded != NULL) {
                str_set (&buff, "");
                test_push (t, "Matches expected expanded");
                str_cat_splx_expanded (&buff, &sd, sd.root);
                test_str (t, str_data(&buff), expected_expanded);
                free (expected_expanded);
            }

            parent_test_pop(t);
        }

    } else {
        char *tsplx = get_test_file (&pool, test_name, TSPLX_EXTENSION, NULL);

        if (tsplx != NULL) {
            struct splx_data_t sd = {0};
            tsplx_parse_str_name (&sd, tsplx, "tsplx-test", &error_msg);

            if (expanded_out) {
                print_splx_expanded (&sd, sd.root);

            } else if (tokens_out) {
                print_tsplx_tokens (tsplx);

            } else if (no_output) {
                // do nothing

            } else {
                string_t tsplx_expanded = {0};
                str_cat_splx_expanded (&tsplx_expanded, &sd, sd.root);

                printf (ECMA_MAGENTA("TSPLX") "\n");
                prnt_debug_string (tsplx);
                printf ("\n");

                printf (ECMA_MAGENTA("EXPANDED") "\n");
                prnt_debug_string (str_data(&tsplx_expanded));

                if (str_len(&error_msg) > 0) {
                    printf (ECMA_MAGENTA("\nERRORS") "\n");
                    printf ("%s", str_data (&error_msg));
                }

                set_expected_subtype_path (&buff, test_name, TSPLX_EXPANDED);
                print_diff_str_to_expected_file (&tsplx_expanded, str_data (&buff));

                str_free (&tsplx_expanded);
            }

        } else {
            printf ("Test file not found\n");
        }
    }

    printf ("%s", str_data(&t->result));

    test_ctx_destroy (t);
    mem_pool_destroy (&pool);
    return 0;
}
