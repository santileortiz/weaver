/*
 * Copyright (C) 2021 Santiago León O.
 */

struct note_runtime_t* rt_get ()
{
    // For now it's impossible to not have a runtime as it's a globall variable.
    // Do we want to allow this to be a possibility?, so far it seems like you
    // always need a runtime...
    struct note_runtime_t *rt = &__g_note_runtime;
    assert (rt != NULL);

    if (rt->block_allocation.pool == NULL) rt->block_allocation.pool = &rt->pool;

    return rt;
}

void rt_destroy (struct note_runtime_t *rt)
{
    mem_pool_destroy (&rt->pool);
}

struct note_t* rt_get_note_by_title (string_t *title)
{
    struct note_runtime_t *rt = rt_get ();
    return title_to_note_get (&rt->notes_by_title, title);
}

struct note_t* rt_get_note_by_id (char *id)
{
    struct note_runtime_t *rt = rt_get ();
    return id_to_note_get (&rt->notes_by_id, id);
}

void rt_link_notes (struct note_t *src, struct note_t *tgt)
{
    struct note_runtime_t *rt = rt_get ();
    LINKED_LIST_PUSH_NEW (&rt->pool, struct note_link_t, src->links, src_link);
    src_link->note = tgt;

    LINKED_LIST_PUSH_NEW (&rt->pool, struct note_link_t, tgt->back_links, tgt_link);
    tgt_link->note = src;
}

void rt_process_note (mem_pool_t *pool_out, struct note_t *note)
{
    mem_pool_t *pool_l = pool_out;

    char *id = note->id;
    char *path = str_data(&note->path);
    char *markup = str_data(&note->psplx);

    string_t *error_msg = &note->error_msg;

    struct psx_parser_ctx_t _ctx = {0};
    struct psx_parser_ctx_t *ctx = &_ctx;
    ctx->id = id;
    ctx->path = path;
    ctx->error_msg = &note->error_msg;

    struct block_allocation_t _ba = {0};
    struct block_allocation_t *ba = &_ba;
    ba->pool = pool_l;


    PROCESS_NOTE_PARSE


    PROCESS_NOTE_CREATE_LINKS


    PROCESS_NOTE_USER_CALLBACKS


    {
        mem_pool_t _html_pool = {0};
        mem_pool_t *pool_l = &_html_pool;
        PROCESS_NOTE_GENERATE_HTML
        mem_pool_destroy (&_html_pool);
    }
}

void rt_process_notes (struct note_runtime_t *rt, string_t *error_msg)
{
    struct psx_parser_ctx_t _ctx = {0};
    struct psx_parser_ctx_t *ctx = &_ctx;
    ctx->error_msg = error_msg;

    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            mem_pool_t *pool_l = &rt->pool;
            struct note_t *note = curr_note;
            char *path = str_data(&curr_note->path);
            char *markup = str_data(&curr_note->psplx);
            error_msg = &curr_note->error_msg;

            PROCESS_NOTE_PARSE
        }
    }

    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            struct note_t *note = curr_note;
            ctx->id = curr_note->id;
            ctx->path = str_data(&curr_note->path);
            ctx->error_msg = &curr_note->error_msg;

            PROCESS_NOTE_CREATE_LINKS
        }
    }

    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            struct note_t *note = curr_note;
            struct block_allocation_t *ba = &rt->block_allocation;
            ctx->id = curr_note->id;
            ctx->path = str_data(&curr_note->path);
            ctx->error_msg = &curr_note->error_msg;

            PROCESS_NOTE_USER_CALLBACKS
        }
    }

    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            struct note_t *note = curr_note;
            ctx->id = curr_note->id;
            ctx->path = str_data(&curr_note->path);
            ctx->error_msg = &curr_note->error_msg;

            // Parse to html but don't keep the full html tree, only store the
            // resulting html string. So far the HTML tree hasn't been necesary.
            mem_pool_t _html_pool = {0};
            mem_pool_t *pool_l = &_html_pool;

            PROCESS_NOTE_GENERATE_HTML

            mem_pool_destroy (&_html_pool);

            PROCESS_NOTE_HANDLE_ERRORS
        }
    }
}
