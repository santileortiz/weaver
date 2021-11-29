/*
 * Copyright (C) 2021 Santiago León O.
 */

BINARY_TREE_NEW (cstr_to_splx_node_map, char*, struct splx_node_t*, strcmp(a, b))

#define SPLX_NODE_TYPES_TABLE                  \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_UNKNOWN) \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_STRING)  \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_DOUBLE)  \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_INTEGER) \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_URI)     \
    SPLX_NODE_TYPE_ROW(SPLX_NODE_TYPE_OBJECT)  \

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

    struct cstr_to_splx_node_map_tree_t attributes;

    struct splx_node_t *floating_values;
    struct splx_node_t *floating_values_end;

    struct splx_node_t *next;
};

struct splx_data_t {
    mem_pool_t pool;

    struct cstr_to_splx_node_map_tree_t nodes;
    struct splx_node_t *root;
};

void splx_destroy (struct splx_data_t *sd)
{
    mem_pool_destroy (&sd->pool);
}

// CAUTION: DO NOT MODIFY THE RETURNED STRING! It's used as index to the nodes
// map.
char* splx_get_node_id (struct splx_data_t *sd, struct splx_node_t *node)
{
    // Add the predicate to the nodes map, but don't allocate an
    // actual node yet. So far it's only being used as predicate,
    // it's possible we won't need to create a node because no one
    // is using it as an object or subject.
    char *node_id_str = NULL;
    struct cstr_to_splx_node_map_tree_node_t *predicate_tree_node = NULL;
    if (!cstr_to_splx_node_map_tree_lookup (&sd->nodes, str_data(&node->str), &predicate_tree_node)) {
        // TODO: Should use a string pool...
        // :string_pool
        node_id_str = pom_strdup (&sd->pool, str_data(&node->str));
        cstr_to_splx_node_map_tree_insert (&sd->nodes, node_id_str, NULL);
    } else {
        node_id_str = predicate_tree_node->key;
    }

    assert (node_id_str != NULL);
    return node_id_str;
}

#define TSPLX_TOKEN_TYPES_TABLE                  \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_UNKNOWN)    \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_COMMENT)    \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_EMPTY_LINE) \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_IDENTIFIER) \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_STRING)     \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_NUMBER)     \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_OPERATOR)   \
    TOKEN_TYPES_ROW(TSPLX_TOKEN_TYPE_EOF)        \

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

    str_cat_printf (tps->error_msg, ECMA_RED("error: "));

    PRINTF_INIT (format, size, args);
    size_t old_len = str_len(tps->error_msg);
    str_maybe_grow (tps->error_msg, str_len(tps->error_msg) + size - 1, true);
    PRINTF_SET (str_data(tps->error_msg) + old_len, size, format, args);

    str_cat_c (tps->error_msg, "\n");
    tps->error = true;
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
    return !tps_is_eof(tps) && char_in_str(tps_curr_char(tps), ";,.[]{}");
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
    bool found = false;
    if (tps_is_digit(tps)) {
        found = true;
        while (tps_is_digit(tps)) {
            tps_advance_char (tps);
        }
    }
    return found;
}

sstring_t tps_advance_line(struct tsplx_parser_state_t *tps)
{
    char *start = tps->pos;
    while (!tps_is_eof(tps) && tps_curr_char(tps) != '\n') {
        tps_advance_char (tps);
    }

    // Advance over the \n character. We leave \n characters because they are
    // handled by the inline parser. Sometimes they are ignored (between URLs),
    // and sometimes they are replaced to space (multiline paragraphs).
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

    } else if (tps_match_str(tps, "\"")) {
        char *start = tps->pos;
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

    } else if (tps_is_operator(tps)) {
        tok->type = TSPLX_TOKEN_TYPE_OPERATOR;
        tok->value = SSTRING(tps->pos, 1);
        tps_advance_char (tps);

    } else {
        char *start = tps->pos;
        // TODO: Maybe better define a set of valid identifier characters?.
        // Reserve : for prefixes, don't allow identifiers starting with
        // numbers.
        while (!tps_is_eof(tps) && !tps_is_operator(tps) && !tps_is_space(tps) && tps_curr_char(tps) != '\n') {
            tps_advance_char (tps);
        }

        tok->value = SSTRING(start, tps->pos - start);
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

struct splx_node_t* splx_node_new (struct splx_data_t *sd)
{
    struct splx_node_t *new_node = mem_pool_push_struct (&sd->pool, struct splx_node_t);
    *new_node = ZERO_INIT (struct splx_node_t);
    str_pool (&sd->pool, &new_node->str);
    return new_node;
}

void splx_node_clear (struct splx_node_t *node)
{
    node->type = SPLX_NODE_TYPE_UNKNOWN;
    strn_set (&node->str, "", 0);

    cstr_to_splx_node_map_tree_destroy (&node->attributes);
    node->attributes = ZERO_INIT (struct cstr_to_splx_node_map_tree_t);

    node->floating_values = NULL;
    node->floating_values_end = NULL;
    node->next = NULL;
}

void splx_node_cpy (struct splx_node_t *tgt, struct splx_node_t *src)
{
    // Backup the string so we don't lose the allocated pointer if there's one
    string_t tmp_str = tgt->str;

    // Backup the attributes tree
    struct cstr_to_splx_node_map_tree_t tmp_tree = tgt->attributes;

    *tgt = *src;

    // Restore the original string and do a string copy
    tgt->str = tmp_str;
    str_set (&tgt->str, str_data(&src->str));

    // Restore the original attributes tree, clear it and add all elements of
    // source
    tgt->attributes = tmp_tree;
    cstr_to_splx_node_map_tree_destroy (&tgt->attributes);
    tgt->attributes = ZERO_INIT (struct cstr_to_splx_node_map_tree_t);
    BINARY_TREE_FOR (cstr_to_splx_node_map, &src->attributes, curr_attribute) {
        cstr_to_splx_node_map_tree_insert (&tgt->attributes, curr_attribute->key, curr_attribute->value);
    }
}

#define SPLX_STR_INDENT 2

#define str_cat_splx_node(str,sd,node) str_cat_splx_node_full(str,sd,node,0)
void str_cat_splx_node_full (string_t *str, struct splx_data_t *sd, struct splx_node_t *node, int curr_indent)
{
    str_cat_debugstr (str, curr_indent, ESC_COLOR_DEFAULT, str_data(&node->str));

    if (node->attributes.num_nodes > 0) {
        BINARY_TREE_FOR (cstr_to_splx_node_map, &node->attributes, curr_attribute) {
            str_cat_debugstr (str, curr_indent+SPLX_STR_INDENT, ESC_COLOR_DEFAULT, curr_attribute->key);
            LINKED_LIST_FOR (struct splx_node_t *, curr_node, curr_attribute->value) {
                str_cat_debugstr (str, curr_indent+2*SPLX_STR_INDENT, ESC_COLOR_DEFAULT, str_data(&curr_node->str));
            }
        }
    }
}

void print_splx_node (struct splx_data_t *sd, struct splx_node_t *node)
{
    string_t buff = {0};
    str_cat_splx_node (&buff, sd, node);
    printf ("%s", str_data(&buff));
    str_free (&buff);
}

static inline
bool splx_node_has_name (struct splx_node_t *node)
{
    return str_len(&node->str) > 0;
}

static inline
bool splx_node_is_root (struct splx_data_t *sd, struct splx_node_t *node)
{
    return sd->root == node;
}

#define str_cat_splx_dump(str,sd,node) str_cat_splx_dump_full(str,sd,node,0)
void str_cat_splx_dump_full (string_t *str, struct splx_data_t *sd, struct splx_node_t *node, int curr_indent)
{
    // TODO: Avoid infinite loops if there is a cycle. Keep a map of printed
    // nodes and don't recurse down to the same node more than once.

    str_cat_indented_printf (str, curr_indent, ECMA_S_CYAN(0, "%p") "\n", node);
    str_cat_indented_printf (str, curr_indent, "TYPE: %s\n", splx_node_type_names[node->type]);
    str_cat_indented_c (str, "STR: ", curr_indent);
    str_cat_debugstr (str, 0, ESC_COLOR_YELLOW, str_data(&node->str));

    if (node->type == SPLX_NODE_TYPE_OBJECT ||
        node->attributes.num_nodes > 0 ||
        node->floating_values != NULL)
    {
        str_cat_indented_printf (str, curr_indent, "ATTRIBUTES: %d\n", node->attributes.num_nodes);
        BINARY_TREE_FOR (cstr_to_splx_node_map, &node->attributes, curr_attribute) {
            str_cat_debugstr (str, curr_indent + SPLX_STR_INDENT, ESC_COLOR_GREEN, curr_attribute->key);
            str_cat_splx_dump_full (str, sd, curr_attribute->value, curr_indent + 2*SPLX_STR_INDENT);
        }

        str_cat_indented_c (str, "FLOATING:", curr_indent);
        if (node->floating_values != NULL) {
            LINKED_LIST_FOR (struct splx_node_t *, curr_node, node->floating_values) {
                str_cat_c (str, "\n");
                str_cat_splx_dump_full (str, sd, curr_node, curr_indent + SPLX_STR_INDENT);
            }
        } else {
            str_cat_c (str, " (none)\n");
        }
    }
}

void str_cat_literal_node (string_t *str, struct splx_node_t *node)
{
    if (node->type == SPLX_NODE_TYPE_STRING) {
        string_t buff = {0};

        str_cpy (&buff, &node->str);
        str_replace (&buff, "\"", "\\\"", NULL);
        str_cat_printf (str, "\"%s\"", str_data(&buff));

        str_free (&buff);

    } else if (node->type == SPLX_NODE_TYPE_INTEGER) {
        str_cat_printf (str, "%s", str_data(&node->str));

    } else {
        str_cat_c (str, " ?");
    }
}

void str_cat_splx_canonical_full (string_t *str, struct splx_data_t *sd, struct splx_node_t *node, int curr_indent);

void tps_cat_floating_literals (string_t *str, struct splx_data_t *sd, struct splx_node_t *node, struct splx_node_t *floating_objects, int curr_indent)
{
    bool is_first_floating = true;
    enum splx_node_type_t prev_type = SPLX_NODE_TYPE_UNKNOWN;
    LINKED_LIST_FOR (struct splx_node_t*, curr_node, node->floating_values) {
        if (curr_node == floating_objects) break;

        if (curr_node->type != prev_type && !is_first_floating && str_last(str) != '\n') {
            str_cat_c (str, "\n");
        }

        if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
            str_cat_indented_c (str, "[\n", curr_indent);
            str_cat_splx_canonical_full(str,sd,curr_node, curr_indent + SPLX_STR_INDENT);
            str_cat_indented_c (str, "]", curr_indent);

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

#define str_cat_splx_canonical(str,sd,node) str_cat_splx_canonical_full(str,sd,node,0)
void str_cat_splx_canonical_full (string_t *str, struct splx_data_t *sd, struct splx_node_t *node, int curr_indent)
{
    bool is_triple = false;
    if (splx_node_has_name (node)) {
        str_cat_indented_c (str, str_data(&node->str), curr_indent);
        is_triple = true;

    } else if (sd->root != node) {
        str_cat_indented_c (str, "_", curr_indent);
        is_triple = true;
    }

    if (node->attributes.num_nodes == 0 && node->floating_values == NULL) {
        str_cat_c (str, " ;");
    }

    struct splx_node_t *floating_objects = NULL;
    enum splx_node_type_t prev_type = SPLX_NODE_TYPE_UNKNOWN;
    if ((node->attributes.num_nodes == 0 || !splx_node_has_name (node))) {
        LINKED_LIST_FOR (struct splx_node_t*, curr_node, node->floating_values) {
            if (curr_node->type == SPLX_NODE_TYPE_OBJECT && prev_type != SPLX_NODE_TYPE_OBJECT) {
                floating_objects = curr_node;
            }
            prev_type = curr_node->type;
        }

        if (!is_triple) {
            if (node->floating_values != floating_objects) {
                tps_cat_floating_literals (str, sd, node, floating_objects, curr_indent);
            }

            if (node->floating_values != NULL && node->floating_values->type != SPLX_NODE_TYPE_OBJECT) {
                str_cat_c (str, "\n\n");
            }
        }

    } else {
        floating_objects = node->floating_values;
    }

    if (node->attributes.num_nodes > 0) {
        int attr_cnt = 0;
        bool is_first_predicate = true;
        BINARY_TREE_FOR (cstr_to_splx_node_map, &node->attributes, curr_attribute) {
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
            LINKED_LIST_FOR (struct splx_node_t *, curr_node, curr_attribute->value) {
                if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
                    str_cat_printf (str, " %s", str_data(&curr_node->str));

                } else {
                    if (is_first_value) {
                        str_cat_c (str, " ");

                    } else {
                        int indent_levels = 1;
                        if (is_triple) indent_levels = 2;
                        str_cat_char (str, ' ', curr_indent + indent_levels*SPLX_STR_INDENT);
                    }

                    str_cat_literal_node (str, curr_node);

                    if (curr_node->next != NULL) {
                        str_cat_c (str, " ;\n");
                    }

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
            tps_cat_floating_literals (str, sd, node, floating_objects, curr_indent + 2*SPLX_STR_INDENT);

            if (str_last(str) != '\n') {
                str_cat_c (str, "\n");
            }
        }

        LINKED_LIST_FOR (struct splx_node_t*, curr_node, floating_objects) {
            if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
                if (!is_first_floating_object) {
                    str_cat_c (str, "\n");
                }
                str_cat_splx_canonical_full(str,sd,curr_node,curr_indent + floating_indent);

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

void str_cat_literal_node_ttl (string_t *str, struct splx_node_t *node)
{
    if (node->type == SPLX_NODE_TYPE_STRING) {
        string_t buff = {0};

        str_cpy (&buff, &node->str);
        str_replace (&buff, "\"", "\\\"", NULL);
        str_cat_printf (str, "\"%s\"", str_data(&buff));

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
        BINARY_TREE_FOR (cstr_to_splx_node_map, &node->attributes, curr_attribute) {
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
            LINKED_LIST_FOR (struct splx_node_t *, curr_node, curr_attribute->value) {
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

                if (curr_node->next != NULL) {
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
        LINKED_LIST_FOR (struct splx_node_t*, curr_node, node->floating_values) {
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

            if (curr_node->next != NULL) {
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
void tps_collect_referenced_node (struct splx_node_t *node, struct cstr_to_splx_node_map_tree_t *referenced_nodes)
{
    if (splx_node_has_name(node) &&
        (node->attributes.num_nodes > 0 || node->floating_values != NULL))
    {
        cstr_to_splx_node_map_tree_insert (referenced_nodes, str_data(&node->str), node);
    }
}

void tps_collect_referenced_nodes (struct splx_data_t *sd, struct splx_node_t *node, struct cstr_to_splx_node_map_tree_t *referenced_nodes)
{
    if (node->attributes.num_nodes > 0) {
        BINARY_TREE_FOR (cstr_to_splx_node_map, &node->attributes, curr_attribute) {
            struct cstr_to_splx_node_map_tree_node_t *tree_node = NULL;
            if (cstr_to_splx_node_map_tree_lookup (&sd->nodes, curr_attribute->key, &tree_node)) {
                if (tree_node->value != NULL) {
                    tps_collect_referenced_node (tree_node->value, referenced_nodes);
                }
            }

            LINKED_LIST_FOR (struct splx_node_t *, curr_node, curr_attribute->value) {
                if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
                    if (!cstr_to_splx_node_map_tree_lookup (referenced_nodes, str_data(&curr_node->str), NULL)) {
                        tps_collect_referenced_nodes (sd, curr_node, referenced_nodes);
                    }

                    tps_collect_referenced_node (curr_node, referenced_nodes);
                }
            }
        }
    }

    LINKED_LIST_FOR (struct splx_node_t*, curr_node, node->floating_values) {
        if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
            if (!cstr_to_splx_node_map_tree_lookup (referenced_nodes, str_data(&curr_node->str), NULL)) {
                tps_collect_referenced_nodes (sd, curr_node, referenced_nodes);
            }

            tps_collect_referenced_node (curr_node, referenced_nodes);
        }
    }
}

void str_cat_splx_ttl (string_t *str, struct splx_data_t *sd, struct splx_node_t *node)
{
    struct cstr_to_splx_node_map_tree_t referenced_nodes = {0};

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

struct splx_node_t* tps_subject_node_from_tmp_node (struct splx_data_t *sd, struct splx_node_t *node)
{
    struct splx_node_t *subject_node = cstr_to_splx_node_map_get (&sd->nodes, str_data(&node->str));
    if (str_len(&node->str) == 0 &&         // Node is unreferenceable
        node->attributes.num_nodes == 0 &&  // Has no triples
        node->floating_values != NULL)      // Has floating values
    {
        // Then we collapse the intermediate node, such that we
        // only use the floating values as subject.
        subject_node = node->floating_values;

    } else if (subject_node == NULL) {
        subject_node = splx_node_new (sd);
        splx_node_cpy (subject_node, node);

        if (str_len(&node->str) > 0) {
            cstr_to_splx_node_map_tree_insert (&sd->nodes, str_data(&subject_node->str), subject_node);
        }
    }

    return subject_node;
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

bool tps_parse_node (struct tsplx_parser_state_t *tps, struct splx_node_t *root_object,
                     struct splx_data_t *sd)
{
    struct splx_node_t *curr_object = root_object;
    struct splx_node_t *curr_value = NULL;

    int triple_idx = 0;
    struct splx_node_t triple[3];
    for (int i=0; i<ARRAY_SIZE(triple); i++) {
        triple[i] = ZERO_INIT (struct splx_node_t);
    }

    while (!tps->is_eof && !tps->error) {
        tps_next (tps);
        if (tps_match(tps, TSPLX_TOKEN_TYPE_IDENTIFIER, NULL)) {
            struct splx_node_t *node = tps_get_triple_node (tps, triple, triple_idx);
            if (node != NULL) {
                splx_node_clear (node);
                if (tps->token.value.len == 1 && *tps->token.value.s == '_') {
                    strn_set (&node->str, "", 0);
                } else {
                    strn_set (&node->str, tps->token.value.s, tps->token.value.len);
                }
                node->type = SPLX_NODE_TYPE_OBJECT;
                triple_idx++;
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_STRING, NULL)) {
            struct splx_node_t *node = tps_get_triple_node (tps, triple, triple_idx);
            if (node != NULL) {
                splx_node_clear (node);
                strn_set (&node->str, tps->token.value.s, tps->token.value.len);
                node->type = SPLX_NODE_TYPE_STRING;
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

                tps_parse_node (tps, list_item, sd);

                if (!tps_match (tps, TSPLX_TOKEN_TYPE_OPERATOR, "]")) {
                    tps_error (tps, "unexpected end of nested object");
                }
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "{")) {
            tps_parse_node (tps, curr_object, sd);

            if (!tps_match (tps, TSPLX_TOKEN_TYPE_OPERATOR, "}")) {
                tps_error (tps, "unexpected end of scope");
            }

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "}") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "]") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ",") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ".") || tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ";")) {
            if (triple_idx == 1) {
                struct splx_node_t *subject_node = tps_subject_node_from_tmp_node (sd, &triple[0]);

                if (curr_value != curr_object->floating_values_end) {
                    curr_value->next = subject_node;

                } else {
                    // There is no current value to link to the value that was
                    // just read. Add new subject node to the current object as
                    // floating.
                    LINKED_LIST_APPEND (curr_object->floating_values, subject_node);
                }

                curr_value = subject_node;
                triple_idx = 0;

            } else if (triple_idx == 2) {
                if (triple[0].type == SPLX_NODE_TYPE_OBJECT) {
                    // Completed a triple with respect to the current object.

                    char *predicate_str = splx_get_node_id (sd, &triple[0]);
                    struct splx_node_t *subject_node = tps_subject_node_from_tmp_node (sd, &triple[1]);

                    cstr_to_splx_node_map_tree_insert (&curr_object->attributes, predicate_str, subject_node);

                    curr_value = subject_node;

                    triple_idx = 0;

                } else {
                    tps_error (tps, "unexpected predicate type");
                }

            } else if (triple_idx == 3) {
                struct splx_node_t *new_node = splx_node_new (sd);
                splx_node_cpy (new_node, &triple[0]);
                if (splx_node_has_name (new_node)) {
                    cstr_to_splx_node_map_tree_insert (&sd->nodes, str_data(&new_node->str), new_node);
                }

                char *predicate_str = splx_get_node_id (sd, &triple[1]);
                struct splx_node_t *subject_node = tps_subject_node_from_tmp_node (sd, &triple[2]);

                cstr_to_splx_node_map_tree_insert (&new_node->attributes, predicate_str, subject_node);
                LINKED_LIST_APPEND (root_object->floating_values, new_node);
                curr_object = new_node;
                curr_value = subject_node;

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

bool tsplx_parse_str_name (struct splx_data_t *sd, char *str, string_t *error_msg)
{
    struct tsplx_parser_state_t _tps = {0};
    struct tsplx_parser_state_t *tps = &_tps;
    tps_init (tps, str, error_msg);

    assert (sd->root == NULL);
    sd->root = splx_node_new (sd);

    bool success = tps_parse_node (tps, sd->root, sd);

    if (tps->error) {
        splx_destroy (sd);
        *sd = ZERO_INIT (struct splx_data_t);
    }

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
    struct splx_node_t *value = cstr_to_splx_node_map_get (&node->attributes, attr);
    if (value != NULL) {
        int len = 0;
        LINKED_LIST_FOR (struct splx_node_t *, curr_node_0, value) {
            len++;
        }

        if (arr_len != NULL) *arr_len = len;
        *arr = mem_pool_push_array (pool, len, char*);

        int idx = 0;
        LINKED_LIST_FOR (struct splx_node_t *, curr_node_1, value) {
            (*arr)[idx++] = pom_strdup (pool, str_data(&curr_node_1->str));
        }
    }

    return false;
}
