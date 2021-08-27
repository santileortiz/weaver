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

void rt_process_notes (struct note_runtime_t *rt, string_t *error_msg)
{
    struct psx_parser_ctx_t ctx = {0};
    ctx.pool = &rt->pool;
    ctx.error_msg = error_msg;

    bool has_output = false;
    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            curr_note->tree = parse_note_text (&rt->pool, str_data(&curr_note->path), str_data(&curr_note->psplx), &curr_note->error_msg);
        }
    }

    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            if (curr_note->tree != NULL) {
                ctx.id = curr_note->id;
                ctx.path = str_data(&curr_note->path);
                ctx.error_msg = &curr_note->error_msg;

                psx_create_links (&ctx, &curr_note->tree);
            }
        }
    }

    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            if (curr_note->tree != NULL) {
                ctx.id = curr_note->id;
                ctx.path = str_data(&curr_note->path);
                ctx.error_msg = &curr_note->error_msg;

                psx_block_tree_user_callbacks (&ctx, &curr_note->tree);
            }
        }
    }

    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            if (!curr_note->error) {
                ctx.id = curr_note->id;
                ctx.path = str_data(&curr_note->path);
                ctx.error_msg = &curr_note->error_msg;

                // HTML generation should not be allocating anything else
                // besides html nodes, so it should be possible to pass NULL as
                // pool and don't crash. If this causes a crash, we are doing
                // something that wasn't intended.
                ctx.pool = NULL;

                // Parse to html but don't keep the full html tree, only store the
                // resulting html string. So far the HTML tree hasn't been necesary.
                mem_pool_t html_pool = {0};

                struct html_t *html = html_new (&html_pool, "div");
                html_element_attribute_set (html, html->root, "id", curr_note->id);
                block_tree_to_html (&ctx, html, curr_note->tree, html->root);

                if (html != NULL) {
                    str_set (&curr_note->html, "");
                    str_cat_html (&curr_note->html, html, 2);
                    curr_note->is_html_valid = true;

                } else {
                    curr_note->error = true;
                }

                mem_pool_destroy (&html_pool);
            }

            if (error_msg != NULL && str_len(&curr_note->error_msg) > 0) {
                if (has_output) {
                    str_cat_printf (error_msg, "\n");
                }

                str_cat_printf (error_msg, "%s - " ECMA_DEFAULT("%s\n") "%s", str_data(&curr_note->path), str_data(&curr_note->title), str_data(&curr_note->error_msg));
                has_output = true;
            }
        }
    }
}
