/*
 * Copyright (C) 2021 Santiago León O.
 */

BINARY_TREE_NEW (cstr_to_splx_value_map, char*, struct splx_value_t*, strcmp(a, b))

struct splx_value_t {
    string_t id;

    struct cstr_to_splx_value_map_tree_t attributes;
    struct splx_value_t *floating_values;;

    struct splx_value_t *next;
};

struct splx_data_t {
    mem_pool_t pool;

    struct splx_value_t *root;
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
    return !tps_is_eof(tps) && char_in_str(tps_curr_char(tps), ",[]{}");
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

void tsplx_print_tokens (struct splx_data_t *sd, char *path)
{
    size_t f_len;
    char *f = full_file_read (NULL, path, &f_len);

    struct tsplx_parser_state_t _tps = {0};
    struct tsplx_parser_state_t *tps = &_tps;
    tps_init (tps, f);

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

    free (f);
}

void tsplx_parse (struct splx_data_t *sd, char *path)
{
    size_t f_len;
    char *f = full_file_read (NULL, path, &f_len);

    struct tsplx_parser_state_t _tps = {0};
    struct tsplx_parser_state_t *tps = &_tps;
    tps_init (tps, f);

    while (!tps->is_eof && !tps->error) {
        tps_next (tps);
    }

    free (f);
}

bool splx_get_value_cstr_arr (struct splx_data_t *sd, mem_pool_t *pool, char *attr, char ***arr, int *arr_len)
{
    return false;
}
