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
};

void tps_init (struct tsplx_parser_state_t *tps, char *str)
{
    tps->str = str;
    tps->pos = str;
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

    if (tps_is_eof(tps)) {
        tps->is_eof = true;
        tok->type = TSPLX_TOKEN_TYPE_EOF;

    } else if (tps_match_str(tps, "//")) {
        tok->type = TSPLX_TOKEN_TYPE_COMMENT;
        tok->value = tps_advance_line (tps);

    } else if (tps_match_digits(tps)) {
        // TODO: Process number tokens

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
        while (!tps_is_eof(tps) && !tps_is_operator(tps) && !tps_is_space(tps)) {
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
    tps_init (tps, tsplx_str);

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

void splx_node_cpy (struct splx_node_t *tgt, struct splx_node_t *src)
{
    // Backup the string so we don't lose the allocated pointer if there's one
    string_t tmp = tgt->str;

    *tgt = *src;

    // Restore the original string and do a string copy
    tgt->str = tmp;
    str_set (&tgt->str, str_data(&src->str));

    // TODO: I think string parameters are the only ones that would cause memory
    // leaks when copying by using an assignment. Still, I'm not 100% sure about
    // the tree.
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

#define str_cat_splx_expanded(str,sd,node) str_cat_splx_expanded_full(str,sd,node,0)
void str_cat_splx_expanded_full (string_t *str, struct splx_data_t *sd, struct splx_node_t *node, int curr_indent)
{
    string_t buff = {0};
    str_cat_indented_c (str, str_data(&node->str), curr_indent);

    if (node->attributes.num_nodes > 0) {
        int attr_cnt = 0;
        BINARY_TREE_FOR (cstr_to_splx_node_map, &node->attributes, curr_attribute) {
            str_cat_indented_c (str, curr_attribute->key, curr_indent+SPLX_STR_INDENT);
            bool is_first_value = true;
            LINKED_LIST_FOR (struct splx_node_t *, curr_node, curr_attribute->value) {
                if (curr_node->type == SPLX_NODE_TYPE_STRING) {
                    str_cpy (&buff, &curr_node->str);
                    str_replace (&buff, "\"", "\\\"", NULL);

                    if (is_first_value) {
                        str_cat_printf (str, " \"%s\"", str_data(&buff));
                    } else {
                        str_cat_indented_printf (str, curr_indent+2*SPLX_STR_INDENT, "\"%s\"", str_data(&buff));
                    }
                    is_first_value = false;

                    if (curr_node->next != NULL) {
                        str_cat_c (str, " ,\n");
                    }

                } else if (curr_node->type == SPLX_NODE_TYPE_OBJECT) {
                    str_cat_indented_c (str, str_data(&buff), curr_indent+2*SPLX_STR_INDENT);
                }
            }

            attr_cnt++;
            if (attr_cnt < node->attributes.num_nodes) {
                str_cat_c (str, " ;\n");
            }
        }

        str_cat_c (str, " .\n");
    }
    str_free (&buff);
}

void print_splx_expanded (struct splx_data_t *sd, struct splx_node_t *node)
{
    string_t buff = {0};
    str_cat_splx_expanded (&buff, sd, node);
    printf ("%s", str_data(&buff));
    str_free (&buff);
}

void tps_parse_node (struct tsplx_parser_state_t *tps, struct splx_node_t *node)
{
    if (tps_match(tps, TSPLX_TOKEN_TYPE_IDENTIFIER, NULL) || tps_match(tps, TSPLX_TOKEN_TYPE_STRING, NULL)) {
        strn_set (&node->str, tps->token.value.s, tps->token.value.len);

        if (tps_match(tps, TSPLX_TOKEN_TYPE_IDENTIFIER, NULL)) {
            node->type = SPLX_NODE_TYPE_OBJECT;
        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_STRING, NULL)) {
            node->type = SPLX_NODE_TYPE_STRING;
        }
    }
}

bool tsplx_parse_str_name (struct splx_data_t *sd, char *str, char* root_name, string_t *error_msg)
{
    struct tsplx_parser_state_t _tps = {0};
    struct tsplx_parser_state_t *tps = &_tps;
    tps_init (tps, str);

    assert (sd->root == NULL);
    sd->root = splx_node_new (sd);
    str_set (&sd->root->str, root_name);

    int triple_idx = 0;
    struct splx_node_t triple[3];
    for (int i=0; i<3; i++) {
        triple[i] = ZERO_INIT (struct splx_node_t);
    }

    struct splx_node_t *curr_object = sd->root;
    while (!tps->is_eof && !tps->error) {
        tps_next (tps);
        if (tps_match(tps, TSPLX_TOKEN_TYPE_IDENTIFIER, NULL) || tps_match(tps, TSPLX_TOKEN_TYPE_STRING, NULL)) {
            struct splx_node_t *node = triple + triple_idx;
            tps_parse_node (tps, node);
            strn_set (&node->str, tps->token.value.s, tps->token.value.len);
            triple_idx++;

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, "[")) {
            struct splx_node_t *list_item = triple + triple_idx;
            do {
                tps_next (tps);
                tps_parse_node (tps, list_item);

                tps_next (tps);
                if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ",")) {
                    list_item->next = splx_node_new (sd);
                    list_item = list_item->next;
                } else {
                    break;
                }
            } while (!tps->error);

            if (!tps_match (tps, TSPLX_TOKEN_TYPE_OPERATOR, "]")) {
                // error: unexpected end of list
            }

            triple_idx++;

        } else if (tps_match(tps, TSPLX_TOKEN_TYPE_OPERATOR, ".")) {
            if (triple_idx == 2) {
                if (triple[0].type == SPLX_NODE_TYPE_OBJECT) {
                    // Completed a triple with respect to the current object.

                    // Add the predicate to the nodes map, but don't allocate an
                    // actual node yet. So far it's only being used as predicate,
                    // it's possible we won't need to create a node because no one
                    // is using it as an object or subject.
                    char *predicate_str = NULL;
                    if (cstr_to_splx_node_map_get (&sd->nodes, str_data(&triple[0].str)) == NULL) {
                        // TODO: Should use a string pool...
                        predicate_str = pom_strdup (&sd->pool, str_data(&triple[0].str));
                        cstr_to_splx_node_map_tree_insert (&sd->nodes, predicate_str, NULL);
                    }

                    struct splx_node_t *subject_node = cstr_to_splx_node_map_get (&sd->nodes, str_data(&triple[1].str));
                    if (subject_node == NULL) {
                        subject_node = splx_node_new (sd);
                        splx_node_cpy (subject_node, &triple[1]);
                        cstr_to_splx_node_map_tree_insert (&sd->nodes, str_data(&subject_node->str), subject_node);
                    }

                    cstr_to_splx_node_map_tree_insert (&curr_object->attributes, predicate_str, subject_node);

                    triple_idx = 0;

                } else {
                    // error: unexpected predicate type
                }
            }
        }
    }

    for (int i=0; i<3; i++) {
        str_free (&triple[i].str);
    }

    return true;
}

void tsplx_parse_name (struct splx_data_t *sd, char *path, char* root_name)
{
    size_t f_len;
    char *f = full_file_read (NULL, path, &f_len);
    tsplx_parse_str_name (sd, f, root_name, NULL);
    free (f);
}

void tsplx_parse (struct splx_data_t *sd, char *path) {
    string_t name = {0};
    str_set_printf (&name, "<file://%s>", path);
    tsplx_parse_name(sd, path, str_data(&name));
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
