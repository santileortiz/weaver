/*
 * Copyright (C) 2021 Santiago León O.
 */

#include "tsplx_parser.h"

BINARY_TREE_NEW (id_to_note, char*, struct note_t*, strcmp(a,b));
BINARY_TREE_NEW (title_to_note, string_t*, struct note_t*, strcmp(str_data(a),str_data(b)));

// :late_tag_api
struct psx_tag_t;
struct note_runtime_t;
#define PSX_LATE_USER_TAG_CB(funcname) void funcname(struct note_runtime_t *rt, struct note_t *note, struct psx_tag_t *tag, struct html_element_t *html_placeholder)
typedef PSX_LATE_USER_TAG_CB(psx_late_user_tag_cb_t);
BINARY_TREE_NEW (psx_late_user_tag_cb, char*, psx_late_user_tag_cb_t*, strcmp(a, b));
void psx_populate_internal_late_cb_tree (struct psx_late_user_tag_cb_t *tree);

struct late_cb_invocation_t {
    struct note_t *note;
    struct psx_tag_t *tag;
    struct html_element_t *html_placeholder;
    psx_late_user_tag_cb_t *cb;

    struct late_cb_invocation_t *next;
};

struct note_runtime_t {
    mem_pool_t pool;
    int notes_len;
    struct note_t *notes;
    struct splx_data_t *metadata;

    int title_note_ids_len;
    char **title_note_ids;

    bool is_public;
    int private_types_len;
    char **private_types;

    struct block_allocation_t block_allocation;

    struct id_to_note_t notes_by_id;
    struct title_to_note_t notes_by_title;

    struct file_vault_t vlt;
    struct splx_data_t sd;

    struct psx_late_user_tag_cb_t user_late_cb_tree;
    LINKED_LIST_DECLARE(struct late_cb_invocation_t,invocations);

    int next_virtual_id;
} __g_note_runtime;

struct note_t* rt_new_note (struct note_runtime_t *rt, char *id, size_t id_len);
struct note_runtime_t* rt_get ();
struct note_t* rt_get_note_by_title (string_t *title);
struct note_t* rt_get_note_by_id (char *id);
void rt_link_entities_by_id (char *src_id, char *tgt_id, char *text, char *section);
void rt_link_entities (struct splx_node_t *src, struct splx_node_t *tgt, char *text, char *section);
void rt_queue_late_callback (struct note_t *note, struct psx_tag_t *tag, struct html_element_t *html_placeholder, psx_late_user_tag_cb_t *cb);

#define CFG_TARGET_DIR "target-dir"
#define CFG_TITLE_NOTES "title-notes"
#define CFG_PUBLIC_TITLE_NOTES "public-title-notes"
#define CFG_PRIVATE_TYPES "private-types"
