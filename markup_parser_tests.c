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

    //char *test_note = full_file_read (&pool, "tests/title_and_paragraphs.psplx", NULL);
    //char *test_note = full_file_read (&pool, "tests/code.psplx", NULL);
    //char *test_note = full_file_read (&pool, "tests/lists.psplx", NULL);
    char *test_note = full_file_read (&pool, "tests/inline_tags.psplx", NULL);
    struct html_t *html = markup_to_html (&pool, test_note, "1", 0);
    printf ("%s\n", html_to_str (html, &pool, 2));
    html_destroy (html);

    //char *code = full_file_read (&pool, "static/lib/katex/katex.min.js", NULL);

    //void *my_udata = (void *) 0xdeadbeef;
    //duk_context *ctx = duk_create_heap(NULL, NULL, NULL, my_udata, duktape_custom_fatal_handler);
    //duk_push_string(ctx, code);
    //duk_push_string(ctx, "\nkatex.renderToString(\"c = \\\\pm\\\\sqrt{a^2 + b^2}\", {"
    //                "throwOnError: false"
    //                "});");
    //duk_concat(ctx, 2);

    //duk_push_string(ctx, "katex");
    //duk_compile(ctx, 0);
    //duk_call(ctx, 0);
    //printf("program result: %s\n", duk_get_string(ctx, -1));

    //duk_destroy_heap(ctx);

    return 0;
}
