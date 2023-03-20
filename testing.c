/*
 * Copyright (C) 2021 Santiago León O.
 */

#define TESTS_DIR "tests"
#define FULL_TEST_DIR "example"

char* get_test_file (mem_pool_t *pool, char *name, char *extension, size_t *fsize)
{
    char *expected = NULL;

    string_t expected_html_path = {0};
    str_set_printf (&expected_html_path, TESTS_DIR "/%s.%s", name, extension);

    if (path_exists (str_data(&expected_html_path))) {
        expected = full_file_read (pool, str_data(&expected_html_path), fsize);
    }

    str_free (&expected_html_path);
    return expected;
}

void print_diff_str_to_expected_file (string_t *result, char *expected_path)
{
    char *expected = NULL;
    if (path_exists (expected_path)) {
        expected = full_file_read (NULL, expected_path, NULL);
    }

    if (expected != NULL && strcmp(str_data(result), expected) != 0) {
        char *tmp_fname = "result";
        full_file_write (str_data(result), str_len(result), tmp_fname);

        string_t cmd = {0};
        printf (ECMA_S_CYAN(0, "\nDIFF TO EXPECTED") "\n");
        str_set_printf (&cmd, "git diff --no-index %s %s", expected_path, tmp_fname);
        printf ("%s\n", str_data(&cmd));
        system (str_data(&cmd));
        str_free (&cmd);

        unlink (tmp_fname);
    }
}
