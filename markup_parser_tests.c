/*
 * Copyright (C) 2021 Santiago León O.
 */

#include "common.h"
#include "binary_tree.c"

#define MARKUP_PARSER_IMPL
#include "markup_parser.h"

int main(int argc, char** argv)
{
    mem_pool_t pool = {0};

    char *test_note = full_file_read (&pool, "tests/code_blocks.psplx", NULL);
    char *res = markup_to_html (&pool, test_note, "1", 0);
    printf ("%s\n", res);

    return 0;
}
