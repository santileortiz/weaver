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

struct note_t* rt_new_note (struct note_runtime_t *rt, char *id, size_t id_len)
{
    LINKED_LIST_PUSH_NEW (&rt->pool, struct note_t, rt->notes, new_note);
    new_note->id = pom_strndup (&rt->pool, id, id_len);
    id_to_note_insert (&rt->notes_by_id, new_note->id, new_note);
    rt->notes_len++;

    return new_note;
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

void rt_link_entities (struct splx_node_t *src, struct splx_node_t *tgt, char *text, char *section)
{
    assert (src != NULL && tgt != NULL);

    struct note_runtime_t *rt = rt_get ();

    // TODO: Instead of just printing a page's backling once, we should provide
    // more context about where the link is coming from in each repetition.
    splx_node_attribute_append_once (&rt->sd, src, "link", tgt);
    splx_node_attribute_append_once (&rt->sd, tgt, "backlink", src);

    if (text != NULL || section != NULL) {
        struct splx_node_t *link_stmt = splx_statement_node_get_or_create (&rt->sd, src, "link", tgt);

        if (text != NULL) {
            splx_node_attribute_append_once_c_str (&rt->sd, link_stmt, "text", text, SPLX_NODE_TYPE_STRING);
        }

        if (section != NULL) {
            splx_node_attribute_append_once_c_str (&rt->sd, link_stmt, "section", section, SPLX_NODE_TYPE_STRING);
        }
    }
}

void rt_link_entities_by_id (char *src_id, char *tgt_id, char *text, char *section)
{
    struct note_runtime_t *rt = rt_get ();

    struct splx_node_t *src_node = splx_node_get_or_create(&rt->sd, src_id, SPLX_NODE_TYPE_OBJECT);
    struct splx_node_t *tgt_node = splx_node_get_or_create(&rt->sd, tgt_id, SPLX_NODE_TYPE_OBJECT);
    rt_link_entities (src_node, tgt_node, text, section);
}

void rt_process_note (mem_pool_t *pool_out, struct file_vault_t *vlt, struct splx_data_t *sd, struct note_t *note)
{
    mem_pool_t *pool_l = pool_out;

    STACK_ALLOCATE(struct psx_parser_ctx_t, ctx);
    ctx->id = note->id;
    ctx->sd = sd;
    ctx->path = str_data(&note->path);
    ctx->error_msg = &note->error_msg;

    STACK_ALLOCATE (struct block_allocation_t, ba);
    ba->pool = pool_l;

    char *markup = str_data(&note->psplx);

    PROCESS_NOTE_PARSE


    PROCESS_NOTE_CREATE_LINKS


    PROCESS_NOTE_USER_CALLBACKS


    {
        mem_pool_t _html_pool = {0};
        PROCESS_NOTE_GENERATE_HTML
        mem_pool_destroy (&_html_pool);
    }
}

void rt_process_notes (struct note_runtime_t *rt, string_t *error_msg_out)
{
    STACK_ALLOCATE (struct psx_parser_ctx_t, ctx);
    ctx->rt = rt;
    ctx->vlt = &rt->vlt;
    ctx->sd = &rt->sd;


    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            struct note_t *note = curr_note;
            ctx->note = curr_note;
            ctx->id = curr_note->id;
            ctx->path = str_data(&curr_note->path);
            ctx->error_msg = &curr_note->error_msg;

            //printf ("%s\n", str_data(&curr_note->path));

            mem_pool_t *pool_l = &rt->pool;
            char *markup = str_data(&curr_note->psplx);

            PROCESS_NOTE_PARSE
        }
    }

    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            struct note_t *note = curr_note;
            ctx->note = curr_note;
            ctx->id = curr_note->id;
            ctx->path = str_data(&curr_note->path);
            ctx->error_msg = &curr_note->error_msg;

            PROCESS_NOTE_CREATE_LINKS
        }
    }

    {
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            struct note_t *note = curr_note;
            ctx->note = curr_note;
            ctx->id = curr_note->id;
            ctx->path = str_data(&curr_note->path);
            ctx->error_msg = &curr_note->error_msg;

            struct block_allocation_t *ba = &rt->block_allocation;

            PROCESS_NOTE_USER_CALLBACKS
        }
    }

    {
        bool has_output = false;
        LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
            struct note_t *note = curr_note;
            ctx->note = curr_note;
            ctx->id = curr_note->id;
            ctx->path = str_data(&curr_note->path);
            ctx->error_msg = &curr_note->error_msg;

            PROCESS_NOTE_GENERATE_HTML

            if (error_msg_out != NULL && str_len(&note->error_msg) > 0) {
                if (has_output) {
                    str_cat_c (error_msg_out, "\n");
                }
                has_output = true;

                str_cat_printf (error_msg_out, ECMA_CYAN("%s\n") "%s", str_data(&note->title), str_data(&note->error_msg));
            }
        }
    }
}

void rt_queue_late_callback (struct note_t *note, struct psx_tag_t *tag, struct html_element_t *html_placeholder, psx_late_user_tag_cb_t *cb)
{
    struct note_runtime_t *rt = rt_get ();
    struct late_cb_invocation_t *invocation = mem_pool_push_struct(&rt->pool, struct late_cb_invocation_t);
    *invocation = ZERO_INIT(struct late_cb_invocation_t);

    invocation->note = note;
    invocation->tag = tag;
    invocation->html_placeholder = html_placeholder;
    invocation->cb = cb;

    LINKED_LIST_APPEND(rt->invocations, invocation);
}

// At the moment we just ensure the orphan list callbacks are invoked at last,
// while trying to preserve the previouse ordering. We may think of exposing
// this step through user configuration, maybe callbacks should have priorities
// assigned, or we may specify callback dependencies?.
static inline
int invocation_cmp (struct late_cb_invocation_t *a, struct late_cb_invocation_t *b)
{
    string_t a_tag = {0};
    str_set_sstr (&a_tag, &a->tag->token.value);

    string_t b_tag = {0};
    str_set_sstr (&b_tag, &b->tag->token.value);

    int result = 0;
    if (strcmp(str_data(&a_tag), "orphan_list") == 0 && strcmp(str_data(&b_tag), "orphan_list") != 0) {
        result = 1;
    } else if (strcmp(str_data(&a_tag), "orphan_list") != 0 && strcmp(str_data(&b_tag), "orphan_list") == 0) {
        result = -1;
    }

    return result;
}

templ_sort_stable_ll(invocation_sort,struct late_cb_invocation_t,invocation_cmp(a, b))

void rt_late_user_callbacks(struct note_runtime_t *rt)
{
    invocation_sort (&rt->invocations, -1);

    LINKED_LIST_FOR (struct late_cb_invocation_t*, curr_invocation, rt->invocations) {
        curr_invocation->cb (rt,
            curr_invocation->note,
            curr_invocation->tag,
            curr_invocation->html_placeholder);
    }
}
