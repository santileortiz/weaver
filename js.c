/*
 * Copyright (C) 2021 Santiago León O.
 */

#ifndef NO_JS
#include "lib/duk_config.h"
#include "lib/duktape.h"

static void duktape_custom_fatal_handler(void *udata, const char *msg) {
    (void) udata;  /* ignored in this case, silence warning */

    /* Note that 'msg' may be NULL. */
    fprintf(stderr, "(DUKTAPE) FATAL ERROR: %s\n", (msg ? msg : "no message"));
    fflush(stderr);
    abort();
}

duk_context *katex_ctx = NULL;

void str_cat_math_strn (string_t *str, bool is_display_mode, int len, char *expression)
{
    mem_pool_t pool = {0};
    string_t buff = {0};
    str_cat_printf (&buff, "\nkatex.renderToString(\"%.*s\", {throwOnError: false, displayMode: %s});", len, expression, is_display_mode ? "true" : "false");
    str_replace (&buff, "\\", "\\\\", NULL);

    if (katex_ctx == NULL) {
        // TODO: Put this in an embedded resource so we don't depend on the location
        // from which the executable is run.
        // :remove_relative_paths
        char *code = full_file_read (&pool, "static/lib/katex/katex.min.js", NULL);
        void *my_udata = (void *) 0xc0ffee;
        katex_ctx = duk_create_heap(NULL, NULL, NULL, my_udata, duktape_custom_fatal_handler);
        duk_eval_string_noresult(katex_ctx, code);
    }

    duk_push_string(katex_ctx, str_data(&buff));
    duk_push_string(katex_ctx, "katex");
    duk_compile(katex_ctx, 0);
    duk_call(katex_ctx, 0);

    char *html_expression = (char*) duk_get_string(katex_ctx, -1);
    str_cat_c (str, html_expression);

    str_free (&buff);
    mem_pool_destroy (&pool);
}

void js_destroy ()
{
    duk_destroy_heap(katex_ctx);
}

#else
void str_cat_math_strn (string_t *str, bool is_display_mode, int len, char *expression)
{
    str_cat_printf (str, "<span>{%s math: %s}</span>", is_display_mode ? "display" : "inline", expression);
}

void js_destroy ()
{
    return;
}

#endif
