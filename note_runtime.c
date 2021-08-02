/*
 * Copyright (C) 2021 Santiago León O.
 */

struct note_t {
    char *id;
    string_t title;
    string_t psplx;

    bool is_html_valid;
    string_t html;

    struct note_t *next;
};

BINARY_TREE_NEW (id_to_note, char*, struct note_t*, strcmp(a,b));

struct note_runtime_t {
    mem_pool_t pool;
    struct note_t *notes;

    struct id_to_note_tree_t notes_by_id;
};

void rt_destroy (struct note_runtime_t *rt)
{
    mem_pool_destroy (&rt->pool);
}

bool rt_parse_to_html (struct note_runtime_t *rt, struct note_t *note, string_t *error_msg)
{
    bool success = true;
    mem_pool_t pool = {0};
    struct html_t *html = markup_to_html (&pool, str_data(&note->psplx), note->id, 0);
    if (html == NULL) {
        success = false;
        str_cat_c (error_msg, "parsing error");
    }

    if (success) {
        str_set (&note->html, "");
        str_cat_html (&note->html, html, 2);
        note->is_html_valid = true;
    }

    mem_pool_destroy (&pool);

    return success;
}
