/*
 * Copyright (C) 2021 Santiago León O.
 */

#define TOKEN_TYPES_TABLE                      \
    TOKEN_TYPES_ROW(TOKEN_TYPE_UNKNOWN)        \
    TOKEN_TYPES_ROW(TOKEN_TYPE_TITLE)          \
    TOKEN_TYPES_ROW(TOKEN_TYPE_PARAGRAPH)      \
    TOKEN_TYPES_ROW(TOKEN_TYPE_BULLET_LIST)    \
    TOKEN_TYPES_ROW(TOKEN_TYPE_NUMBERED_LIST)  \
    TOKEN_TYPES_ROW(TOKEN_TYPE_TAG)            \
    TOKEN_TYPES_ROW(TOKEN_TYPE_OPERATOR)       \
    TOKEN_TYPES_ROW(TOKEN_TYPE_SPACE)          \
    TOKEN_TYPES_ROW(TOKEN_TYPE_TEXT)           \
    TOKEN_TYPES_ROW(TOKEN_TYPE_CODE_HEADER)    \
    TOKEN_TYPES_ROW(TOKEN_TYPE_CODE_LINE)      \
    TOKEN_TYPES_ROW(TOKEN_TYPE_BLANK_LINE)     \
    TOKEN_TYPES_ROW(TOKEN_TYPE_END_OF_FILE)

#define TOKEN_TYPES_ROW(value) value,
enum psx_token_type_t {
    TOKEN_TYPES_TABLE
};
#undef TOKEN_TYPES_ROW

#define TOKEN_TYPES_ROW(value) #value,
char* psx_token_type_names[] = {
    TOKEN_TYPES_TABLE
};
#undef TOKEN_TYPES_ROW

struct psx_token_t {
    sstring_t value;

    enum psx_token_type_t type;
    int margin;
    int content_start;
    int heading_number;
    int coalesced_spaces;
    bool is_eol;
};

#define BLOCK_TYPES_TABLE                 \
    BLOCK_TYPES_ROW(BLOCK_TYPE_ROOT)      \
    BLOCK_TYPES_ROW(BLOCK_TYPE_LIST)      \
    BLOCK_TYPES_ROW(BLOCK_TYPE_LIST_ITEM) \
    BLOCK_TYPES_ROW(BLOCK_TYPE_HEADING)   \
    BLOCK_TYPES_ROW(BLOCK_TYPE_PARAGRAPH) \
    BLOCK_TYPES_ROW(BLOCK_TYPE_CODE)

#define BLOCK_TYPES_ROW(value) value,
enum psx_block_type_t {
    BLOCK_TYPES_TABLE
};
#undef BLOCK_TYPES_ROW

#define BLOCK_TYPES_ROW(value) #value,
char* psx_block_type_names[] = {
    BLOCK_TYPES_TABLE
};
#undef BLOCK_TYPES_ROW

struct psx_block_t {
    enum psx_block_type_t type;
    int margin;

    string_t inline_content;

    int heading_number;

    // List
    enum psx_token_type_t list_type;

    // Number of characters from the start of a list marker to the start of the
    // content. Includes al characters of the list marker.
    // "    -    C" -> 5
    // "   10.   C" -> 6
    int content_start;

    // Each non-leaf block contains a linked list of children
    struct psx_block_t *block_content;
    struct psx_block_t *block_content_end;

    struct splx_node_t *data;

    struct psx_block_t *next;
};

struct block_allocation_t {
    mem_pool_t *pool;

    // TODO: Actually use this free list, right now we only add stuff, but never
    // use it.
    struct psx_block_t *blocks_fl;
};

