/*
 * Copyright (C) 2022 Santiago León O.
 */

#if !defined(TSPLX_PARSER_H)

BINARY_TREE_NEW (ptr_set, void*, void*, (a==b) ? 0 : (a<b ? -1 : 1))
BINARY_TREE_NEW (cstr_to_splx_node_map, char*, struct splx_node_t*, strcmp(a, b))
BINARY_TREE_NEW (cstr_to_splx_node_list_map, char*, struct splx_node_list_t*, strcmp(a, b))

#define SPLX_NODE_TYPES_TABLE                         \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_UNKNOWN)        \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_SOFT_REFERENCE) \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_STRING)         \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_DOUBLE)         \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_INTEGER)        \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_URI)            \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_OBJECT)         \

#define SPLX_NODE_TYPE_ROW(value) value,
enum splx_node_type_t {
    SPLX_NODE_TYPES_TABLE
};
#undef SPLX_NODE_TYPE_ROW

#define SPLX_NODE_TYPE_ROW(value) #value,
char* splx_node_type_names[] = {
    SPLX_NODE_TYPES_TABLE
};
#undef SPLX_NODE_TYPE_ROW

struct splx_node_t {
    enum splx_node_type_t type;
    string_t str;

    struct cstr_to_splx_node_list_map_t attributes;

    struct splx_node_list_t *floating_values;
    struct splx_node_list_t *floating_values_end;

    bool uri_formatted_identifier;
};

struct splx_node_list_t {
    struct splx_node_t *node;
    struct splx_node_list_t *next;
};

struct splx_data_t {
    mem_pool_t pool;

    struct cstr_to_splx_node_map_t nodes;
    struct splx_node_t *entities;

    // The root is different than "entities" in that it has the actual nesting
    // structure of the whole object parsed, entities instead is just a flat
    // node containing all entities as floating objects. Some entities without
    // identifier may only be reached through an iterative traversal starting at
    // the root, while it's guaranteed that all entities are reachable at one
    // level depth through the entities node.
    struct splx_node_t *root;
};

/////////
// API
struct splx_node_t* splx_node_new (struct splx_data_t *sd);

struct splx_node_list_t* splx_node_get_attributes (struct splx_node_t *node, char *attr);
struct splx_node_t* splx_node_get_attribute (struct splx_node_t *node, char *attr);
string_t* splx_node_get_name (struct splx_node_t *node);
struct splx_node_t* splx_get_node_by_name(struct splx_data_t *sd, char *name);

#define TSPLX_PARSER_H
#endif
