/*
 * Copyright (C) 2021 Santiago León O.
 */

struct note_t {
    string_t path;

    char *id;
    string_t title;
    string_t psplx;

    bool is_html_valid;
    string_t html;

    bool error;
    string_t error_msg;

    struct note_t *next;
};

BINARY_TREE_NEW (id_to_note, char*, struct note_t*, strcmp(a,b));

BINARY_TREE_NEW (title_to_note, string_t*, struct note_t*, strcmp(str_data(a),str_data(b)));

struct note_runtime_t {
    mem_pool_t pool;
    struct note_t *notes;

    struct id_to_note_tree_t notes_by_id;
    struct title_to_note_tree_t notes_by_title;
} __g_note_runtime;

struct note_runtime_t* rt_get ();
bool rt_parse_to_html (struct note_runtime_t *rt, struct note_t *note, string_t *error_msg);
