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

struct note_t* rt_get_note_by_title (string_t *title)
{
    struct note_t *note = NULL;
    struct note_runtime_t *rt = rt_get ();
    if (rt != NULL) {
        note = title_to_note_get (&rt->notes_by_title, title);
    } else {
        // TODO: There is no runtime in the parser state. Is there any reason we
        // would use the parser without a runtime?. If that's the case, then tag
        // evaluations like this one, that reffer to other notes won't work.
    }

    return note;
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
