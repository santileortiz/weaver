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

void set_expected_subtype_path (string_t *str, char *name, char *extension)
{
    str_set_printf (str, TESTS_DIR "/%s.%s", name, extension);
}

#define TEST_EXTENSION_TTL "ttl"
#define TEST_EXTENSION_CANONICAL "canonical.tsplx"

int main(int argc, char** argv)
{
    mem_pool_t pool = {0};

    string_t buff = {0};
    str_pool (&pool, &buff);

    string_t error_msg = {0};

    STACK_ALLOCATE (struct test_ctx_t , t);

    STACK_ALLOCATE (struct cli_ctx_t, cli_ctx);
    bool ttl_out = get_cli_bool_opt_ctx (cli_ctx, "--ttl", argv, argc);
    bool canonical_out = get_cli_bool_opt_ctx (cli_ctx, "--canonical", argv, argc);
    bool dump_out = get_cli_bool_opt_ctx (cli_ctx, "--dump", argv, argc);
    bool tokens_out = get_cli_bool_opt_ctx (cli_ctx, "--tokens", argv, argc);
    bool no_output = get_cli_bool_opt_ctx (cli_ctx, "--none", argv, argc);
    t->show_all_children = get_cli_bool_opt_ctx (cli_ctx, "--full", argv, argc);
    char *test_name = get_cli_no_opt_arg (cli_ctx, argv, argc);

    if (test_name == NULL) {
        struct get_test_names_clsr_t clsr = {0};
        clsr.pool = &pool;
        iterate_dir (TESTS_DIR, get_test_names, &clsr);

        LINKED_LIST_FOR (struct char_ll_t*, test_name, clsr.test_names) {
            // Don't allocate these files into pool so we free them on each test
            // execution. This avoids keeping all in memory until the end.
            set_expected_subtype_path (&buff, test_name->v, TEST_EXTENSION_TTL);
            char *expected_ttl = NULL;
            if (path_exists (str_data(&buff))) {
                expected_ttl = full_file_read (NULL, str_data(&buff), NULL);
            }

            set_expected_subtype_path (&buff, test_name->v, TEST_EXTENSION_CANONICAL);
            char *expected_canonical = NULL;
            if (path_exists (str_data(&buff))) {
                expected_canonical = full_file_read (NULL, str_data(&buff), NULL);
            }

            if (expected_ttl == NULL || expected_canonical == NULL) {
                string_t missing_info = {0};

                if (expected_canonical == NULL) {
                    str_cat_c (&missing_info, "!canonical");
                }

                if (expected_ttl == NULL) {
                    if (str_len (&missing_info) && str_last (&missing_info) != ' ') {
                        str_cat_c (&missing_info, " ");
                    }

                    str_cat_c (&missing_info, "!ttl");
                }

                test_push (t, "%s " ECMA_YELLOW("(%s)"), test_name->v, str_data(&missing_info));

                str_free (&missing_info);
            } else {
                test_push (t, "%s", test_name->v);
            }

            char *tsplx = get_test_file (&pool, test_name->v, TSPLX_EXTENSION, NULL);

            bool no_crash = true;
            bool success = true;
            struct splx_data_t sd = {0};
            CRASH_TEST_AND_RUN (no_crash, t->error,
                 success = tsplx_parse_str_name (&sd, tsplx, &error_msg);
            );

            if (no_crash) {
                bool parsing_failed = false;
                test_push (t, "Parsing");
                if (!test_bool (t, success && str_len(&error_msg) == 0)) {
                    test_error_c (t, str_data(&error_msg));
                    str_set (&error_msg, "");
                    parsing_failed = true;
                }

                if (!parsing_failed && expected_canonical != NULL) {
                    str_set (&buff, "");
                    test_push (t, "Canonical output matches");
                    str_cat_splx_canonical (&buff, &sd, sd.root);
                    test_str (t, str_data(&buff), expected_canonical);

                    string_t crash_error_msg = {0};
                    struct splx_data_t tmp = {0};
                    CRASH_TEST_AND_RUN (no_crash, &crash_error_msg,
                         success = tsplx_parse_str_name (&tmp, expected_canonical, &error_msg);
                    );
                    test_push (t, "Canonical parsing");
                    if (!test_bool (t, no_crash && success && str_len(&error_msg) == 0)) {
                        if (no_crash) {
                            test_error_c (t, str_data(&error_msg));
                        } else {
                            test_error_c (t, str_data(&crash_error_msg));
                            success = false;
                        }

                        str_set (&error_msg, "");
                    }

                    if (success) {
                        str_set (&buff, "");
                        test_push (t, "Canonical idempotence");
                        str_cat_splx_canonical (&buff, &tmp, tmp.root);
                        test_str (t, str_data(&buff), expected_canonical);
                    }

                    free (expected_canonical);
                }

                if (!parsing_failed && expected_ttl != NULL) {
                    str_set (&buff, "");
                    test_push (t, "Matches expected turtle");
                    str_cat_splx_ttl (&buff, &sd, sd.root);
                    test_str (t, str_data(&buff), expected_ttl);
                    free (expected_ttl);
                }
            }

            test_pop(t, no_crash);
        }

    } else {
        char *tsplx = get_test_file (&pool, test_name, TSPLX_EXTENSION, NULL);

        if (tsplx != NULL) {
            struct splx_data_t sd = {0};
            bool success = tsplx_parse_str_name (&sd, tsplx, &error_msg);

            if (ttl_out) {
                print_splx_ttl (&sd, sd.root);

            } else if (canonical_out) {
                print_splx_canonical (&sd, sd.root);

            } else if (dump_out) {
                print_splx_dump (&sd, sd.root);

            } else if (tokens_out) {
                print_tsplx_tokens (tsplx);

            } else if (no_output) {
                // do nothing

            } else {
                printf (ECMA_MAGENTA("TSPLX") "\n");
                prnt_debug_string (tsplx);
                printf ("\n");

                if (success) {
                    string_t tsplx_formatted = {0};
                    str_cat_splx_canonical (&tsplx_formatted, &sd, sd.root);
                    printf (ECMA_MAGENTA("CANONICAL") "\n");
                    prnt_debug_string (str_data(&tsplx_formatted));

                    set_expected_subtype_path (&buff, test_name, TEST_EXTENSION_CANONICAL);
                    print_diff_str_to_expected_file (&tsplx_formatted, str_data (&buff));
                    str_free (&tsplx_formatted);
                }

                printf ("\n");

                if (success) {
                    string_t tsplx_formatted = {0};
                    str_cat_splx_ttl (&tsplx_formatted, &sd, sd.root);
                    printf (ECMA_MAGENTA("TTL") "\n");
                    prnt_debug_string (str_data(&tsplx_formatted));

                    set_expected_subtype_path (&buff, test_name, TEST_EXTENSION_TTL);
                    print_diff_str_to_expected_file (&tsplx_formatted, str_data (&buff));
                    str_free (&tsplx_formatted);
                }

                if (str_len(&error_msg) > 0) {
                    printf (ECMA_MAGENTA("\nERRORS") "\n");
                    printf ("%s", str_data (&error_msg));
                }

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
