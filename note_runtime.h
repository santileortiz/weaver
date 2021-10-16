/*
 * Copyright (C) 2021 Santiago León O.
 */

BINARY_TREE_NEW (id_to_note, char*, struct note_t*, strcmp(a,b));

BINARY_TREE_NEW (title_to_note, string_t*, struct note_t*, strcmp(str_data(a),str_data(b)));

struct note_runtime_t {
    mem_pool_t pool;
    struct note_t *notes;

    int title_note_ids_len;
    char **title_note_ids;

    struct block_allocation_t block_allocation;

    struct id_to_note_tree_t notes_by_id;
    struct title_to_note_tree_t notes_by_title;
} __g_note_runtime;

struct note_runtime_t* rt_get ();
struct note_t* rt_get_note_by_title (string_t *title);
struct note_t* rt_get_note_by_id (char *id);
void rt_link_notes (struct note_t *src, struct note_t *tgt);

#define CFG_TITLE_NOTES "title-notes"
#define CFG_SOURCE_DIR "source-dir"
#define CFG_TARGET_DIR "target-dir"
