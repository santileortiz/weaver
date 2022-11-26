/* Stub for 'PROCESS_NOTE_'
char* markup_to_html (
    mem_pool_t *pool_out, struct file_vault_t *vlt, struct splx_data_t *sd,
    char *path, char *markup, char *id,
    string_t *error_msg)
{
    STACK_ALLOCATE (mem_pool_t, pool_l);

    STACK_ALLOCATE (struct note_t, note);
    note->id = id;

    STACK_ALLOCATE (struct psx_parser_ctx_t, ctx);
    ctx->id = id;
    ctx->path = path;
    ctx->vlt = vlt;
    ctx->sd = sd;
    ctx->error_msg = error_msg;

    STACK_ALLOCATE (struct block_allocation_t, ba);
    ba->pool = pool_l;


    PROCESS_NOTE_PARSE


    PROCESS_NOTE_CREATE_LINKS


    PROCESS_NOTE_USER_CALLBACKS


    PROCESS_NOTE_GENERATE_HTML

    string_t *html_out = str_pool(pool_out, "");
    if (!note->error) {
        str_cat_html (html_out, note->html, 2);
    }

    mem_pool_destroy (&_pool_l);

    return str_data(html_out);
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
        note->html = html_new (&note->pool, "div"); \
        html_element_attribute_set (note->html, note->html->root, "id", note->id); \
        block_tree_to_html (ctx, note->html, note->tree, note->html->root); \
 \
        if (note->html == NULL) { \
            note->error = true; \
        } \
    } \
    
