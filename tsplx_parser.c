/*
 * Copyright (C) 2021 Santiago León O.
 */

#include "tsplx_parser.h"

void splx_destroy (struct splx_data_t *sd)
{
    mem_pool_destroy (&sd->pool);
}

// CAUTION: DO NOT MODIFY THE RETURNED STRING! It's used as index to the nodes
// map.
char* splx_get_node_id_str (struct splx_data_t *sd, char *id)
{
    // Add the predicate to the nodes map, but don't allocate an
    // actual node yet. So far it's only being used as predicate,
    // it's possible we won't need to create a node because no one
    // is using it as an object or subject.
    char *node_id_str = NULL;
    struct cstr_to_splx_node_map_node_t *predicate_tree_node = NULL;
    if (!cstr_to_splx_node_map_lookup (&sd->nodes, id, &predicate_tree_node)) {
        // TODO: Should use a string pool...
        // :string_pool
        node_id_str = pom_strdup (&sd->pool, id);
        cstr_to_splx_node_map_insert (&sd->nodes, node_id_str, NULL);
    } else {
        node_id_str = predicate_tree_node->key;
    }

    assert (node_id_str != NULL);
    return node_id_str;
}

char* splx_get_node_id (struct splx_data_t *sd, struct splx_node_t *node)
{
    return splx_get_node_id_str (sd, str_data(&node->str));
}

#define TSPLX_TOKEN_TYPES_TABLE                        \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_UNKNOWN)          \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_COMMENT)          \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_EMPTY_LINE)       \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_IDENTIFIER)       \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_VARIABLE)         \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_STRING)           \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_MULTILINE_STRING) \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_URI)              \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_NUMBER)           \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_SOFT_REFERENCE)   \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_OPERATOR)         \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_EOF)              \

#define TOKEN_TYPES_ROW(value) value,
enum tsplx_token_type_t {
    TSPLX_TOKEN_TYPES_TABLE
};
#undef TOKEN_TYPES_ROW

#define TOKEN_TYPES_ROW(value) #value,
char* tsplx_token_type_names[] = {
    TSPLX_TOKEN_TYPES_TABLE
};
#undef TOKEN_TYPES_ROW

struct tsplx_token_t {
    sstring_t value;
    enum tsplx_token_type_t type;
};

struct tsplx_parser_state_t {
    char *str;
    char *pos;
    bool is_eof;

    int line_number;
    int column_number;

    struct tsplx_token_t token;

    bool error;
    string_t *error_msg;
};

void tps_init (struct tsplx_parser_state_t *tps, char *str, string_t *error_msg)
{
    tps->str = str;
    tps->pos = str;
    tps->error_msg = error_msg;
}

GCC_PRINTF_FORMAT(2, 3)
void tps_error (struct tsplx_parser_state_t *tps, char *format, ...)
{
    if (tps->error_msg == NULL) return;

    str_cat_printf (tps->error_msg, "%i:%i " ECMA_RED("error: "), tps->line_number, tps->column_number);

    PRINTF_INIT (format, size, args);
    size_t old_len = str_len(tps->error_msg);
    str_maybe_grow (tps->error_msg, str_len(tps->error_msg) + size - 1, true);
    PRINTF_SET (str_data(tps->error_msg) + old_len, size, format, args);

    str_cat_c (tps->error_msg, "\n");
    tps->error = true;
}

GCC_PRINTF_FORMAT(2, 3)
void tps_warning (struct tsplx_parser_state_t *tps, char *format, ...)
{
    if (tps->error_msg == NULL) return;

    str_cat_printf (tps->error_msg, "%i:%i " ECMA_YELLOW("warning: "), tps->line_number, tps->column_number);

    PRINTF_INIT (format, size, args);
    size_t old_len = str_len(tps->error_msg);
    str_maybe_grow (tps->error_msg, str_len(tps->error_msg) + size - 1, true);
    PRINTF_SET (str_data(tps->error_msg) + old_len, size, format, args);

    str_cat_c (tps->error_msg, "\n");
}

static inline
char tps_curr_char (struct tsplx_parser_state_t *tps)
{
    return *(tps->pos);
}

static inline
bool tps_is_eof (struct tsplx_parser_state_t *tps)
{
    return tps->is_eof || tps_curr_char(tps) == '\0';
}

static inline
bool tps_is_digit (struct tsplx_parser_state_t *tps)
{
    return !tps_is_eof(tps) && char_in_str(tps_curr_char(tps), "1234567890");
}

static inline
bool tps_is_space (struct tsplx_parser_state_t *ps)
{
    return !tps_is_eof(ps) && is_space(ps->pos);
}

static inline
bool tps_is_operator (struct tsplx_parser_state_t *tps)
{
    return !tps_is_eof(tps) && char_in_str(tps_curr_char(tps), ";,.[]{}()=");
}

static inline
bool tps_is_identifier_char (struct tsplx_parser_state_t *tps)
{
    return !tps_is_eof(tps) && !tps_is_operator(tps) && !tps_is_space(tps) && tps_curr_char(tps) != '\n';
}

static inline
void tps_advance_char (struct tsplx_parser_state_t *tps)
{
    if (!tps_is_eof(tps)) {
        tps->pos++;

        tps->column_number++;

        if (*(tps->pos) == '\n') {
            tps->line_number++;
            tps->column_number = 0;
        }

    } else {
        tps->is_eof = true;
    }
}

// NOTE: c_str MUST be null terminated!!
bool tps_match_str(struct tsplx_parser_state_t *tps, char *c_str)
{
    char *backup_pos = tps->pos;

    bool found = true;
    while (*c_str) {
        if (tps_curr_char(tps) != *c_str) {
            found = false;
            break;
        }

        tps_advance_char (tps);
        c_str++;
    }

    if (!found) tps->pos = backup_pos;

    return found;
}

static inline
bool tps_match_digits (struct tsplx_parser_state_t *tps)
{
    char *pos = tps->pos;
    int column_number = tps->column_number;
    int line_number = tps->line_number;

    bool found = false;
    if (tps_is_digit(tps)) {
        found = true;
        while (tps_is_digit(tps)) {
            tps_advance_char (tps);
        }
    }

    // This isn't actually a digit because it's followed by additional
    // characters. Restore the state and mark it as not found.
    if (tps_is_identifier_char(tps)) {
        tps->pos = pos;
        tps->column_number = column_number;
        tps->line_number = line_number;
        found = false;
    }

    return found;
}

sstring_t tps_advance_line(struct tsplx_parser_state_t *tps)
{
    char *start = tps->pos;
    while (!tps_is_eof(tps) && tps_curr_char(tps) != '\n') {
        tps_advance_char (tps);
    }

    // Advance over the \n character.
    tps_advance_char (tps);

    return SSTRING(start, tps->pos - start);
}

static inline
void tps_consume_empty_lines (struct tsplx_parser_state_t *tps)
{
    while (tps_is_space(tps) || tps_curr_char(tps) == '\n') {
        tps_advance_char (tps);
    }
}

void tps_parse_identifier (struct tsplx_parser_state_t *tps, struct tsplx_token_t *tok)
{
    char *start = tps->pos;
    // TODO: Maybe better define a set of valid identifier characters?.
    // Reserve : for prefixes, don't allow identifiers starting with
    // numbers.
    while (tps_is_identifier_char(tps)) {
        tps_advance_char (tps);
    }

    tok->value = SSTRING(start, tps->pos - start);
}

struct tsplx_token_t tps_next (struct tsplx_parser_state_t *tps)
{
    struct tsplx_token_t _tok = {0};
    struct tsplx_token_t *tok = &_tok;

    tps_consume_empty_lines (tps);

    char *non_space_pos = tps->pos;
    if (tps_is_eof(tps)) {
        tps->is_eof = true;
        tok->type = TSPLX_TOKEN_TYPE_EOF;

    } else if (tps_match_str(tps, "//")) {
        tok->type = TSPLX_TOKEN_TYPE_COMMENT;
        tok->value = tps_advance_line (tps);

    } else if (tps_match_digits(tps)) {
        // TODO: Process numbers with decimal part
        tok->value = SSTRING(non_space_pos, tps->pos - non_space_pos);
        tok->type = TSPLX_TOKEN_TYPE_NUMBER;

    } else if (tps_match_str(tps, "<")) {
        char *start = tps->pos;
        while (!tps_is_eof(tps) && tps_curr_char(tps) != '>') {
            tps_advance_char (tps);
        }

        tok->type = TSPLX_TOKEN_TYPE_URI;
        tok->value = SSTRING(start, tps->pos - start);

        // Advance over terminating > character
        tps_advance_char (tps);

    } else if (tps_match_str(tps, "\"\"\"")) {
        char *start = tps->pos;
        while (!tps_is_eof(tps) && !tps_match_str(tps, "\"\"\"")) {
            tps_advance_char (tps);
        }

        tok->type = TSPLX_TOKEN_TYPE_MULTILINE_STRING;
        tok->value = SSTRING(start, tps->pos - start - 3);

    } else if (tps_match_str(tps, "\"")) {
        char *start = tps->pos;
        // :scr_match_double_quoted_string
        while (!tps_is_eof(tps) && tps_curr_char(tps) != '"') {
            tps_advance_char (tps);

            if (tps_curr_char(tps) == '"' && *(tps->pos - 1) == '\\') {
                tps_advance_char (tps);
            }
        }

        tok->type = TSPLX_TOKEN_TYPE_STRING;
        tok->value = SSTRING(start, tps->pos - start);

        // Advance over terminating " character
        tps_advance_char (tps);

    } else if (tps_match_str(tps, "q\"")) {
        char *start = tps->pos;
        while (!tps_is_eof(tps) && tps_curr_char(tps) != '"') {
            tps_advance_char (tps);

            if (tps_curr_char(tps) == '"' && *(tps->pos - 1) == '\\') {
                tps_advance_char (tps);
            }
        }

        tok->type = TSPLX_TOKEN_TYPE_SOFT_REFERENCE;
        tok->value = SSTRING(start, tps->pos - start);

        // Advance over terminating " character
        tps_advance_char (tps);

    } else if (tps_is_operator(tps)) {
        tok->type = TSPLX_TOKEN_TYPE_OPERATOR;
        tok->value = SSTRING(tps->pos, 1);
        tps_advance_char (tps);

    } else if (tps_match_str(tps, "?")) {
        tps_parse_identifier (tps, tok);
        tok->type = TSPLX_TOKEN_TYPE_VARIABLE;

    } else {
        tps_parse_identifier (tps, tok);
        tok->type = TSPLX_TOKEN_TYPE_IDENTIFIER;
    }

    tps->token = *tok;

    return tps->token;
}

bool tps_match(struct tsplx_parser_state_t *tps, enum tsplx_token_type_t type, char *value)
{
    if (tps->is_eof) return false;

    bool match = false;

    if (type == tps->token.type) {
        if (value == NULL) {
            match = true;

        } else if (strncmp(tps->token.value.s, value, tps->token.value.len) == 0) {
            match = true;
        }
    }

    return match;
}

void print_tsplx_tokens (char *tsplx_str)
{
    struct tsplx_parser_state_t _tps = {0};
    struct tsplx_parser_state_t *tps = &_tps;
    tps_init (tps, tsplx_str, NULL);

    while (!tps->is_eof && !tps->error) {
        tps_next (tps);

        printf ("%s", tsplx_token_type_names[tps->token.type]);
        if (tps->token.value.len > 0) {
            printf (": ");
            prnt_debug_sstring (&tps->token.value);

        } else {
            printf ("\n");
        }
    }
}

struct splx_node_list_t* tps_wrap_in_list_node (struct splx_data_t *sd, struct splx_node_t *node)
{
    struct splx_node_list_t *new_node_list_element = mem_pool_push_struct (&sd->pool, struct splx_node_list_t);
    *new_node_list_element = ZERO_INIT(struct splx_node_list_t);
    new_node_list_element->node = node;

    return new_node_list_element;
}

static inline
bool splx_node_has_name (struct splx_node_t *node)
{
    return str_len(&node->str) > 0;
}

static inline
bool splx_node_render_as_reference (struct splx_node_t *node, struct cstr_to_splx_node_map_t *printed_nodes)
{
    return (splx_node_has_name (node) &&
        node->attributes.num_nodes == 0 &&
        node->floating_values == NULL) ||
        cstr_to_splx_node_map_lookup (printed_nodes, str_data(&node->str), NULL);
}

static inline
void splx_cat_node_identifier (string_t *str, int curr_indent, struct splx_node_t *node)
{
    if (node->uri_formatted_identifier) {
        str_cat_indented_printf (str, curr_indent, "<%s>", str_data(&node->str));
    } else {
        str_cat_indented_printf (str, curr_indent, "%s", str_data(&node->str));
    }
}

static inline
bool splx_node_is_root (struct splx_data_t *sd, struct splx_node_t *node)
{
    return sd->root == node;
}

struct splx_node_t* splx_node_new (struct splx_data_t *sd)
{
    struct splx_node_t *new_node = mem_pool_push_struct (&sd->pool, struct splx_node_t);
    *new_node = ZERO_INIT (struct splx_node_t);
    str_pool (&sd->pool, &new_node->str);
    return new_node;
}

void splx_node_add (struct splx_data_t *sd, struct splx_node_t *node)
{
    // Don't reuse literal nodes.
    // TODO: If this is a desireable thing to do for space performance reasons,
    // then we should have a string pool, not really a value node map. A value
    // node map is problematic because we may have 2 literals of different type
    // but same value like "John Doe" and q"John Doe" or 1 and "1".
    if (splx_node_has_name (node) &&
        node->type != SPLX_NODE_TYPE_INTEGER &&
        node->type != SPLX_NODE_TYPE_STRING &&
        node->type != SPLX_NODE_TYPE_SOFT_REFERENCE) {
        cstr_to_splx_node_map_insert (&sd->nodes, str_data(&node->str), node);
    }

    if (node->type == SPLX_NODE_TYPE_OBJECT) {
        if (sd->entities == NULL) {
            // TODO: Which other places may be reached while having a NULL
            // entities?.
            sd->entities = splx_node_new (sd);
        }

        struct splx_node_list_t *list_node = tps_wrap_in_list_node (sd, node);
        LINKED_LIST_APPEND (sd->entities->floating_values, list_node);
    }
}

struct splx_node_t* splx_node (struct splx_data_t *sd, char *c_str, enum splx_node_type_t type)
{
    struct splx_node_t *node = splx_node_new (sd);
    str_set (&node->str, c_str);
    node->type = type;
    splx_node_add (sd, node);

    return node;
}

void splx_node_clear (struct splx_node_t *node)
{
    node->type = SPLX_NODE_TYPE_UNKNOWN;
    strn_set (&node->str, "", 0);

    cstr_to_splx_node_list_map_destroy (&node->attributes);
    node->attributes = ZERO_INIT (struct cstr_to_splx_node_list_map_t);

    node->floating_values = NULL;
    node->floating_values_end = NULL;
}

struct splx_node_t* splx_node_get_or_create (struct splx_data_t *sd,
                                             char *id, enum splx_node_type_t type)
{
    assert(sd != NULL);

    struct splx_node_t *subject_node = NULL;
    if (id != NULL) {
        subject_node = cstr_to_splx_node_map_get (&sd->nodes, id);
        if (subject_node == NULL) {
            subject_node = splx_node_new (sd);
            str_set (&subject_node->str, id);
            subject_node->type = type;
            splx_node_add (sd, subject_node);
        }

    } else {
        subject_node = splx_node_new (sd);
        subject_node->type = type;
        splx_node_add (sd, subject_node);
    }

    return subject_node;
}

void splx_node_attribute_add (struct splx_data_t *sd, struct splx_node_t *node,
                              char *predicate,
                              char *value, enum splx_node_type_t value_type)
{
    char *predicate_str = splx_get_node_id_str (sd, predicate);

    struct splx_node_t *subject_node = splx_node_get_or_create (sd, value, value_type);
    struct splx_node_list_t *subject_node_list = tps_wrap_in_list_node (sd, subject_node);

    cstr_to_splx_node_list_map_insert (&node->attributes, predicate_str, subject_node_list);
}

void splx_node_attribute_append (struct splx_data_t *sd, struct splx_node_t *node,
                                 char *predicate,
                                 struct splx_node_t *object)
{
    char *predicate_str = splx_get_node_id_str (sd, predicate);

    struct splx_node_list_t *subject_node_list = cstr_to_splx_node_list_map_get (&node->attributes, predicate_str);
    if (subject_node_list == NULL) {
        subject_node_list = tps_wrap_in_list_node (sd, object);
        cstr_to_splx_node_list_map_insert (&node->attributes, predicate_str, subject_node_list);

    } else {
        while (subject_node_list->next != NULL) {
            subject_node_list = subject_node_list->next;
        }

        struct splx_node_list_t *new_node_list_element = tps_wrap_in_list_node (sd, object);
        subject_node_list->next = new_node_list_element;
    }
}

void splx_node_attribute_append_c_str (struct splx_data_t *sd, struct splx_node_t *node,
                                       char *predicate,
                                       char *c_str,
                                       enum splx_node_type_t type)
{
    struct splx_node_t *value = splx_node (sd, c_str, type);
    splx_node_attribute_append (sd, node, predicate, value);
}

void splx_node_attribute_append_once (struct splx_data_t *sd, struct splx_node_t *node,
                                      char *predicate,
                                      struct splx_node_t *object)
{
    char *predicate_str = splx_get_node_id_str (sd, predicate);

    bool found = false;
    struct splx_node_list_t *subject_node_list = cstr_to_splx_node_list_map_get (&node->attributes, predicate_str);
    if (subject_node_list == NULL) {
        subject_node_list = tps_wrap_in_list_node (sd, object);
        cstr_to_splx_node_list_map_insert (&node->attributes, predicate_str, subject_node_list);

    } else {
        found = false;
        LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, subject_node_list) {
            if (object == curr_list_node->node) {
                found = true;
            }

            if (curr_list_node->next == NULL) break;
        }

        if (!found) {
            struct splx_node_list_t *new_node_list_element = tps_wrap_in_list_node (sd, object);
            curr_list_node->next = new_node_list_element;
        }
    }
}

void splx_node_attribute_append_once_c_str (struct splx_data_t *sd, struct splx_node_t *node,
                                            char *predicate,
                                            char *c_str,
                                            enum splx_node_type_t type)
{
    char *predicate_str = splx_get_node_id_str (sd, predicate);

    bool found = false;
    struct splx_node_list_t *subject_node_list = cstr_to_splx_node_list_map_get (&node->attributes, predicate_str);
    if (subject_node_list == NULL) {
        subject_node_list = tps_wrap_in_list_node (sd, splx_node (sd, c_str, type));
        cstr_to_splx_node_list_map_insert (&node->attributes, predicate_str, subject_node_list);

    } else {
        found = false;
        LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, subject_node_list) {
            if (strcmp (c_str, str_data(&curr_list_node->node->str)) == 0) {
                found = true;
            }

            if (curr_list_node->next == NULL) break;
        }

        if (!found) {
            struct splx_node_t *value = splx_node (sd, c_str, type);
            struct splx_node_list_t *new_node_list_element = tps_wrap_in_list_node (sd, value);
            curr_list_node->next = new_node_list_element;
        }
    }
}

struct splx_node_list_t* splx_node_get_attributes (struct splx_node_t *node, char *attr)
{
    return cstr_to_splx_node_list_map_get (&node->attributes, attr);
}

struct splx_node_t* splx_get_node_by_name(struct splx_data_t *sd, char *name)
{
    struct splx_node_t *result = NULL;

    LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, sd->entities->floating_values) {
        struct splx_node_t *entity = curr_list_node->node;

        struct splx_node_list_t *names = splx_node_get_attributes (entity, "name");
        LINKED_LIST_FOR (struct splx_node_list_t *, curr_name, names) {
            struct splx_node_t *node = curr_name->node;

            if (node->type == SPLX_NODE_TYPE_STRING && strcmp(str_data(&node->str), name) == 0) {
                result = entity;
                break;
            }
        }

        if (result != NULL) break;
    }

    return result;
}

// TODO: Actually search for a specific type
struct splx_node_t* splx_get_node_by_default_constructor(struct splx_data_t *sd, char *type, char *name)
{
    struct splx_node_t *result = NULL;

    LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, sd->entities->floating_values) {
        struct splx_node_t *entity = curr_list_node->node;

        struct splx_node_list_t *names = splx_node_get_attributes (entity, "name");
        LINKED_LIST_FOR (struct splx_node_list_t *, curr_name, names) {
            struct splx_node_t *node = curr_name->node;

            if (node->type == SPLX_NODE_TYPE_STRING && strcmp(str_data(&node->str), name) == 0) {
                result = entity;
                break;
            }
        }

        if (result != NULL) break;
    }

    return result;
}

// CAUTION: This isn't a fully deep copy, the values pointed to by the attribute
// map are just aliased from the created node. Still, node lists should never be
// duplicated or aliased.
// TODO: Maybe create a consistency check that verifies that no 2 node's
// attributes point to the same node list.
struct splx_node_t* splx_node_dup (struct splx_data_t *sd, struct splx_node_t *original)
{
    struct splx_node_t *new_node = splx_node_new (sd);

    // Copy all simple attributes
    *new_node = *original;


    // Zero initialize all non-simple attributes, set their value from original

    // Value string
    new_node->str = ZERO_INIT(string_t);
    str_set (&new_node->str, str_data(&original->str));

    // Attributes tree
    new_node->attributes = ZERO_INIT (struct cstr_to_splx_node_list_map_t);
    BINARY_TREE_FOR (cstr_to_splx_node_list_map, &original->attributes, curr_attribute) {
        cstr_to_splx_node_list_map_insert (&new_node->attributes, curr_attribute->key, curr_attribute->value);
    }

    return new_node;
}

#define SPLX_STR_INDENT 2

#define str_cat_splx_node(str,node) str_cat_splx_node_full(str,node,0)
void str_cat_splx_node_full (string_t *str, struct splx_node_t *node, int curr_indent)
{
    str_cat_debugstr (str, curr_indent, ESC_COLOR_DEFAULT, str_data(&node->str));

    if (node->attributes.num_nodes > 0) {
        BINARY_TREE_FOR (cstr_to_splx_node_list_map, &node->attributes, curr_attribute) {
            str_cat_debugstr (str, curr_indent+SPLX_STR_INDENT, ESC_COLOR_DEFAULT, curr_attribute->key);
            LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, curr_attribute->value) {
                str_cat_debugstr (str, curr_indent+2*SPLX_STR_INDENT, ESC_COLOR_DEFAULT, str_data(&curr_list_node->node->str));
            }
        }
    }
}

void print_splx_node (struct splx_node_t *node)
{
    string_t buff = {0};
    str_cat_splx_node (&buff, node);
    printf ("%s", str_data(&buff));
    str_free (&buff);
}

#define str_cat_splx_dump(str,sd,node) str_cat_splx_dump_full(str,sd,node,0)
void str_cat_splx_dump_full (string_t *str, struct splx_data_t *sd, struct splx_node_t *node, int curr_indent)
{
    if (node == NULL) {
        str_cat_indented_c (str, ECMA_S_RED(0, "0x0") "\n", curr_indent);
        return;
    }

    // TODO: Avoid infinite loops if there is a cycle. Keep a map of printed
    // nodes and don't recurse down to the same node more than once.

    str_cat_indented_printf (str, curr_indent, ECMA_S_CYAN(0, "%p") "\n", node);
    str_cat_indented_printf (str, curr_indent, "TYPE: %s\n", splx_node_type_names[node->type]);
    str_cat_indented_c (str, "STR: ", curr_indent);

    if (strstr(str_data(&node->str), "\n") == NULL) {
        str_cat_debugstr (str, 0, ESC_COLOR_YELLOW, str_data(&node->str));
    } else {
        str_cat_printf (str, "\n");
        str_cat_debugstr (str, curr_indent + 1*SPLX_STR_INDENT, ESC_COLOR_YELLOW, str_data(&node->str));
    }

    if (node->type == SPLX_NODE_TYPE_OBJECT ||
        node->attributes.num_nodes > 0 ||
        node->floating_values != NULL)
    {
        str_cat_indented_printf (str, curr_indent, "ATTRIBUTES: %d\n", node->attributes.num_nodes);
        BINARY_TREE_FOR (cstr_to_splx_node_list_map, &node->attributes, curr_attribute) {
            str_cat_debugstr (str, curr_indent + SPLX_STR_INDENT, ESC_COLOR_GREEN, curr_attribute->key);

            bool is_first_value = true;
            struct ptr_set_t past_values = {0};
            LINKED_LIST_FOR (struct splx_node_list_t *, curr_node_list_element, curr_attribute->value) {
                if (!is_first_value) {
                    str_cat_indented_c (str, "-----\n", curr_indent + 2*SPLX_STR_INDENT);
                }
                is_first_value = false;

                str_cat_splx_dump_full (str, sd, curr_node_list_element->node, curr_indent + 2*SPLX_STR_INDENT);

                if (curr_node_list_element->next == curr_node_list_element) {
                    str_cat_indented_printf (str, curr_indent + 2*SPLX_STR_INDENT,
                                             ECMA_RED("error:") " self cyclic list node.\n");
                    break;

                } else if (ptr_set_get (&past_values, curr_node_list_element) != NULL) {
                    str_cat_indented_printf (str, curr_indent + 2*SPLX_STR_INDENT,
                                             ECMA_RED("error:") " returned back to list node of: " ECMA_S_CYAN(0, "%p") "\n", curr_node_list_element->node);
                    break;
                }


                ptr_set_insert (&past_values, curr_node_list_element, curr_node_list_element);
            }
            ptr_set_destroy (&past_values);
        }

        str_cat_indented_c (str, "FLOATING:", curr_indent);
        if (node->floating_values != NULL) {
            LINKED_LIST_FOR (struct splx_node_list_t *, curr_node_list_element, node->floating_values) {
                str_cat_c (str, "\n");
                str_cat_splx_dump_full (str, sd, curr_node_list_element->node, curr_indent + SPLX_STR_INDENT);
            }
        } else {
            str_cat_c (str, " (none)\n");
        }
    }
}

void str_cat_literal_node (string_t *str, struct splx_node_t *node)
{
    if (node->type == SPLX_NODE_TYPE_STRING || node->type == SPLX_NODE_TYPE_SOFT_REFERENCE) {
        string_t buff = {0};

        str_cpy (&buff, &node->str);
        str_replace (&buff, "\"", "\\\"", NULL);
        str_replace (&buff, "\n", "\\n", NULL);
        str_replace (&buff, "\t", "\\t", NULL);

        if (node->type == SPLX_NODE_TYPE_SOFT_REFERENCE) str_cat_c(str, "q");
        str_cat_printf (str, "\"%s\"", str_data(&buff));

        str_free (&buff);

    } else if (node->type == SPLX_NODE_TYPE_INTEGER) {
        str_cat_printf (str, "%s", str_data(&node->str));

    } else {
        str_cat_c (str, " ?");
    }
}

void str_cat_splx_canonical_full (string_t *str, struct splx_data_t *sd,
                                  struct splx_node_t *node,
                                  int curr_indent,
                                  struct cstr_to_splx_node_map_t *printed_nodes);

void tps_cat_floating_literals (string_t *str, struct splx_data_t *sd,
                                struct splx_node_t *node,
                                struct splx_node_list_t *floating_objects,
                                int curr_indent,
                                struct cstr_to_splx_node_map_t *printed_nodes)
{
    bool is_first_floating = true;
    enum splx_node_type_t prev_type = SPLX_NODE_TYPE_UNKNOWN;
    LINKED_LIST_FOR (struct splx_node_list_t*, curr_node_list_element, node->floating_values) {
        struct splx_node_t *curr_node = curr_node_list_element->node;

        if (curr_node_list_element == floating_objects) break;

        if (curr_node->type != prev_type && !is_first_floating && str_last(str) != '\n') {
            str_cat_c (str, "\n");
        }

        if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
            if (splx_node_render_as_reference(curr_node, printed_nodes))
            {
                splx_cat_node_identifier(str, curr_indent, curr_node);

            } else {
                str_cat_indented_c (str, "[\n", curr_indent);
                str_cat_splx_canonical_full(str,sd,curr_node, curr_indent + SPLX_STR_INDENT,printed_nodes);
                str_cat_indented_c (str, "]", curr_indent);
            }

        } else {
            if (curr_node->type != prev_type) {
                str_cat_char (str, ' ', curr_indent);
            } else {
                str_cat_c (str, " ");
            }

            str_cat_literal_node (str, curr_node);
        }

        str_cat_c (str, ";");

        prev_type = curr_node->type;
        is_first_floating = false;
    }
}

// TODO: It's a bit cumbersome to have to pass sd here. It's only used to detect
// if the node is the root node. I think serializing should be independent of
// weather or not a node is the root.
void str_cat_splx_canonical_full (string_t *str, struct splx_data_t *sd,
                                  struct splx_node_t *node,
                                  int curr_indent,
                                  struct cstr_to_splx_node_map_t *printed_nodes)
{
    bool is_triple = false;
    if (splx_node_has_name (node)) {
        cstr_to_splx_node_map_insert (printed_nodes, str_data(&node->str), node);

        str_cat_indented_c (str, str_data(&node->str), curr_indent);
        is_triple = true;

    } else if (sd->root != node) {
        str_cat_indented_c (str, "_", curr_indent);
        is_triple = true;
    }

    if (node->attributes.num_nodes == 0 && node->floating_values == NULL) {
        str_cat_c (str, " ;");
    }

    enum splx_node_type_t prev_type = SPLX_NODE_TYPE_UNKNOWN;
    struct splx_node_list_t *floating_objects = node->floating_values;
    LINKED_LIST_FOR (struct splx_node_list_t*, curr_node_list_element, node->floating_values) {
        struct splx_node_t *curr_node = curr_node_list_element->node;

        if (curr_node->type == SPLX_NODE_TYPE_OBJECT &&
            prev_type != SPLX_NODE_TYPE_OBJECT)
        {
            floating_objects = curr_node_list_element;
        }
        prev_type = curr_node->type;
    }

    while (floating_objects != NULL && splx_node_render_as_reference (floating_objects->node, printed_nodes)) {
        floating_objects = floating_objects->next;
    }

    if ((node->attributes.num_nodes == 0 || !splx_node_has_name (node))) {
        if (!is_triple) {
            if (node->floating_values != floating_objects) {
                tps_cat_floating_literals (str, sd, node, floating_objects, curr_indent, printed_nodes);

                if (node->floating_values != NULL) {
                    str_cat_c (str, "\n\n");
                }
            }
        }
    }

    if (node->attributes.num_nodes > 0) {
        int attr_cnt = 0;
        bool is_first_predicate = true;
        BINARY_TREE_FOR (cstr_to_splx_node_list_map, &node->attributes, curr_attribute) {
            if (is_first_predicate) {
                if (sd->root != node) {
                    str_cat_c (str, " ");
                }
                str_cat_c (str, curr_attribute->key);

            } else {
                if (!is_triple) {
                    str_cat_indented_c (str, curr_attribute->key, curr_indent);
                } else {
                    str_cat_indented_c (str, curr_attribute->key, curr_indent+SPLX_STR_INDENT);
                }
            }
            is_first_predicate = false;

            bool is_first_value = true;
            LINKED_LIST_FOR (struct splx_node_list_t *, curr_node_list_element, curr_attribute->value) {
                struct splx_node_t *curr_node = curr_node_list_element->node;

                int indent_levels = 0;
                if (is_first_value) {
                    str_cat_c (str, " ");

                } else {
                    indent_levels = 1;
                    if (is_triple) indent_levels = 2;
                    str_cat_char (str, ' ', curr_indent + indent_levels*SPLX_STR_INDENT);
                }

                if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
                    if (splx_node_render_as_reference(curr_node, printed_nodes)) {
                        splx_cat_node_identifier (str, curr_indent, curr_node);

                    } else {
                        str_cat_indented_c (str, "[\n", curr_indent + indent_levels*SPLX_STR_INDENT);

                        indent_levels = 1;
                        if (is_triple) indent_levels = 2;
                        str_cat_splx_canonical_full(str,sd,curr_node, curr_indent + (indent_levels+1)*SPLX_STR_INDENT, printed_nodes);
                        str_cat_indented_c (str, "]", curr_indent + indent_levels*SPLX_STR_INDENT);
                    }

                } else {
                    str_cat_literal_node (str, curr_node);
                }

                if (curr_node_list_element->next != NULL) {
                    str_cat_c (str, " ;\n");
                }

                is_first_value = false;
            }

            attr_cnt++;
            str_cat_c (str, " ;\n");
        }

    }

    if (sd->root != node && node->floating_values != NULL) {
        str_cat_indented_c (str, "{\n", curr_indent + (is_triple ? 1 : 0)*SPLX_STR_INDENT);
    } else if (splx_node_has_name(node) && node->attributes.num_nodes == 0) {
        str_cat_c (str, "\n");
    } else if (node->attributes.num_nodes > 0 && floating_objects != NULL && node->floating_values != floating_objects) {
        str_cat_c (str, "\n");
    }

    int floating_indent = 0;
    if (is_triple) floating_indent = 2*SPLX_STR_INDENT;

    bool is_first_floating_object = true;
    prev_type = SPLX_NODE_TYPE_UNKNOWN;
    {
        if (is_triple) {
            tps_cat_floating_literals (str, sd, node, floating_objects, curr_indent + 2*SPLX_STR_INDENT, printed_nodes);

            if (str_last(str) != '\n') {
                str_cat_c (str, "\n");
            }

            if (floating_objects != NULL && floating_objects != node->floating_values) {
                str_cat_c (str, "\n");
            }
        }

        LINKED_LIST_FOR (struct splx_node_list_t*, curr_node_list_element, floating_objects) {
            struct splx_node_t *curr_node = curr_node_list_element->node;

            if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
                if (!is_first_floating_object) {
                    str_cat_c (str, "\n");
                }
                str_cat_splx_canonical_full(str, sd, curr_node, curr_indent + floating_indent, printed_nodes);

            } else {
                if (is_first_floating_object || curr_node->type != prev_type) {
                    if (!is_first_floating_object) {
                        str_cat_c (str, "\n");
                    }
                    str_cat_char (str, ' ', curr_indent + floating_indent);
                } else {
                    str_cat_c (str, " ");
                }

                str_cat_literal_node (str, curr_node);
                str_cat_c (str, ";");
            }

            prev_type = curr_node->type;
            is_first_floating_object = false;
        }
    }

    if (str_last(str) != '\n'){
        str_cat_c (str, "\n");
    }

    if (sd->root != node && node->floating_values != NULL) {
        str_cat_indented_c (str, "}\n", curr_indent + (is_triple ? 1 : 0)*SPLX_STR_INDENT);
    }

    if (str_last(str) != '\n'){
        str_cat_c (str, "\n");
    }
}

static inline
void str_cat_splx_canonical(string_t *str, struct splx_data_t *sd, struct splx_node_t *node) 
{
    struct cstr_to_splx_node_map_t printed_nodes = {0};
    str_cat_splx_canonical_full (str, sd, node, 0, &printed_nodes);
    cstr_to_splx_node_map_destroy (&printed_nodes);
}

void str_cat_literal_node_ttl (string_t *str, struct splx_node_t *node)
{
    if (node->type == SPLX_NODE_TYPE_STRING || node->type == SPLX_NODE_TYPE_SOFT_REFERENCE) {
        string_t buff = {0};

        str_cpy (&buff, &node->str);
        str_replace (&buff, "\"", "\\\"", NULL);
        str_replace (&buff, "\n", "\\n", NULL);
        str_replace (&buff, "\t", "\\t", NULL);

        str_cat_printf (str, "\"%s\"", str_data(&buff));
        if (node->type == SPLX_NODE_TYPE_SOFT_REFERENCE) str_cat_c(str, "^^t:soft-reference");

        str_free (&buff);

    } else if (node->type == SPLX_NODE_TYPE_OBJECT) {
        str_cat_printf (str, "<%s>", str_data(&node->str));

    } else if (node->type == SPLX_NODE_TYPE_INTEGER) {
        str_cat_printf (str, "%s", str_data(&node->str));

    } else {
        str_cat_c (str, " ?");
    }
}

void str_cat_splx_ttl_full (string_t *str, struct splx_data_t *sd, struct splx_node_t *node, char *empty_node_name, int curr_indent)
{
    if (str_len(&node->str) > 0) {
        str_cat_indented_printf (str, curr_indent, "<%s>", str_data(&node->str));

    } else if (empty_node_name != NULL) {
        str_cat_indented_printf (str, curr_indent, "<%s>", empty_node_name);
    }

    if (node->attributes.num_nodes > 0) {
        int attr_cnt = 0;
        bool is_first_predicate = true;
        BINARY_TREE_FOR (cstr_to_splx_node_list_map, &node->attributes, curr_attribute) {
            if (is_first_predicate) {
                if (splx_node_has_name (node) || splx_node_is_root(sd, node)) {
                    str_cat_c (str, " ");

                } else {
                    str_cat_char (str, ' ', curr_indent);
                }

                if (curr_attribute->key[0] != 'a' || curr_attribute->key[1] != '\0') {
                    str_cat_printf (str, "<%s>", curr_attribute->key);
                } else {
                    str_cat_c (str, "a");
                }

            } else {
                int indent_levels = 0;
                if (splx_node_has_name(node) || splx_node_is_root(sd, node)) indent_levels = 1;

                if (curr_attribute->key[0] != 'a' || curr_attribute->key[1] != '\0') {
                    str_cat_indented_printf (str, curr_indent+indent_levels*SPLX_STR_INDENT, "<%s>", curr_attribute->key);
                } else {
                    str_cat_indented_c (str, "a", curr_indent+indent_levels*SPLX_STR_INDENT);
                }
            }
            is_first_predicate = false;

            bool is_first_value = true;
            LINKED_LIST_FOR (struct splx_node_list_t *, curr_node_list_element, curr_attribute->value) {
                struct splx_node_t *curr_node = curr_node_list_element->node;

                if (is_first_value) {
                    str_cat_c (str, " ");
                } else {
                    str_cat_char (str, ' ', curr_indent+2*SPLX_STR_INDENT);
                }

                if (curr_node->type == SPLX_NODE_TYPE_OBJECT && !splx_node_has_name(curr_node)) {
                    str_cat_c (str, "[\n");
                    str_cat_splx_ttl_full (str, sd, curr_node, NULL, curr_indent + 3*SPLX_STR_INDENT);
                    str_cat_indented_c (str, "]\n", curr_indent + SPLX_STR_INDENT);

                } else {
                    str_cat_literal_node_ttl (str, curr_node);
                }

                if (curr_node_list_element->next != NULL) {
                    str_cat_c (str, " ,\n");
                }
                is_first_value = false;
            }

            attr_cnt++;
            if (attr_cnt < node->attributes.num_nodes) {
                str_cat_c (str, " ;\n");
            }
            is_first_value = false;
        }
    }

    if (node->attributes.num_nodes != 0 && node->floating_values != NULL) {
        int indent_levels = 0;
        if (splx_node_has_name(node) || splx_node_is_root(sd, node)) indent_levels = 1;

        str_cat_c (str, " ;\n");
        str_cat_char (str, ' ', curr_indent + indent_levels*SPLX_STR_INDENT);
    } else {
        str_cat_c (str, " ");
    }

    if (node->floating_values != NULL) {
        str_cat_c (str, "<has-entity>");

        bool is_first_floating_object = true;
        LINKED_LIST_FOR (struct splx_node_list_t*, curr_node_list_element, node->floating_values) {
            struct splx_node_t *curr_node = curr_node_list_element->node;

            int indent_levels = 1;
            if (splx_node_has_name(node) || splx_node_is_root(sd, node)) indent_levels = 2;

            if (!is_first_floating_object) {
                str_cat_char (str, ' ', curr_indent + indent_levels*SPLX_STR_INDENT);

            } else {
                str_cat_c (str, " ");
            }

            if (curr_node->type == SPLX_NODE_TYPE_OBJECT && !splx_node_has_name(curr_node)) {
                str_cat_c (str, "[\n");
                str_cat_splx_ttl_full (str, sd, curr_node, NULL, curr_indent + (indent_levels+1)*SPLX_STR_INDENT);
                str_cat_indented_c (str, "]", curr_indent + indent_levels*SPLX_STR_INDENT);

            } else {
                str_cat_literal_node_ttl (str, curr_node);
            }

            if (curr_node_list_element->next != NULL) {
                str_cat_c (str, " ,\n");
            }

            is_first_floating_object = false;
        }
    }

    if (str_last(str) != ' '){
        str_cat_c (str, " ");
    }

    if (splx_node_has_name (node) || splx_node_is_root(sd, node)) {
        str_cat_c (str, ".\n");
    } else {
        str_cat_c (str, ";\n");
    }
}

static inline
void tps_collect_referenced_node (struct splx_node_t *node, struct cstr_to_splx_node_map_t *referenced_nodes)
{
    if (splx_node_has_name(node) &&
        (node->attributes.num_nodes > 0 || node->floating_values != NULL))
    {
        cstr_to_splx_node_map_insert (referenced_nodes, str_data(&node->str), node);
    }
}

void tps_collect_referenced_nodes (struct splx_data_t *sd, struct splx_node_t *node, struct cstr_to_splx_node_map_t *referenced_nodes)
{
    if (node->attributes.num_nodes > 0) {
        BINARY_TREE_FOR (cstr_to_splx_node_list_map, &node->attributes, curr_attribute) {
            struct cstr_to_splx_node_map_node_t *tree_node = NULL;
            if (cstr_to_splx_node_map_lookup (&sd->nodes, curr_attribute->key, &tree_node)) {
                if (tree_node->value != NULL) {
                    tps_collect_referenced_node (tree_node->value, referenced_nodes);
                }
            }

            LINKED_LIST_FOR (struct splx_node_list_t *, curr_node_list_element, curr_attribute->value) {
                struct splx_node_t *curr_node = curr_node_list_element->node;

                if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
                    if (!cstr_to_splx_node_map_lookup (referenced_nodes, str_data(&curr_node->str), NULL)) {
                        tps_collect_referenced_nodes (sd, curr_node, referenced_nodes);
                    }

                    tps_collect_referenced_node (curr_node, referenced_nodes);
                }
            }
        }
    }

    LINKED_LIST_FOR (struct splx_node_list_t*, curr_node_list_element, node->floating_values) {
        struct splx_node_t *curr_node = curr_node_list_element->node;

        if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
            if (!cstr_to_splx_node_map_lookup (referenced_nodes, str_data(&curr_node->str), NULL)) {
                tps_collect_referenced_nodes (sd, curr_node, referenced_nodes);
            }

            tps_collect_referenced_node (curr_node, referenced_nodes);
        }
    }
}

void str_cat_splx_ttl (string_t *str, struct splx_data_t *sd, struct splx_node_t *node)
{
    struct cstr_to_splx_node_map_t referenced_nodes = {0};

    // We can't collect referenced nodes during str_cat_splx_ttl_full() because
    // it's called while iterating the collcted referenced nodes. It's not good
    // to modify the tree while it's being iterated, so we do this at a separate
    // step.
    tps_collect_referenced_nodes (sd, node, &referenced_nodes);

    str_cat_splx_ttl_full(str, sd, node, "tsplx-data", 0);

    if (referenced_nodes.num_nodes > 0) {
        BINARY_TREE_FOR (cstr_to_splx_node_map, &referenced_nodes, curr_collected_node) {
            str_cat_c (str, "\n");
            assert (splx_node_has_name(curr_collected_node->value));
            str_cat_splx_ttl_full(str, sd, curr_collected_node->value, NULL, 0);
        }
    }
}

void print_splx_ttl (struct splx_data_t *sd, struct splx_node_t *node)
{
    string_t buff = {0};
    str_cat_splx_ttl (&buff, sd, node);
    printf ("%s", str_data(&buff));
    str_free (&buff);
}

void print_splx_canonical (struct splx_data_t *sd, struct splx_node_t *node)
{
    string_t buff = {0};
    str_cat_splx_canonical (&buff, sd, node);
    printf ("%s", str_data(&buff));
    str_free (&buff);
}

void print_splx_dump (struct splx_data_t *sd, struct splx_node_t *node)
{
    string_t buff = {0};
    str_cat_splx_dump (&buff, sd, node);
    printf ("%s", str_data(&buff));
    str_free (&buff);
}

struct splx_node_list_t* tps_subject_node_list_from_tmp_node (struct splx_data_t *sd, struct splx_node_t *node)
{
    struct splx_node_list_t *subject_node_list;

    struct splx_node_t *subject_node = cstr_to_splx_node_map_get (&sd->nodes, str_data(&node->str));
    if (str_len(&node->str) == 0 &&         // Node is unreferenceable
        node->attributes.num_nodes == 0 &&  // Has no triples
        node->floating_values != NULL)      // Has floating values
    {
        // Then we collapse the intermediate node, this makes the subject list
        // be the inner list of floating values.
        subject_node_list = node->floating_values;

    } else {
        if (subject_node == NULL) {
            subject_node = splx_node_dup (sd, node);
            splx_node_add (sd, subject_node);
        }

        subject_node_list = tps_wrap_in_list_node (sd, subject_node);
    }

    return subject_node_list;
}

#define tps_get_triple_node(tps,buffer,pos) _tps_get_buffered_node(tps,buffer,ARRAY_SIZE(buffer),pos)
struct splx_node_t* _tps_get_buffered_node(struct tsplx_parser_state_t *tps, struct splx_node_t *buffer, int buffer_len, int pos)
{
    struct splx_node_t *node = NULL;
    if (pos < buffer_len) {
        node = buffer + pos;
    } else {
        tps_error (tps, "triple buffer overflowed");
    }

    return node;
}

struct splx_node_list_t* tps_insert_subject (struct splx_data_t *sd,
                                             struct splx_node_t *object,
                                             char *predicate_str,
                                             struct splx_node_t *subject)
{
    struct splx_node_list_t *new_node_list_element = tps_wrap_in_list_node (sd, subject);
    cstr_to_splx_node_list_map_insert (&object->attributes, predicate_str, new_node_list_element);

    return new_node_list_element;
}

void tps_resolve_id_attribute(struct tsplx_parser_state_t *tps,
                              struct splx_data_t *sd,
                              struct splx_node_t *curr_object, struct splx_node_t *subject)
{
    if (str_len(&curr_object->str) == 0) {
        if (subject->type == SPLX_NODE_TYPE_OBJECT || subject->type == SPLX_NODE_TYPE_STRING) {
            str_set (&curr_object->str, str_data(&subject->str));
            cstr_to_splx_node_map_insert (&sd->nodes, str_data(&curr_object->str), curr_object);

        } else {
            // TODO: should we also somehow allows integer, float and boolean
            // IDs?. integers kind of make sense (but it's very likely they
            // should actually be some locally scoped Ids...), the other types,
            // don't make much sense.
            tps_warning (tps, "Unexpected id type.");
        }
    }
}

struct tsplx_scope_t {
    struct cstr_to_splx_node_map_t variables;
    struct cstr_to_splx_node_map_t used_variables;
};

bool tps_parse_node (struct tsplx_parser_state_t *tps, struct splx_node_t *root_object,
                     struct splx_data_t *sd,
                     struct tsplx_scope_t *scope)
{
    assert (scope != NULL);

    struct splx_node_t *curr_object = root_object;
    struct splx_node_list_t *curr_value = NULL;

    int triple_idx = 0;
    struct splx_node_t triple[3];
    for (int i=0; i<ARRAY_SIZE(triple); i++) {
        triple[i] = ZERO_INIT (struct splx_node_t);
    }

    while (!tps->is_eof && !tps->error) {
        tps_next (tps);
        if (tps_match(tps, TSPLX_TOKEN_TYPE_IDENTIFIER, NULL) || tps_match(tps, TSPLX_TOKEN_TYPE_URI, NULL)) {
            struct splx_node_t *node = tps_get_triple_node (tps, triple, triple_idx);
            if (node != NULL) {
                splx_node_clear (node);

                if (tps->token.value.len == 1 && *tps->token.value.s == '_') {
                    strn_set (&node->str, "", 0);
                } else {
                    strn_set (&node->str, tps->token.value.s, tps->token.value.len);
                }
                node->type = SPLX_NODE_TYPE_OBJECT;
                if (tps_match(tps, TSPLX_TOKEN_TYPE_URI, NULL)) {
                    node->uri_formatted_identifier = true;
                }
                triple_idx++;
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_VARIABLE, NULL)) {
            struct splx_node_t *node = tps_get_triple_node (tps, triple, triple_idx);

            string_t variable_name = {0};
            strn_set (&variable_name, tps->token.value.s, tps->token.value.len);

            struct splx_node_t *target_node = NULL;
            struct cstr_to_splx_node_map_node_t *tree_node = NULL;
            if (cstr_to_splx_node_map_lookup (&scope->variables, str_data(&variable_name), &tree_node)) {
                target_node = tree_node->value;

                // :string_pool
                char *variable_name_str = pom_strdup (&sd->pool, str_data(&variable_name));
                cstr_to_splx_node_map_insert (&scope->used_variables, variable_name_str, tree_node->value);

            } else {
                tps_error (tps, "use of undefined variable: %s", str_data(&variable_name));
            }

            if (!tps->error && node != NULL) {
                splx_node_clear (node);
                strn_set (&node->str, str_data(&target_node->str), str_len(&target_node->str));
                target_node->type = target_node->type;
                triple_idx++;
            }

            str_free (&variable_name);

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_MULTILINE_STRING, NULL)) {
            // TODO: Does this implementation of multiline strings work in
            // systems that use \r\n as line feed?.

            struct splx_node_t *node = tps_get_triple_node (tps, triple, triple_idx);
            if (node != NULL) {
                splx_node_clear (node);
                strn_set (&node->str, tps->token.value.s, tps->token.value.len);
                str_replace (&node->str, "\\n", "\n", NULL);
                str_replace (&node->str, "\\t", "\t", NULL);

                str_dedent(&node->str);

                node->type = SPLX_NODE_TYPE_STRING;
                triple_idx++;
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_STRING, NULL)) {
            struct splx_node_t *node = tps_get_triple_node (tps, triple, triple_idx);
            if (node != NULL) {
                splx_node_clear (node);
                strn_set (&node->str, tps->token.value.s, tps->token.value.len);
                str_replace (&node->str, "\\n", "\n", NULL);
                str_replace (&node->str, "\\t", "\t", NULL);
                node->type = SPLX_NODE_TYPE_STRING;
                triple_idx++;
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_SOFT_REFERENCE, NULL)) {
            struct splx_node_t *node = tps_get_triple_node (tps, triple, triple_idx);
            if (node != NULL) {
                splx_node_clear (node);
                strn_set (&node->str, tps->token.value.s, tps->token.value.len);
                node->type = SPLX_NODE_TYPE_SOFT_REFERENCE;
                triple_idx++;
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_NUMBER, NULL)) {
            struct splx_node_t *node = tps_get_triple_node (tps, triple, triple_idx);
            if (node != NULL) {
                splx_node_clear (node);
                strn_set (&node->str, tps->token.value.s, tps->token.value.len);
                node->type = SPLX_NODE_TYPE_INTEGER;
                triple_idx++;
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "[")) {
            struct splx_node_t *list_item = tps_get_triple_node (tps, triple, triple_idx);
            if (list_item != NULL) {
                splx_node_clear (list_item);
                list_item->type = SPLX_NODE_TYPE_OBJECT;
                triple_idx++;

                tps_parse_node (tps, list_item, sd, scope);

                if (!tps_match (tps, TSPLX_TOKEN_TYPE_OPERATOR, "]")) {
                    tps_error (tps, "unexpected end of nested object");
                }
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "{")) {
            tps_parse_node (tps, curr_object, sd, scope);

            if (!tps_match (tps, TSPLX_TOKEN_TYPE_OPERATOR, "}")) {
                tps_error (tps, "unexpected end of scope");
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "(")) {
            if (triple_idx == 1) {
                // Constructor signature
                struct splx_node_t *constructor_lhs = &triple[0];


                int parameter_idx = 0;
                tps_next (tps);
                while (!tps->error && !tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ")")) {
                    char *parameter_name_str = NULL;
                    struct splx_node_t *parameter = NULL;

                    if (tps_match(tps, TSPLX_TOKEN_TYPE_VARIABLE, NULL)) {
                        string_t parameter_name = {0};
                        strn_set (&parameter_name, tps->token.value.s, tps->token.value.len);

                        // :string_pool
                        parameter_name_str = pom_strdup (&sd->pool, str_data(&parameter_name));

                        string_t parameter_id = {0};
                        str_set_printf (&parameter_id, "__%s_p%i", str_data(&constructor_lhs->str), parameter_idx+1);
                        parameter = splx_node_get_or_create (sd, str_data(&parameter_id), SPLX_NODE_TYPE_OBJECT);
                        splx_node_attribute_add (sd,
                                                 parameter,
                                                 "a", "parameter", SPLX_NODE_TYPE_OBJECT);
                        splx_node_attribute_add (sd,
                                                 parameter,
                                                 "name", str_data (&parameter_name), SPLX_NODE_TYPE_STRING);

                        splx_node_attribute_append (sd,
                                                    constructor_lhs,
                                                    "has-parameter", parameter);

                    } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ",") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ")")) {
                        // Do nothing, move to next token.

                    } else {
                        tps_error (tps, "unexpected token in constructor signature parameters: '%.*s' (%s)", tps->token.value.len, tps->token.value.s, tsplx_token_type_names[tps->token.type]);
                    }

                    if (!tps->error) {
                        assert (parameter != NULL && parameter_name_str != NULL);
                        cstr_to_splx_node_map_insert (&scope->variables, parameter_name_str, parameter);
                    }

                    tps_next (tps);
                }

                if (!tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ")")) {
                    tps_error (tps, "unexpected end of parameter list in constructor signature");
                }

            } else {
                tps_error (tps, "constructor signature at unexpected position");
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "=")) {
            if (triple_idx == 1) {
                // Constructor invocation
                struct splx_node_t *target_node = &triple[0];
                if (cstr_to_splx_node_map_get (&sd->nodes, str_data(&target_node->str)) == NULL) {
                    struct splx_node_t *instance = splx_node_dup (sd, target_node);
                    splx_node_add (sd, instance);


                    // Optionally parse a constructor invocation
                    tps_next (tps);
                    if (tps_match(tps, TSPLX_TOKEN_TYPE_IDENTIFIER, NULL)) {
                        string_t invoked_constructor = {0};
                        strn_set (&invoked_constructor, tps->token.value.s, tps->token.value.len);

                        splx_node_attribute_add (sd,
                                                 instance,
                                                 "a", str_data (&invoked_constructor), SPLX_NODE_TYPE_OBJECT);

                        str_free (&invoked_constructor);

                        tps_next (tps);
                    }

                    if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "{")) {
                        tps_parse_node (tps, instance, sd, scope);

                        BINARY_TREE_FOR (cstr_to_splx_node_map, &scope->variables, curr_variable) {
                            if (!cstr_to_splx_node_map_lookup (&scope->used_variables, curr_variable->key, NULL)) {
                                splx_node_attribute_append (sd,
                                                            instance,
                                                            curr_variable->key,
                                                            curr_variable->value);

                            }
                        }

                        cstr_to_splx_node_map_destroy (&scope->variables);
                        cstr_to_splx_node_map_destroy (&scope->used_variables);
                        *scope = ZERO_INIT (struct tsplx_scope_t);

                        if (!tps_match (tps, TSPLX_TOKEN_TYPE_OPERATOR, "}")) {
                            tps_error (tps, "unexpected end of scope");
                        }
                    }


                    if (!tps->error) {
                        struct splx_node_list_t *subject_node_list = tps_wrap_in_list_node (sd, instance);
                        LINKED_LIST_APPEND (curr_object->floating_values, subject_node_list);
                        while (curr_object->floating_values_end->next != NULL) curr_object->floating_values_end = curr_object->floating_values_end->next;
                    }

                    triple_idx = 0;

                } else {
                    tps_error (tps, "duplicate instances. should instances be merged?, or is this very likely a mistake?");
                }

            } else {
                tps_error (tps, "constructor body at unexpected position");
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "}") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "]") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ",") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ".") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ";")) {
            if (triple_idx == 1) {
                struct splx_node_list_t *subject_node_list = tps_subject_node_list_from_tmp_node (sd, &triple[0]);

                if (curr_value != NULL && curr_value != curr_object->floating_values_end) {
                    curr_value->next = subject_node_list;

                } else {
                    // There is no current value to link to the value that was
                    // just read. Add new subject node to the current object as
                    // floating.
                    LINKED_LIST_APPEND (curr_object->floating_values, subject_node_list);
                    while (curr_object->floating_values_end->next != NULL) curr_object->floating_values_end = curr_object->floating_values_end->next;
                }

                curr_value = subject_node_list;
                while (curr_value->next != NULL) curr_value = curr_value->next;

                triple_idx = 0;

            } else if (triple_idx == 2) {
                if (triple[0].type == SPLX_NODE_TYPE_OBJECT) {
                    // Completed a triple with respect to the current object.

                    char *predicate_str = splx_get_node_id (sd, &triple[0]);
                    if (strcmp(predicate_str, "id") == 0) {
                        tps_resolve_id_attribute(tps, sd, curr_object, &triple[1]);

                    } else {
                        struct splx_node_list_t *subject_node_list = tps_subject_node_list_from_tmp_node (sd, &triple[1]);

                        // Look if this predicate is already used in the curent
                        // object, if it is, combine the value lists.
                        struct splx_node_list_t *existing_subject_list = cstr_to_splx_node_list_map_get (&curr_object->attributes, predicate_str);
                        if (existing_subject_list == NULL) {
                            cstr_to_splx_node_list_map_insert (&curr_object->attributes, predicate_str, subject_node_list);
                        } else {
                            while (existing_subject_list->next != NULL) existing_subject_list = existing_subject_list->next;
                            existing_subject_list->next = subject_node_list;
                        }

                        curr_value = subject_node_list;
                        while (curr_value->next != NULL) curr_value = curr_value->next;
                    }

                    triple_idx = 0;

                } else {
                    tps_error (tps, "unexpected predicate type");
                }

            } else if (triple_idx == 3) {
                struct splx_node_t *new_node = splx_node_dup (sd, &triple[0]);
                splx_node_add (sd, new_node);
                curr_object = new_node;

                char *predicate_str = splx_get_node_id (sd, &triple[1]);
                if (strcmp(predicate_str, "id") == 0) {
                    tps_resolve_id_attribute(tps, sd, curr_object, &triple[2]);

                } else {

                    struct splx_node_list_t *subject_node_list = tps_subject_node_list_from_tmp_node (sd, &triple[2]);

                    cstr_to_splx_node_list_map_insert (&new_node->attributes, predicate_str, subject_node_list);
                    curr_value = subject_node_list;
                    while (curr_value->next != NULL) curr_value = curr_value->next;

                    struct splx_node_list_t *new_node_list_element = tps_wrap_in_list_node (sd, new_node);
                    LINKED_LIST_APPEND (root_object->floating_values, new_node_list_element);

                }

                triple_idx = 0;
            }

            if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "}") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "]")) {
                break;
            }
        }
    }

    for (int i=0; i<ARRAY_SIZE(triple); i++) {
        str_free (&triple[i].str);
    }

    return !tps->error;
}

#define tsplx_parse_str_name(sd,str,error_msg) tsplx_parse_str_name_full(sd,str,NULL,error_msg)
bool tsplx_parse_str_name_full (struct splx_data_t *sd, char *str, struct splx_node_t *anchor, string_t *error_msg)
{
    struct tsplx_parser_state_t _tps = {0};
    struct tsplx_parser_state_t *tps = &_tps;
    tps_init (tps, str, error_msg);

    if (anchor == NULL) {
        assert (sd->root == NULL);
        sd->root = splx_node_new (sd);
        anchor = sd->root;
    }

    struct tsplx_scope_t scope = {0};
    bool success = tps_parse_node (tps, anchor, sd, &scope);
    cstr_to_splx_node_map_destroy (&scope.variables);
    cstr_to_splx_node_map_destroy (&scope.used_variables);

    // Why was I doing this?... it causes breakages because it frees a lot of
    // memory that we will be reusing if we just want to move on parsing other
    // stuff that's not broken... probably commenting this out is causing
    // ungraceful stoppage of parsing of TSPLX data producing an inconsistent
    // state?.
    //if (tps->error) {
    //    splx_destroy (sd);
    //    *sd = ZERO_INIT (struct splx_data_t);
    //}

    return success;
}

void tsplx_parse_name (struct splx_data_t *sd, char *path)
{
    size_t f_len;
    char *f = full_file_read (NULL, path, &f_len);
    tsplx_parse_str_name (sd, f, NULL);
    free (f);
}

void tsplx_parse (struct splx_data_t *sd, char *path) {
    string_t name = {0};
    str_set_printf (&name, "<file://%s>", path);
    tsplx_parse_name(sd, path);
    str_free (&name);
}

#define splx_get_value_cstr_arr(sd,pool,attr,arr,arr_len) splx_node_get_value_cstr_arr(sd,sd->root,pool,attr,arr,arr_len)
bool splx_node_get_value_cstr_arr (struct splx_data_t *sd, struct splx_node_t *node, mem_pool_t *pool, char *attr, char ***arr, int *arr_len)
{
    struct splx_node_list_t *value_list = cstr_to_splx_node_list_map_get (&node->attributes, attr);
    if (value_list != NULL) {
        int len = 0;
        LINKED_LIST_FOR (struct splx_node_list_t *, curr_node_0, value_list) {
            len++;
        }

        if (arr_len != NULL) *arr_len = len;
        *arr = mem_pool_push_array (pool, len, char*);

        int idx = 0;
        LINKED_LIST_FOR (struct splx_node_list_t *, curr_node_1, value_list) {
            (*arr)[idx++] = pom_strdup (pool, str_data(&curr_node_1->node->str));
        }
    }

    return false;
}

// Common Queries

string_t* splx_node_get_id (struct splx_node_t *node)
{
    return &node->str;
}

struct splx_node_t* splx_node_get_attribute (struct splx_node_t *node, char *attr)
{
    struct splx_node_list_t *values = splx_node_get_attributes(node, attr);
    assert(values->next == NULL);
    return values->node;
}

string_t* splx_node_get_name (struct splx_node_t *node)
{
    // TODO: If there are multiple names, don't just return the first one. When
    // reification is supported we should allow sorting by reification tags.
    struct splx_node_list_t *values = splx_node_get_attributes(node, "name");
    return &values->node->str;
}
