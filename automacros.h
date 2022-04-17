/* Stub for 'PROCESS_NOTE_'
char* markup_to_html (mem_pool_t *pool_out, struct file_vault_t *vlt, char *path, char *markup, char *id, string_t *error_msg)
{
    STACK_ALLOCATE (mem_pool_t, pool_l);

    STACK_ALLOCATE (struct note_t, note);
    note->id = id;

    STACK_ALLOCATE (struct psx_parser_ctx_t, ctx);
    ctx->id = id;
    ctx->path = path;
    ctx->vlt = vlt;
    ctx->error_msg = error_msg;

    STACK_ALLOCATE (struct block_allocation_t, ba);
    ba->pool = pool_l;


    PROCESS_NOTE_PARSE


    PROCESS_NOTE_CREATE_LINKS


    PROCESS_NOTE_USER_CALLBACKS


    PROCESS_NOTE_GENERATE_HTML

    mem_pool_destroy (&_pool_l);

    char *html_out = pom_strndup (pool_out, str_data(&note->html), str_len(&note->html));
    return html_out;
}
*/

#define PROCESS_NOTE_PARSE \
    note->tree = parse_note_text (pool_l, ctx, markup); \
    if (note->tree == NULL) { \
        note->error = true; \
    } \
    
#define PROCESS_NOTE_CREATE_LINKS \
    if (!note->error) { \
        psx_create_links (ctx, &note->tree); \
    } \
    
#define PROCESS_NOTE_USER_CALLBACKS \
    if (!note->error) { \
        psx_block_tree_user_callbacks (ctx, ba, &note->tree); \
    } \
    
#define PROCESS_NOTE_GENERATE_HTML \
    if (!note->error) { \
        struct html_t *html = html_new (pool_l, "div"); \
        html_element_attribute_set (html, html->root, "id", note->id); \
        block_tree_to_html (ctx, html, note->tree, html->root); \
 \
        if (html != NULL) { \
            str_set (&note->html, ""); \
            str_cat_html (&note->html, html, 2); \
            note->is_html_valid = true; \
 \
        } else { \
            note->error = true; \
        } \
    } \
    
