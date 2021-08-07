/*
 * Copyright (C) 2021 Santiago León O.
 */

struct note_runtime_t* rt_get ()
{
    return &__g_note_runtime;
}

void rt_destroy (struct note_runtime_t *rt)
{
    mem_pool_destroy (&rt->pool);
}

bool rt_parse_to_html (struct note_runtime_t *rt, struct note_t *note, string_t *error_msg)
{
    bool success = true;
    mem_pool_t pool = {0};
    struct html_t *html = markup_to_html (&pool, str_data(&note->path), str_data(&note->psplx), note->id, 0, error_msg);
    if (html == NULL) {
        success = false;
    }

    if (success) {
        str_set (&note->html, "");
        str_cat_html (&note->html, html, 2);
        note->is_html_valid = true;
    }

    mem_pool_destroy (&pool);

    return success;
}
