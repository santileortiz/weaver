/* Stub for 'PROCESS_NOTE_'
char* markup_to_html (mem_pool_t *pool_out, char *path, char *markup, char *id, string_t *error_msg)
{
    mem_pool_t _pool_l = {0};
    mem_pool_t *pool_l = &_pool_l;

    struct note_t _note = {0};
    struct note_t *note = &_note;

    struct psx_parser_ctx_t _ctx = {0};
    struct psx_parser_ctx_t *ctx = &_ctx;
    ctx->id = id;
    ctx->path = path;
    ctx->error_msg = error_msg;

    struct block_allocation_t _ba = {0};
    struct block_allocation_t *ba = &_ba;
    ba->pool = pool_l;


    PROCESS_NOTE_PARSE


    PROCESS_NOTE_CREATE_LINKS


    PROCESS_NOTE_USER_CALLBACKS


    PROCESS_NOTE_GENERATE_HTML


    PROCESS_NOTE_HANDLE_ERRORS


    mem_pool_destroy (&_pool_l);

    char *html_out = pom_strndup (pool_out, str_data(&note->html), str_len(&note->html));
    return html_out;
}
*/

#define PROCESS_NOTE_PARSE \
    note->tree = parse_note_text (pool_l, path, markup, error_msg); \
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
    
#define PROCESS_NOTE_HANDLE_ERRORS \
    bool has_output = false; \
    if (error_msg != NULL && str_len(&note->error_msg) > 0) { \
        if (has_output) { \
            str_cat_printf (error_msg, "\n"); \
        } \
 \
        str_cat_printf (error_msg, "%s - " ECMA_DEFAULT("%s\n") "%s", str_data(&note->path), str_data(&note->title), str_data(&note->error_msg)); \
        has_output = true; \
    } \
    
