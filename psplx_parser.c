/*
 * Copyright (C) 2021 Santiago León O.
 */

#include <limits.h>
#include "tsplx_parser.h"
#include "html_builder.h"
#include "lib/regexp.h"
#include "lib/regexp.c"

#include "js.c"
#include "tsplx_parser.c"

struct note_t;

struct note_link_t {
    struct note_t *note;
    struct note_link_t *next;
};

struct note_t {
    string_t path;

    char *id;
    string_t title;
    string_t psplx;

    struct psx_block_t *tree;

    bool is_html_valid;
    string_t html;

    bool error;
    string_t error_msg;

    struct note_link_t *links;
    struct note_link_t *back_links;

    struct note_t *next;
};


int psx_content_width = 588; // px

struct psx_parser_ctx_t {
    char *id;
    char *path;
    struct file_vault_t *vlt;
    struct splx_data_t *sd;

    string_t *error_msg;
};

struct psx_parser_state_t {
    struct block_allocation_t block_allocation;

    mem_pool_t pool;

    struct psx_parser_ctx_t ctx;

    bool error;

    bool is_eof;
    bool is_eol;
    bool is_peek;
    char *str;

    char *pos;
    char *pos_peek;

    int line_number;
    int column_number;

    struct psx_token_t token;
    struct psx_token_t token_peek;

    DYNAMIC_ARRAY_DEFINE (struct psx_block_t*, block_stack);
    DYNAMIC_ARRAY_DEFINE (struct psx_block_unit_t*, block_unit_stack);
};

void ps_init (struct psx_parser_state_t *ps, char *str)
{
    ps->str = str;
    ps->pos = str;

    DYNAMIC_ARRAY_INIT (&ps->pool, ps->block_stack, 100);
    DYNAMIC_ARRAY_INIT (&ps->pool, ps->block_unit_stack, 100);
}

void ps_destroy (struct psx_parser_state_t *ps)
{
    mem_pool_destroy (&ps->pool);
}

static inline
char pps_curr_char (struct psx_parser_state_t *ps)
{
    return *(ps->pos);
}

static inline
char pps_prev_char (struct psx_parser_state_t *ps)
{
    return *(ps->pos-1);
}

static inline
bool pps_is_eof (struct psx_parser_state_t *ps)
{
    return ps->is_eof || pps_curr_char(ps) == '\0';
}

static inline
bool pps_is_digit (struct psx_parser_state_t *ps)
{
    return !pps_is_eof(ps) && char_in_str(pps_curr_char(ps), "1234567890");
}

static inline
bool pps_is_space (struct psx_parser_state_t *ps)
{
    return !pps_is_eof(ps) && is_space(ps->pos);
}

static inline
bool char_is_operator (char c)
{
    return char_in_str(c, ",=[]{}\\|`");
}

static inline
bool pps_is_operator (struct psx_parser_state_t *ps)
{
    return !pps_is_eof(ps) && char_is_operator(pps_curr_char(ps));
}

static inline
void pps_advance_char (struct psx_parser_state_t *ps)
{
    if (!pps_is_eof(ps)) {
        ps->pos++;

        ps->column_number++;

        if (*(ps->pos) == '\n') {
            ps->line_number++;
            ps->column_number = 0;
        }

    } else {
        ps->is_eof = true;
    }
}

// NOTE: c_str MUST be null terminated!!
bool pps_match_str(struct psx_parser_state_t *ps, char *c_str)
{
    char *backup_pos = ps->pos;

    bool found = true;
    while (*c_str) {
        if (pps_curr_char(ps) != *c_str) {
            found = false;
            break;
        }

        pps_advance_char (ps);
        c_str++;
    }

    if (!found) ps->pos = backup_pos;

    return found;
}

static inline
bool pps_match_digits (struct psx_parser_state_t *ps)
{
    bool found = false;
    if (pps_is_digit(ps)) {
        found = true;
        while (pps_is_digit(ps)) {
            pps_advance_char (ps);
        }
    }
    return found;
}

sstring_t pps_advance_line(struct psx_parser_state_t *ps)
{
    char *start = ps->pos;
    while (!pps_is_eof(ps) && pps_curr_char(ps) != '\n') {
        pps_advance_char (ps);
    }

    // Advance over the \n character. We leave \n characters because they are
    // handled by the inline parser. Sometimes they are ignored (between URLs),
    // and sometimes they are replaced to space (multiline paragraphs).
    pps_advance_char (ps);

    return SSTRING(start, ps->pos - start);
}

static inline
void pps_consume_spaces (struct psx_parser_state_t *ps)
{
    while (pps_is_space(ps)) {
        pps_advance_char (ps);
    }
}

BINARY_TREE_NEW (sstring_map, sstring_t, sstring_t, strncmp(a.s, b.s, MIN(a.len, b.len)))

struct sstring_ll_t {
    sstring_t v;
    struct sstring_ll_t *next;
};

struct psx_tag_parameters_t {
    struct sstring_ll_t *positional;
    struct sstring_ll_t *positional_end;

    struct sstring_map_t named;
};

GCC_PRINTF_FORMAT(2, 3)
void psx_error (struct psx_parser_state_t *ps, char *format, ...)
{
    if (ps->ctx.path != NULL) {
         str_cat_printf (ps->ctx.error_msg, ECMA_DEFAULT("%s:") ECMA_RED(" error: "), ps->ctx.path);
    } else {
         str_cat_printf (ps->ctx.error_msg, ECMA_RED(" error: "));
    }

    PRINTF_INIT (format, size, args);
    size_t old_len = str_len(ps->ctx.error_msg);
    str_maybe_grow (ps->ctx.error_msg, str_len(ps->ctx.error_msg) + size - 1, true);
    PRINTF_SET (str_data(ps->ctx.error_msg) + old_len, size, format, args);

    str_cat_c (ps->ctx.error_msg, "\n");
    ps->error = true;
}

GCC_PRINTF_FORMAT(2, 3)
void psx_warning (struct psx_parser_state_t *ps, char *format, ...)
{
    if (ps->ctx.path != NULL) {
         str_cat_printf (ps->ctx.error_msg, ECMA_DEFAULT("%s:") ECMA_YELLOW(" warning: "), ps->ctx.path);
    } else {
         str_cat_printf (ps->ctx.error_msg, ECMA_YELLOW("warning: "));
    }

    PRINTF_INIT (format, size, args);
    size_t old_len = str_len(ps->ctx.error_msg);
    str_maybe_grow (ps->ctx.error_msg, str_len(ps->ctx.error_msg) + size - 1, true);
    PRINTF_SET (str_data(ps->ctx.error_msg) + old_len, size, format, args);

    str_cat_c (ps->ctx.error_msg, "\n");
}

GCC_PRINTF_FORMAT(2, 3)
void psx_warning_ctx (struct psx_parser_ctx_t *ctx, char *format, ...)
{
    if (ctx->path != NULL) {
         str_cat_printf (ctx->error_msg, ECMA_DEFAULT("%s:") ECMA_YELLOW(" warning: "), ctx->path);
    } else {
         str_cat_printf (ctx->error_msg, ECMA_YELLOW("warning: "));
    }

    PRINTF_INIT (format, size, args);
    size_t old_len = str_len(ctx->error_msg);
    str_maybe_grow (ctx->error_msg, str_len(ctx->error_msg) + size - 1, true);
    PRINTF_SET (str_data(ctx->error_msg) + old_len, size, format, args);

    str_cat_c (ctx->error_msg, "\n");
}

bool ps_parse_tag_parameters (struct psx_parser_state_t *ps, struct psx_tag_parameters_t *parameters)
{
    bool has_parameters = false;

    if (parameters != NULL) {
        parameters->named.pool = &ps->pool;
    }

    char *original_pos = ps->pos;
    if (pps_curr_char(ps) == '[') {
        has_parameters = true;

        while (!ps->is_eof && pps_curr_char(ps) != ']') {
            pps_advance_char (ps);

            char *start = ps->pos;
            if (pps_curr_char(ps) != '\"') {
                // TODO: Allow quote strings for values. This is to allow
                // values containing "," or "]".
            }

            while (!ps->is_eof &&
                   pps_curr_char(ps) != ',' &&
                   pps_curr_char(ps) != '=' &&
                   pps_curr_char(ps) != ']')
            {
                pps_advance_char (ps);
            }

            if (ps->is_eof) {
                has_parameters = false;
            }

            if (pps_curr_char(ps) == ',' || pps_curr_char(ps) == ']') {
                if (parameters != NULL) {
                    LINKED_LIST_APPEND_NEW (&ps->pool, struct sstring_ll_t, parameters->positional, new_param);
                    new_param->v = sstr_strip(SSTRING(start, ps->pos - start));
                }

            } else if (pps_curr_char(ps) == '=') {
                sstring_t name = sstr_strip(SSTRING(start, ps->pos - start));

                // Advance over the '=' character
                pps_advance_char (ps);

                char *value_start = ps->pos;
                while (!ps->is_eof &&
                       pps_curr_char(ps) != ',' &&
                       pps_curr_char(ps) != ']')
                {
                    pps_advance_char (ps);
                }

                if (ps->is_eof) {
                    has_parameters = false;
                }

                sstring_t value = sstr_strip(SSTRING(value_start, ps->pos - value_start));

                if (parameters != NULL) {
                    sstring_map_insert (&parameters->named, name, value);
                }
            }
        }

        if (pps_curr_char(ps) == ']') {
            pps_advance_char (ps);
        } else {
            has_parameters = false;
        }
    }

    if (!has_parameters) {
        ps->pos = original_pos;
        ps->is_eof = false;
    }

    return has_parameters;
}

struct psx_token_t ps_next_peek(struct psx_parser_state_t *ps)
{
    struct psx_token_t _tok = {0};
    struct psx_token_t *tok = &_tok;

    char *backup_pos = ps->pos;
    int backup_column_number = ps->column_number;

    pps_consume_spaces (ps);
    tok->margin = MAX(ps->column_number-1, 0);
    tok->type = TOKEN_TYPE_PARAGRAPH;

    char *non_space_pos = ps->pos;
    if (pps_is_eof(ps)) {
        ps->is_eof = true;
        ps->is_eol = true;
        tok->type = TOKEN_TYPE_END_OF_FILE;

    } else if (pps_match_str(ps, "- ") || pps_match_str(ps, "* ")) {
        pps_consume_spaces (ps);

        tok->type = TOKEN_TYPE_BULLET_LIST;
        tok->value = SSTRING(non_space_pos, 1);
        tok->margin = non_space_pos - backup_pos;
        tok->content_start = ps->pos - non_space_pos;

    } else if (pps_match_digits(ps)) {
        char *number_end = ps->pos;
        if (ps->pos - non_space_pos <= 9 && pps_match_str(ps, ". ")) {
            pps_consume_spaces (ps);

            tok->type = TOKEN_TYPE_NUMBERED_LIST;
            tok->value = SSTRING(non_space_pos, number_end - non_space_pos);
            tok->margin = non_space_pos - backup_pos;
            tok->content_start = ps->pos - non_space_pos;

        } else {
            // It wasn't a numbered list marker, restore position.
            ps->pos = backup_pos;
            ps->column_number = backup_column_number;
        }

    } else if (pps_match_str(ps, "#")) {
        int heading_number = 1;
        while (pps_curr_char(ps) == '#') {
            heading_number++;
            pps_advance_char (ps);
        }

        if (heading_number <= 6) {
            pps_consume_spaces (ps);

            tok->value = sstr_strip(pps_advance_line (ps));
            tok->is_eol = true;
            tok->margin = non_space_pos - backup_pos;
            tok->type = TOKEN_TYPE_TITLE;
            tok->heading_number = heading_number;
        }

    } else if (pps_match_str(ps, "\\code")) {
        ps_parse_tag_parameters(ps, NULL);

        if (pps_match_str(ps, "\n")) {
            tok->type = TOKEN_TYPE_CODE_HEADER;
            while (pps_is_space(ps) || pps_curr_char(ps) == '\n') {
                pps_advance_char (ps);
            }

        } else {
            // False alarm, this was an inline code block, restore position.
            // :restore_pos
            ps->pos = backup_pos;
            ps->column_number = backup_column_number;
        }

    } else if (pps_match_str(ps, "|")) {
        tok->value = pps_advance_line (ps);
        tok->is_eol = true;
        tok->type = TOKEN_TYPE_CODE_LINE;

    } else if (pps_match_str(ps, "\n")) {
        tok->type = TOKEN_TYPE_BLANK_LINE;
    }

    if (tok->type == TOKEN_TYPE_PARAGRAPH) {
        tok->value = sstr_strip(pps_advance_line (ps));
        tok->is_eol = true;
    }


    // Because we are only peeking, restore the old position and store the
    // resulting one in a separate variable.
    assert (!ps->is_peek && "Trying to peek more than once in a row");
    ps->is_peek = true;
    ps->token_peek = *tok;
    ps->pos_peek = ps->pos;
    ps->pos = backup_pos;

    //printf ("%s: '%.*s'\n", psx_token_type_names[ps->token_peek.type], ps->token_peek.value.len, ps->token_peek.value.s);

    return ps->token_peek;
}

struct psx_token_t ps_next(struct psx_parser_state_t *ps) {
    if (!ps->is_peek) {
        ps_next_peek(ps);
    }

    ps->pos = ps->pos_peek;
    ps->token = ps->token_peek;
    ps->is_peek = false;

    //printf ("%s: '%.*s'\n", psx_token_type_names[ps->token.type], ps->token.value.len, ps->token.value.s);

    return ps->token;
}

bool ps_match(struct psx_parser_state_t *ps, enum psx_token_type_t type, char *value)
{
    bool match = false;

    if (type == ps->token.type) {
        if (value == NULL) {
            match = true;

        } else if (strncmp(ps->token.value.s, value, ps->token.value.len) == 0) {
            match = true;
        }
    }

    return match;
}

#define ps_inline_next(ps) ps_inline_next_full(ps,true)
struct psx_token_t ps_inline_next_full(struct psx_parser_state_t *ps, bool escape_operators)
{
    struct psx_token_t tok = {0};

    if (pps_is_eof(ps)) {
        ps->is_eof = true;

    } else if (pps_curr_char(ps) == '\\' || pps_curr_char(ps) == '^') {
        if (pps_curr_char(ps) == '\\') {
            tok.type = TOKEN_TYPE_TEXT_TAG;
        } else {
            tok.type = TOKEN_TYPE_DATA_TAG;
        }

        pps_advance_char (ps);

        if (!pps_is_operator(ps) || pps_curr_char(ps) == '\\' || pps_curr_char(ps) == '{') {
            char *start = ps->pos;

            // TODO: Would be nicer to make '^' be an operator, unfortunately
            // that breaks LaTeX parsing, why?.
            while (!pps_is_eof(ps) && !pps_is_operator(ps) && !pps_is_space(ps) &&
                   pps_curr_char(ps) != '\n' && pps_curr_char(ps) != '^')
            {
                pps_advance_char (ps);
            }

            tok.value = SSTRING(start, ps->pos - start);

        } else {
            if (escape_operators) {
                // Escaped operator
                tok.value = SSTRING(ps->pos, 1);
                tok.type = TOKEN_TYPE_TEXT;

                pps_advance_char (ps);

            } else {
                tok.value = SSTRING("\\", 1);
                tok.type = TOKEN_TYPE_TEXT;
            }
        }

    } else if (pps_is_operator(ps)) {
        tok.type = TOKEN_TYPE_OPERATOR;
        tok.value = SSTRING(ps->pos, 1);
        pps_advance_char (ps);

    } else if (pps_is_space(ps)) {
        // This collapses consecutive spaces into a single one.
        tok.coalesced_spaces = 0;
        while (pps_is_space(ps)) {
            tok.coalesced_spaces++;
            pps_advance_char (ps);
        }

        tok.value = SSTRING(" ", 1);
        tok.type = TOKEN_TYPE_SPACE;

    } else {
        char *start = ps->pos;
        while (!pps_is_eof(ps) && !pps_is_operator(ps) && !pps_is_space(ps)) {
            pps_advance_char (ps);
        }

        tok.value = SSTRING(start, ps->pos - start);
        tok.type = TOKEN_TYPE_TEXT;
    }

    ps->token = tok;

    //printf ("%s: %.*s\n", psx_token_type_names[tok.type], tok.value.len, tok.value.s);
    return tok;
}

struct psx_tag_t {
    struct psx_token_t token;

    bool has_parameters;
    struct psx_tag_parameters_t parameters;

    bool has_content;
    string_t content;
};

void ps_restore_pos (struct psx_parser_state_t *ps, char *original_pos)
{
    ps->pos = original_pos;
    ps->is_eof = false;
}

void str_cat_literal_token (string_t *str, struct psx_token_t token)
{
    if (token.type == TOKEN_TYPE_TEXT_TAG) {
        str_cat_c (str, "\\");
        strn_cat_c (str, token.value.s, token.value.len);

    } else if (token.type == TOKEN_TYPE_NUMBERED_LIST) {
        strn_cat_c(str, token.value.s, token.value.len);
        str_cat_c (str, ".");

    } else if (token.type == TOKEN_TYPE_SPACE) {
        str_cat_char(str, ' ', token.coalesced_spaces);

    } else {
        strn_cat_c (str, token.value.s, token.value.len);
    }
}

// This function returns the same string that was parsed as the current token.
// It's used as fallback, for example in the case of ',' and '#' characters in
// the middle of paragraphs, or unrecognized tag sequences.
//
// TODO: I feel that using this for operators isn't nice, we should probably
// have a stateful tokenizer that will consider ',' as part of a TEXT token
// sometimes and as operators other times. It's probably best to just have an
// attribute tokenizer/parser, then the inline parser only deals with tags.
void str_cat_literal_token_ps (string_t *str, struct psx_parser_state_t *ps)
{
    str_cat_literal_token (str, ps->token);
}

bool psx_extract_until_unescaped_operator (struct psx_parser_state_t *ps, char c, string_t *tgt)
{
    assert (char_is_operator(c));

    bool success = true;

    char *last_start = ps->pos;
    while (!ps->is_eof) {
        pps_advance_char (ps);

        if (pps_curr_char(ps) == c) {
            if (pps_prev_char(ps) == '\\') {
                strn_cat_c (tgt, last_start, ps->pos - last_start - 1);
                str_cat_char (tgt, c, 1);

                pps_advance_char (ps);
                last_start = ps->pos;

                if (pps_curr_char(ps) == c) break;
            } else {
                break;
            }

        }
    }

    if (pps_curr_char(ps) == c) {
        strn_cat_c (tgt, last_start, ps->pos - last_start);
        last_start = ps->pos;

        // Advance over the _c_ character
        pps_advance_char (ps);

    } else {
        success = false;
    }

    return success;
}

bool psx_cat_tag_content (struct psx_parser_state_t *ps, string_t *str, bool expect_balanced_braces)
{
    bool has_content = false;

    char *original_pos = ps->pos;
    ps_inline_next(ps);
    if (ps_match(ps, TOKEN_TYPE_OPERATOR, "{")) {
        has_content = true;

        if (!expect_balanced_braces) {
            has_content = psx_extract_until_unescaped_operator (ps, '}', str);

        } else {
            int brace_level = 1;

            while (!ps->is_eof && brace_level != 0) {
                ps_inline_next_full (ps, false);
                if (ps_match (ps, TOKEN_TYPE_OPERATOR, "{")) {
                    brace_level++;
                } else if (ps_match (ps, TOKEN_TYPE_OPERATOR, "}")) {
                    brace_level--;
                }

                if (brace_level != 0) { // Avoid appending the closing }
                    str_cat_literal_token_ps (str, ps);
                }
            }
        }

    } else if (ps_match(ps, TOKEN_TYPE_OPERATOR, "|")) {
        has_content = true;

        char *start = ps->pos;
        while (!ps->is_eof && *(ps->pos) != '|') {
            pps_advance_char (ps);
        }

        if (ps->is_eof) {
            psx_error (ps, "Unexpected end of block. Incomplete definition of block content delimiter.");

        } else {
            char *number_end;
            size_t size = strtoul (start, &number_end, 10);
            if (ps->pos == number_end) {
                pps_advance_char (ps);
                strn_cat_c (str, ps->pos, size);
                ps->pos += size;

            } else {
                // TODO: Implement sentinel based tag content parsing
                // \code|SENTINEL|int a[2] = {1,2}SENTINEL
            }
        }
    }

    if (!has_content) {
        ps_restore_pos (ps, original_pos);
    }

    return has_content;
}

struct psx_tag_t* ps_tag_new (struct psx_parser_state_t *ps)
{
    struct psx_tag_t *tag = mem_pool_push_struct (&ps->pool, struct psx_tag_t);
    *tag = ZERO_INIT (struct psx_tag_t);
    tag->parameters.named.pool = &ps->pool;
    str_pool (&ps->pool, &tag->content);

    return tag;
}

#define ps_parse_tag(ps,end) ps_parse_tag_full(ps,end,false)
struct psx_tag_t* ps_parse_tag_full (struct psx_parser_state_t *ps, char **end, bool expect_balanced_braces)
{
    if (end != NULL) *end = ps->pos;

    mem_pool_marker_t mrk = mem_pool_begin_temporary_memory (&ps->pool);

    struct psx_tag_t *tag = ps_tag_new (ps);
    tag->token = ps->token;

    char *original_pos = ps->pos;
    if (pps_curr_char(ps) == '[' || pps_curr_char(ps) == '{' || pps_curr_char(ps) == '|') {
        tag->has_parameters = ps_parse_tag_parameters(ps, &tag->parameters);

        if (tag->has_parameters && end != NULL) *end = ps->pos;

        tag->has_content = psx_cat_tag_content(ps, &tag->content, expect_balanced_braces);

        if (!tag->has_parameters && !tag->has_content) {
            ps->pos = original_pos;
            ps->is_eof = false;
        }
    }

    if (ps->error) {
        mem_pool_end_temporary_memory (mrk);
        tag = NULL;
    }

    return tag;
}

enum psx_block_unit_type_t {
    BLOCK_UNIT_TYPE_ROOT,
    BLOCK_UNIT_TYPE_HTML
};

struct psx_block_unit_t {
    enum psx_block_unit_type_t type;
    struct html_element_t *html_element;

    enum psx_token_type_t list_type;
};

struct psx_block_unit_t* psx_block_unit_new(mem_pool_t *pool, enum psx_block_unit_type_t type, struct html_element_t *html_element)
{
    struct psx_block_unit_t *new_block_unit = mem_pool_push_struct (pool, struct psx_block_unit_t);
    *new_block_unit = ZERO_INIT (struct psx_block_unit_t);
    new_block_unit->html_element = html_element;
    new_block_unit->type = type;

    return new_block_unit;
}

void psx_append_html_element (struct psx_parser_state_t *ps, struct html_t *html, struct html_element_t *html_element)
{
    struct psx_block_unit_t *head_ctx = DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack);
    html_element_append_child (html, head_ctx->html_element, html_element);
}

void psx_append_inline_code_element (struct psx_parser_state_t *ps, struct html_t *html, string_t *str)
{
    str_replace (str, "\n", " ", NULL);

    struct html_element_t *code_element = html_new_element (html, "code");
    html_element_class_add(html, code_element, "code-inline");
    if (str_len(str) > 0) {
        html_element_append_strn (html, code_element, str_len(str), str_data(str));
    }

    psx_append_html_element(ps, html, code_element);
}

struct psx_block_unit_t* psx_push_block_unit_new (struct psx_parser_state_t *ps, struct html_t *html, sstring_t tag)
{
    struct html_element_t *html_element = html_new_element_strn (html, tag.len, tag.s);
    psx_append_html_element (ps, html, html_element);

    struct psx_block_unit_t *new_block_unit = psx_block_unit_new (&ps->pool, BLOCK_UNIT_TYPE_HTML, html_element);
    DYNAMIC_ARRAY_APPEND(ps->block_unit_stack, new_block_unit);
    return new_block_unit;
}

struct psx_block_unit_t* psx_block_unit_pop (struct psx_parser_state_t *ps)
{
    return DYNAMIC_ARRAY_POP_LAST(ps->block_unit_stack);
}

void ps_html_cat_literal_tag (struct psx_parser_state_t *ps, struct psx_tag_t *tag, struct html_t *html)
{
    string_t literal_tag = {0};
    struct psx_block_unit_t *curr_unit = DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack);
    str_cat_literal_token (&literal_tag, tag->token);

    if (tag->has_parameters) {
        bool is_first = true;
        str_cat_c (&literal_tag, "[");

        LINKED_LIST_FOR (struct sstring_ll_t *, pos_param, tag->parameters.positional) {
            if (!is_first) {
                str_cat_c (&literal_tag, ", ");
            }
            is_first = false;

            strn_cat_c (&literal_tag, pos_param->v.s, pos_param->v.len);
        }

        BINARY_TREE_FOR(sstring_map, &tag->parameters.named, named_param) {
            if (!is_first) {
                str_cat_c (&literal_tag, ", ");
            }
            is_first = false;

            strn_cat_c (&literal_tag, named_param->key.s, named_param->key.len);
            str_cat_c (&literal_tag, "=");
            strn_cat_c (&literal_tag, named_param->value.s, named_param->value.len);
        }

        str_cat_c (&literal_tag, "]");
    }

    html_element_append_strn (html, curr_unit->html_element, str_len(&literal_tag), str_data (&literal_tag));
    str_free (&literal_tag);
}

void compute_media_size (struct psx_tag_parameters_t *parameters, double aspect_ratio, double max_width, double *w, double *h)
{
    assert (w != NULL && h != NULL);
    sstring_t value;

    double a_width = NAN;
    if (sstring_map_maybe_get (&parameters->named, SSTRING_C("width"), &value)) {
        a_width = strtod(value.s, NULL);
    }

    double a_height = NAN;
    if (sstring_map_maybe_get (&parameters->named, SSTRING_C("height"), &value)) {
        a_height = strtod(value.s, NULL);
    }


    double width, height;
    if (a_height == NAN &&
        a_width != NAN && a_width <= max_width) {
        width = a_width;
        height = width/aspect_ratio;

    } else if (a_width == NAN &&
               a_height != NAN && a_height <= max_width/aspect_ratio) {
        height = a_height;
        width = height*aspect_ratio;

    } else if (a_width != NAN && a_width <= max_width &&
               a_height != NAN && a_height <= max_width/aspect_ratio) {
        width = a_width;
        height = a_height;

    } else {
        width = max_width;
        height = width/aspect_ratio;
    }

    *w = width;
    *h = height;
}

struct psx_tag_t* psx_parse_tag_note (struct psx_parser_state_t *ps, char **original_pos, struct note_t **target_note, string_t *target_note_title)
{
    assert (target_note != NULL);
    struct psx_tag_t *tag = ps_parse_tag (ps, original_pos);

    str_replace (&tag->content, "\n", " ", NULL);
    // TODO: Trim tag->content of start/end spaces...

    // TODO: Implement navigation to note's subsections. Right now we
    // just ignore what comes after the #. Also, this notation is
    // problematic if there are titles that contain # characters, do we
    // care about that enough to make the subsection be passed as a
    // parameter instead of the current shorthand syntax?. Do we want to
    // allow both?. We can check that a note link is valid because we
    // did a pre processing of all titles and have a map from titles to
    // notes, we can't do the same for sections because notes have not
    // been fully parsed yet. If we want to show warnings in these cases
    // we will need a post processing phase that validates all note
    // links that contain a subsection.

    // TODO: This should essentially parse the format
    //
    //                 {note-title}#{note-section}
    //
    // where the # and note-section are optional and both "capture
    // groups" trim spaces at the start and end. I though parsing this
    // with a regular expression was going to be straightforward but the
    // smallest regex I found was
    //
    //                 ^\s*(.+?)(\s*#\s*(.*?)\s*)?$
    //
    // then assign capture groups like this
    //
    //  note-title <- (1)
    //  note-section <- (3)
    //
    // I'd like to design a notation that allows defining these kinds of
    // tags easily. So far we also do a very similar thing for links
    // where we parse the format
    //
    //                      {link-title}->{url}
    //
    // here the difference being, that optional components are
    // link-title and the -> characters.
    //
    // Designing a syntax for this would have the benefit of letting us
    // easily recreate the original tag from the components. Also it
    // would let us automatically define named parameters to set the
    // components in case the content would need some escaping, for
    // example
    //
    //  \note[note-title="# hash character", note-section="#subsesction"]{}
    //
    // We can have the syntax fall back to a regex expression where the
    // user provides a mapping between capture groups and component
    // names. But now we would require of them to also add a format to
    // show how to build the original tag back from the components.
    // Although, we can always fall back to the tag parameters
    // notation...?, hmmm this sound promising.
    //
    // TeX has a very similar format, but it was created before regular
    // expressions were a thing and I think it hardcodes some policies
    // about greediness. The fall back to regular expressions would
    // allow the full expressibility for defining parsing while reusing
    // a language somewhat familiar to programmers and not having to
    // make the big jump to using an actual programming language.
    char *start = str_data (&tag->content);
    while (is_space (start)) start++;

    char *pos = start;
    while (*pos != '\0' && *pos != '#') pos++;
    while (pos > start && is_space (pos-1)) pos--;

    if (pos != start) {
        strn_set (target_note_title, start, pos-start);
    } else {
        str_set (target_note_title, start);
    }

    *target_note = rt_get_note_by_title (target_note_title);

    return tag;
}

// This function parses the content of a block of text. The formatting is
// limited to tags that affect the formating inline. This parsing function
// will not add nested blocks like paragraphs, lists, code blocks etc.
//
// TODO: How do we handle the prescence of nested blocks here?, ignore them and
// print them or raise an error and stop parsing.
void block_content_parse_text (struct psx_parser_ctx_t *ctx, struct html_t *html, struct html_element_t *container, char *content)
{
    string_t buff = {0};
    STACK_ALLOCATE (struct psx_parser_state_t, ps);
    ps->ctx = *ctx;
    ps_init (ps, content);
    char *original_pos = NULL;

    // TODO: I'm starting to think inline parser shouldn't use a stack based
    // state for handling nested tags. I'm starting to think that a recursive
    // parser would  better?, but it's just a feeling, It's possible the code
    // will still look the same.
    DYNAMIC_ARRAY_APPEND (ps->block_unit_stack, psx_block_unit_new (&ps->pool, BLOCK_UNIT_TYPE_ROOT, container));
    while (!ps->is_eof && !ps->error) {
        struct psx_token_t tok = ps_inline_next (ps);

        str_set (&buff, "");
        if (ps_match(ps, TOKEN_TYPE_TEXT, NULL) || ps_match(ps, TOKEN_TYPE_SPACE, NULL)) {
            struct psx_block_unit_t *curr_unit = DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack);

            strn_set (&buff, tok.value.s, tok.value.len);
            str_replace (&buff, "\n", " ", NULL);
            html_element_append_strn (html, curr_unit->html_element, str_len(&buff), str_data(&buff));

        } else if (ps_match(ps, TOKEN_TYPE_OPERATOR, "}") && DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack)->type != BLOCK_UNIT_TYPE_ROOT) {
            psx_block_unit_pop (ps);

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "i") || ps_match(ps, TOKEN_TYPE_TEXT_TAG, "b")) {
            struct psx_token_t tag = ps->token;

            char *original_pos = ps->pos;
            ps_inline_next (ps);
            if (ps_match(ps, TOKEN_TYPE_OPERATOR, "{")) {
                // If there was an opening { but the rest of the tag's content
                // was missing we still push a styling unit. This is different
                // to all other tags that require content, for these we bail out
                // and print the literal tag.
                //
                // TODO: Is this the correct behavior?, we did this stack based
                // inline parser just to support nested tags like these, and to
                // defer the handling of broken tag definitions to each tag. But
                // now, we have a pretty well defined behavior for handling tags
                // with broken content component, we could just use
                // ps_parse_tag() here, and bail out in the same way we do for
                // other tags if the content block is broken.
                psx_push_block_unit_new (ps, html, tag.value);

            } else {
                // Don't fail, instead print literal tag.
                struct psx_block_unit_t *curr_unit = DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack);
                str_cat_literal_token (&buff, tag);
                html_element_append_strn (html, curr_unit->html_element, str_len(&buff), str_data (&buff));

                ps_restore_pos (ps, original_pos);
            }

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "link")) {
            struct psx_tag_t *tag = ps_parse_tag (ps, &original_pos);
            if (tag->has_content) {
                // We parse the URL from the title starting at the end. I thinkg is
                // far less likely to have a non URL encoded > character in the
                // URL, than a user wanting to use > inside their title.
                //
                // TODO: Support another syntax for the rare case of a user that
                // wants a > character in a URL. Or make sure URLs are URL encoded
                // from the UI that will be used to edit this.
                ssize_t pos = str_len(&tag->content) - 1;
                while (pos > 0 && str_data(&tag->content)[pos] != '>') {
                    pos--;
                }

                sstring_t url = SSTRING(str_data(&tag->content), str_len(&tag->content));
                sstring_t title = SSTRING(str_data(&tag->content), str_len(&tag->content));
                if (pos > 0 && str_data(&tag->content)[pos - 1] == '-') {
                    pos--;
                    url = sstr_strip(SSTRING(str_data(&tag->content) + pos + 2, str_len(&tag->content) - (pos + 2)));
                    title = sstr_strip(SSTRING(str_data(&tag->content), pos));

                    // TODO: It would be nice to have this API...
                    //sstr_strip(sstr_substr(pos+2));
                    //sstr_strip(sstr_substr(0, pos));
                }

                struct html_element_t *link_element = html_new_element (html, "a");
                strn_set (&buff, url.s, url.len);
                str_replace (&buff, "\n", "", NULL);
                html_element_attribute_set (html, link_element, "href", str_data(&buff));
                html_element_attribute_set (html, link_element, "target", "_blank");

                strn_set (&buff, title.s, title.len);
                str_replace (&buff, "\n", " ", NULL);
                html_element_append_strn (html, link_element, str_len(&buff), str_data(&buff));

                psx_append_html_element(ps, html, link_element);

            } else {
                ps_restore_pos (ps, original_pos);
                ps_html_cat_literal_tag (ps, tag, html);
            }

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "youtube")) {
            // TODO: I'm thinking this would look cleaner as a custom tag
            bool success = true;

            struct psx_tag_t *tag = ps_parse_tag (ps, &original_pos);
            if (tag->has_content) {
                str_replace (&tag->content, "\n", " ", NULL);

                const char *error;
                Resub m;
                Reprog *regex = regcomp("^.*(youtu.be\\/|youtube(-nocookie)?.com\\/(v\\/|.*u\\/\\w\\/|embed\\/|.*v=))([\\w-]{11}).*", 0, &error);

                if (!regexec(regex, str_data(&tag->content), &m, 0)) {
                    sstring_t video_id = SSTRING((char*) m.sub[4].sp, m.sub[4].ep - m.sub[4].sp);

                    // Assume 16:9 aspect ratio
                    double width, height;
                    compute_media_size(&tag->parameters, 16.0L/9, psx_content_width - 30, &width, &height);

                    struct html_element_t *html_element = html_new_element (html, "iframe");
                    str_set_printf (&buff, "%.6g", width);
                    html_element_attribute_set (html, html_element, "width", str_data(&buff));
                    str_set_printf (&buff, "%.6g", height);
                    html_element_attribute_set (html, html_element, "height", str_data(&buff));
                    html_element_attribute_set (html, html_element, "style", "margin: 0 auto; display: block;");
                    str_set_printf (&buff, "https://www.youtube-nocookie.com/embed/%.*s", video_id.len, video_id.s);
                    html_element_attribute_set (html, html_element, "src", str_data(&buff));
                    html_element_attribute_set (html, html_element, "frameborder", "0");
                    html_element_attribute_set (html, html_element, "allow", "accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture");
                    html_element_attribute_set (html, html_element, "allowfullscreen", "");
                    psx_append_html_element(ps, html, html_element);
                    regfree (regex);

                } else { success = false; }
            } else { success = false; }

            if (!success) {
                ps_restore_pos (ps, original_pos);
                ps_html_cat_literal_tag (ps, tag, html);
            }

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "image")) {
            struct psx_tag_t *tag = ps_parse_tag (ps, &original_pos);
            if (tag->has_content) {
                str_replace (&tag->content, "\n", " ", NULL);

                struct html_element_t *img_element = html_new_element (html, "img");
                if (is_canonical_id(str_data(&tag->content))) {
                    uint64_t id = canonical_id_parse(str_data(&tag->content), 0);
                    struct vlt_file_t *file = file_id_lookup (ctx->vlt, id);

                    // TODO: Put image extensions into an array, don't duplicate
                    // upper and lower case cases.
                    while (strncmp(str_data(&file->extension), "png", 3) != 0 &&
                           strncmp(str_data(&file->extension), "PNG", 3) != 0 &&
                           strncmp(str_data(&file->extension), "jpg", 3) != 0 &&
                           strncmp(str_data(&file->extension), "JPG", 3) != 0 &&
                           strncmp(str_data(&file->extension), "jpeg", 4) != 0 &&
                           strncmp(str_data(&file->extension), "JPEG", 4) != 0 &&
                           strncmp(str_data(&file->extension), "svg", 3) != 0 &&
                           strncmp(str_data(&file->extension), "SVG", 3) != 0) {
                        file = file->next;
                    }

                    str_set_printf (&buff, "files/%s", str_data(&file->path));

                } else {
                    str_set_printf (&buff, "files/%s", str_data(&tag->content));
                }
                html_element_attribute_set (html, img_element, "src", str_data(&buff));

                str_set_printf (&buff, "%d", psx_content_width);
                html_element_attribute_set (html, img_element, "width", str_data(&buff));
                psx_append_html_element(ps, html, img_element);

            } else {
                ps_restore_pos (ps, original_pos);
                ps_html_cat_literal_tag (ps, tag, html);
            }

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "code") || ps_match(ps, TOKEN_TYPE_OPERATOR, "`")) {
            if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "code")) {
                // TODO: Actually do something with the passed language name
                struct psx_tag_t *tag = ps_parse_tag_full (ps, &original_pos, true);
                if (tag->has_content) {
                    psx_append_inline_code_element (ps, html, &tag->content);

                } else {
                    ps_restore_pos (ps, original_pos);
                    ps_html_cat_literal_tag (ps, tag, html);
                }

            } else {
                string_t content = {0};
                psx_extract_until_unescaped_operator (ps, '`', &content);
                str_strip (&content);
                psx_append_inline_code_element (ps, html, &content);
            }

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "note")) {
            struct note_t *target_note = NULL;
            struct psx_tag_t *tag = psx_parse_tag_note (ps, &original_pos, &target_note, &buff);
            if (tag->has_content) {
                struct html_element_t *link_element = html_new_element (html, "a");
                html_element_attribute_set (html, link_element, "href", "#");
                if (target_note != NULL) {
                    str_set_printf (&buff, "return open_note('%s');", target_note->id);
                    html_element_attribute_set (html, link_element, "onclick", str_data(&buff));
                    html_element_class_add (html, link_element, "note-link");
                } else {
                    html_element_class_add (html, link_element, "note-link-broken");

                    // :target_note_not_found_error
                    psx_warning (ps, "broken note link, couldn't find note for title: %s", str_data (&buff));
                }
                html_element_append_strn (html, link_element, str_len(&tag->content), str_data(&tag->content));
                psx_append_html_element(ps, html, link_element);

            } else {
                ps_restore_pos (ps, original_pos);
                ps_html_cat_literal_tag (ps, tag, html);
            }

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "html")) {
            struct psx_tag_t *tag = ps_parse_tag (ps, &original_pos);
            if (tag->has_content) {
                struct psx_block_unit_t *head_unit = ps->block_unit_stack[ps->block_unit_stack_len-1];
                html_element_append_no_escape_strn (html, head_unit->html_element, str_len(&tag->content), str_data(&tag->content));
            } else {
                ps_restore_pos (ps, original_pos);
                ps_html_cat_literal_tag (ps, tag, html);
            }

        } else if (ps->is_eof) {
            // do nothing

        } else {
            struct psx_block_unit_t *curr_unit = DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack);
            str_cat_literal_token_ps (&buff, ps);
            html_element_append_strn (html, curr_unit->html_element, str_len(&buff), str_data (&buff));
        }
    }

    ps_destroy (ps);
    str_free (&buff);
}

#define psx_block_new(ba) psx_block_new_cpy(ba,NULL)
struct psx_block_t* psx_block_new_cpy(struct block_allocation_t *ba, struct psx_block_t *original)
{
    struct psx_block_t *new_block = mem_pool_push_struct (ba->pool, struct psx_block_t);
    *new_block = ZERO_INIT (struct psx_block_t);
    str_pool (ba->pool, &new_block->inline_content);

    if (original != NULL) {
        new_block->type = original->type;
        new_block->margin = original->margin;
        str_cpy (&new_block->inline_content, &original->inline_content);
    }

    return new_block;
}


struct psx_block_t* psx_leaf_block_new(struct psx_parser_state_t *ps, enum psx_block_type_t type, int margin, sstring_t inline_content)
{
    struct psx_block_t *new_block = psx_block_new (&ps->block_allocation);
    strn_set (&new_block->inline_content, inline_content.s, inline_content.len);
    new_block->type = type;
    new_block->margin = margin;

    return new_block;
}

struct psx_block_t* psx_container_block_new(struct psx_parser_state_t *ps, enum psx_block_type_t type, int margin)
{
    struct psx_block_t *new_block = psx_block_new (&ps->block_allocation);
    new_block->type = type;
    new_block->margin = margin;

    return new_block;
}

struct psx_block_t* psx_push_block (struct psx_parser_state_t *ps, struct psx_block_t *new_block)
{
    struct psx_block_t *curr_block = DYNAMIC_ARRAY_GET_LAST(ps->block_stack);

    //console.assert (curr_block.block_content != NULL, "Pushing nested block to non-container block.");
    LINKED_LIST_APPEND (curr_block->block_content, new_block);

    DYNAMIC_ARRAY_APPEND (ps->block_stack, new_block);
    return new_block;
}

struct str_replacement_t {
    size_t start_idx;
    size_t len;
    string_t s;

    struct str_replacement_t *next;
};

enum psx_user_tag_cb_status_t {
    USER_TAG_CB_STATUS_CONTINUE,
    USER_TAG_CB_STATUS_BREAK,
};

// CAUTION: DON'T MODIFY THE BLOCK CONTENT'S STRING FROM A psx_user_tag_cb_t.
// These callbacks are called while parsing the block's content, modifying
// block->inline_content will mess up the parser.
#define PSX_USER_TAG_CB(funcname) enum psx_user_tag_cb_status_t funcname(struct psx_parser_ctx_t *ctx, struct block_allocation_t *block_allocation, struct psx_parser_state_t *ps_inline, struct psx_block_t **block, struct str_replacement_t *replacement)
typedef PSX_USER_TAG_CB(psx_user_tag_cb_t);

BINARY_TREE_NEW (psx_user_tag_cb, char*, psx_user_tag_cb_t*, strcmp(a, b));

void psx_populate_internal_cb_tree (struct psx_user_tag_cb_t *tree);

// TODO: User callbacks will be modifying the tree. It's possible the user
// messes up and for example adds a cycle into the tree, we should detect such
// problem and avoid maybe later entering an infinite loop.
#define psx_block_tree_user_callbacks(ctx,ba,root) psx_block_tree_user_callbacks_full(ctx,ba,root,NULL)
void psx_block_tree_user_callbacks_full (struct psx_parser_ctx_t *ctx, struct block_allocation_t *ba, struct psx_block_t **root, struct psx_block_t **block_p)
{
    if (block_p == NULL) block_p = root;

    struct psx_block_t *block = *block_p;

    if (block->block_content != NULL) {
        struct psx_block_t **sub_block = &((*block_p)->block_content);
        while (*sub_block != NULL) {
            psx_block_tree_user_callbacks_full (ctx, ba, root, sub_block);
            sub_block = &(*sub_block)->next;
        }

    } else {
        struct psx_parser_state_t _ps_inline = {0};
        struct psx_parser_state_t *ps_inline = &_ps_inline;
        ps_inline->ctx = *ctx;
        ps_init (ps_inline, str_data(&block->inline_content));

        struct psx_user_tag_cb_t user_cb_tree = {0};
        user_cb_tree.pool = &ps_inline->pool;
        psx_populate_internal_cb_tree (&user_cb_tree);

        struct str_replacement_t *replacements = NULL;
        struct str_replacement_t *replacements_end = NULL;
        char *block_start = ps_inline->pos;
        while (!ps_inline->is_eof && !ps_inline->error) {
            char *tag_start = ps_inline->pos;
            struct psx_token_t tok = ps_inline_next (ps_inline);
            if (ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, NULL)) {
                string_t tag_name = strn_new (tok.value.s, tok.value.len);

                psx_user_tag_cb_t* cb = psx_user_tag_cb_get (&user_cb_tree, str_data(&tag_name));
                if (cb != NULL) {
                    string_t cb_error_msg = {0};
                    string_t *error_msg_bak = ps_inline->ctx.error_msg;
                    ps_inline->ctx.error_msg = &cb_error_msg;

                    struct str_replacement_t tmp_replacement = {0};
                    enum psx_user_tag_cb_status_t status = cb (ctx, ba, ps_inline, block_p, &tmp_replacement);
                    tmp_replacement.start_idx = tag_start - block_start;
                    tmp_replacement.len = ps_inline->pos - tag_start;

                    if (ps_inline->error) {
                        psx_warning_ctx (ctx, "failed parsing of custom tag '%s'", str_data(&tag_name));
                        str_cat_indented_c (ctx->error_msg, str_data(&cb_error_msg), 2);
                    }

                    if (str_len (&tmp_replacement.s) > 0) {
                        struct str_replacement_t *replacement = mem_pool_push_struct (&ps_inline->pool, struct str_replacement_t);
                        *replacement = tmp_replacement;
                        str_pool (&ps_inline->pool, &replacement->s);
                        LINKED_LIST_APPEND (replacements, replacement);
                    }

                    ps_inline->ctx.error_msg = error_msg_bak;
                    str_free (&cb_error_msg);

                    if (status == USER_TAG_CB_STATUS_BREAK) {
                        break;
                    }
                }

                str_free (&tag_name);
            }
        }

        if (replacements != NULL) {
            char *original = strndup(str_data(&block->inline_content), str_len(&block->inline_content));
            char *p = original;
            str_set (&block->inline_content, "");
            LINKED_LIST_FOR (struct str_replacement_t*, curr_replacement, replacements) {
                size_t original_chunk_len = curr_replacement->start_idx - (p - original);
                strn_cat_c (&block->inline_content, p, original_chunk_len);
                str_cat (&block->inline_content, &curr_replacement->s);
                p += original_chunk_len + curr_replacement->len;
            }
            str_cat_c (&block->inline_content, p);
            free (original);
        }

        // TODO: What will happen if a parse error occurs here?. We should print
        // some details about which user function failed by checking if there
        // is an error in ps_inline.
        ps_destroy (ps_inline);
    }
}

#define psx_create_links(ctx,root) psx_create_links_full(ctx,root,NULL)
void psx_create_links_full (struct psx_parser_ctx_t *ctx, struct psx_block_t **root, struct psx_block_t **block_p)
{
    if (block_p == NULL) block_p = root;

    struct psx_block_t *block = *block_p;

    if (block->block_content != NULL) {
        struct psx_block_t **sub_block = &((*block_p)->block_content);
        while (*sub_block != NULL) {
            psx_create_links_full (ctx, root, sub_block);
            sub_block = &(*sub_block)->next;
        }

    } else {
        struct psx_parser_state_t _ps_inline = {0};
        struct psx_parser_state_t *ps_inline = &_ps_inline;
        ps_init (ps_inline, str_data(&block->inline_content));

        while (!ps_inline->is_eof && !ps_inline->error) {
            ps_inline_next (ps_inline);
            if (ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "note")) {
                string_t target_note_title = {0};

                struct note_t *target_note = NULL;
                psx_parse_tag_note (ps_inline, NULL, &target_note, &target_note_title);
                struct note_t *current_note = rt_get_note_by_id (ctx->id);

                // If the target note is not found, fail silently because we
                // will show the warning message later, when building the html.
                // :target_note_not_found_error
                if (target_note != NULL) {
                    rt_link_notes (current_note, target_note);
                }

                str_free (&target_note_title);
            }
        }

        ps_destroy (ps_inline);
    }
}

// I'm anticipating the actual HTML for attributes to be changing a lot, so I
// don't want to break all tests every time a change is made. Instead, for tests
// we compare TSPLX data in canonical form and in the HTML we just output a
// placeholder div that represents all attributes.
void note_attributes_html_append (struct html_t *html, struct psx_block_t *block, struct html_element_t *parent)
#ifdef ATTRIBUTE_PLACEHOLDER
{
    struct html_element_t *attributes = html_new_element (html, "div");
    html_element_attribute_set (html, attributes, "id", "__attributes");
    html_element_append_child (html, parent, attributes);
}
#else
{
    // Types
    struct splx_node_list_t *type_list = cstr_to_splx_node_list_map_get (&block->data->attributes, "a");

    if (type_list != NULL) {
        struct html_element_t *types = html_new_element (html, "div");
        html_element_attribute_set (html, types, "style", "margin-bottom: 10px; align-items: baseline; display: flex; color: var(--secondary-fg-color)");

        LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, type_list) {
            struct html_element_t *value = html_new_element (html, "div");
            html_element_class_add (html, value, "type");
            html_element_append_cstr (html, value, str_data(&curr_list_node->node->str));
            html_element_append_child (html, types, value);
        }

        html_element_append_child (html, parent, types);
    }


    // Attributes

    // NOTE: Avoid generating any attribute elements if the only present
    // attributer are those already shown soehwre else (note's title, type
    // labels etc.).
    if ((type_list != NULL || block->data->attributes.num_nodes != 1) &&
        (type_list == NULL || block->data->attributes.num_nodes != 2)) {

        struct html_element_t *container = html_new_element (html, "div");
        html_element_class_add (html, container, "attributes");

        BINARY_TREE_FOR (cstr_to_splx_node_list_map, &block->data->attributes, curr_attribute) {
            // Type attributes "a" are rendered as labels, the "name" attribute is
            // the note's title.
            if (strcmp(curr_attribute->key, "a") == 0 || strcmp(curr_attribute->key, "name") == 0) continue;

            struct html_element_t *attribute = html_new_element (html, "div");
            html_element_attribute_set (html, attribute, "style", "align-items: baseline; display: flex; color: var(--secondary-fg-color)");

            struct html_element_t *label = html_new_element (html, "div");
            html_element_class_add (html, label, "attribute-label");
            html_element_append_cstr (html, label, curr_attribute->key);
            html_element_append_child (html, attribute, label);

            // NOTE: This additionar wrapper div for values is added so that they
            // wrap nicely and are left aligned with all other values.
            struct html_element_t *values = html_new_element (html, "div");
            html_element_attribute_set (html, values, "style", "row-gap: 3px; display: flex; flex-wrap: wrap;");
            LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, curr_attribute->value) {
                struct html_element_t *value = html_new_element (html, "div");
                html_element_class_add (html, value, "attribute-value");
                html_element_append_cstr (html, value, str_data(&curr_list_node->node->str));
                html_element_append_child (html, values, value);
            }
            html_element_append_child (html, attribute, values);

            html_element_append_child (html, container, attribute);
        }

        html_element_append_child (html, parent, container);
    }
}
#endif

void block_attributes_html_append (struct html_t *html, struct psx_block_t *block, struct html_element_t *html_element)
#ifdef ATTRIBUTE_PLACEHOLDER
{
    if (block->data != NULL) {
        html_element_attribute_set (html, html_element, "data-block-attributes", "");
    }
}
#else
{
}
#endif

void block_tree_to_html (struct psx_parser_ctx_t *ctx, struct html_t *html, struct psx_block_t *block, struct html_element_t *parent)
{
    string_t buff = {0};

    if (block->type == BLOCK_TYPE_PARAGRAPH) {
        struct html_element_t *new_dom_element = html_new_element (html, "p");
        html_element_append_child (html, parent, new_dom_element);
        block_content_parse_text (ctx, html, new_dom_element, str_data(&block->inline_content));
        block_attributes_html_append (html, block, new_dom_element);

    } else if (block->type == BLOCK_TYPE_HEADING) {
        str_set_printf (&buff, "h%i", block->heading_number);
        struct html_element_t *new_dom_element = html_new_element (html, str_data(&buff));

        html_element_append_child (html, parent, new_dom_element);
        block_content_parse_text (ctx, html, new_dom_element, str_data(&block->inline_content));

    } else if (block->type == BLOCK_TYPE_CODE) {
        struct html_element_t *pre_element = html_new_element (html, "pre");
        html_element_append_child (html, parent, pre_element);

        struct html_element_t *code_element = html_new_element (html, "code");
        html_element_class_add(html, code_element, "code-block");
        // If we use line numbers, this should be the padding WRT the
        // column containing the numbers.
        // html_element_style_set(html, code_element, "padding-left", "0.25em");

        html_element_append_cstr (html, code_element, str_data(&block->inline_content));
        html_element_attribute_set (html, code_element, "style", "display: block;");
        html_element_append_child (html, pre_element, code_element);

        // Previously I was using a hack to show tight, centered code blocks if
        // they were smaller than psx_content_width and at the same time show
        // horizontal scrolling if they were wider. I'm now of the opinion that
        // although a centered code block looks nice in a read-only environment,
        // it will be horrible if we eve implement inplace editing because the
        // cursor of the user will be jumping so the block gets recentered (if
        // the block gets recentered at all, which wouldn't be the case if I
        // kept using this hack).
        //
        // For this reason we now just make all code blocks have the full width
        // of the note. We could thing of using the hack back when enabling a
        // read-only mode or something like that. So, for reference I'll keep
        // the description of how the hack worked.
        //
        // We need to do this because "display: table", disables scrolling, but
        // "display: block" sets width to be the maximum possible. Here we
        // first create the element with "display: table", compute its width,
        // then change it to "display: block" but if it was smaller than
        // psx_content_width, we explicitly set its width.
        //
        //code_element.style.display = "table";
        //let tight_width = code_element.scrollWidth;
        //code_element.style.display = "block";
        //if (tight_width < psx_content_width) {
        //    code_element.style.width = tight_width - code_block_padding*2 + "px";
        //}

    } else if (block->type == BLOCK_TYPE_ROOT) {
        bool is_first = true;
        LINKED_LIST_FOR (struct psx_block_t*, sub_block, block->block_content) {
            block_tree_to_html(ctx, html, sub_block, parent);

            // If there are note attributes in the root block, print them after
            // the first block whoch sould've been the title (heading).
            if (block->data != NULL && is_first) {
                is_first = false;
                note_attributes_html_append (html, block, parent);
            }
        }

    } else if (block->type == BLOCK_TYPE_LIST) {
        struct html_element_t *new_dom_element = html_new_element (html, "ul");
        if (block->list_type == TOKEN_TYPE_NUMBERED_LIST) {
            new_dom_element = html_new_element (html, "ol");
        }
        html_element_append_child (html, parent, new_dom_element);

        LINKED_LIST_FOR (struct psx_block_t*, sub_block, block->block_content) {
            block_tree_to_html(ctx, html, sub_block, new_dom_element);
        }

    } else if (block->type == BLOCK_TYPE_LIST_ITEM) {
        struct html_element_t *new_dom_element = html_new_element (html, "li");
        html_element_append_child (html, parent, new_dom_element);

        LINKED_LIST_FOR (struct psx_block_t*, sub_block, block->block_content) {
            block_tree_to_html(ctx, html, sub_block, new_dom_element);
        }
    }

    str_free (&buff);
}

void psx_parse_block_attributes (struct psx_parser_state_t *ps, struct psx_block_t *block, char *node_id)
{
    assert (block != NULL);

    char *backup_pos = ps->pos;
    int backup_column_number = ps->column_number;

    struct psx_token_t tok = ps_inline_next (ps);
    if (ps_match(ps, TOKEN_TYPE_DATA_TAG, NULL)) {
        assert(ps->ctx.sd != NULL);

        if (node_id != NULL) {
            block->data = splx_node_get_or_create(ps->ctx.sd, node_id, SPLX_NODE_TYPE_OBJECT);
        } else {
            block->data = splx_node_new(ps->ctx.sd);
        }

        if (tok.value.len > 0) {
            char *internal_backup_pos;
            int internal_backup_column_number;

            do {
                internal_backup_pos = ps->pos;
                internal_backup_column_number = ps->column_number;

                string_t s = {0};
                strn_set(&s, tok.value.s, tok.value.len);
                struct splx_node_t *type = splx_node_get_or_create (ps->ctx.sd, str_data(&s), SPLX_NODE_TYPE_OBJECT);
                splx_node_attribute_append (
                    ps->ctx.sd,
                    block->data,
                    "a", type);
                str_free (&s);

                tok = ps_inline_next (ps);
            } while (
                ps_match(ps, TOKEN_TYPE_DATA_TAG, NULL) &&

                // Double line breaks represent an empty line, this is what
                // breaks a tag sequence.
                !(pps_curr_char(ps) == '\n' && *(ps->pos+1) == '\n')
            );

            ps->pos = internal_backup_pos;
            ps->column_number = internal_backup_column_number;
        }

        string_t tsplx_data = {0};
        psx_cat_tag_content (ps, &tsplx_data, true);
        tsplx_parse_str_name_full(ps->ctx.sd, str_data(&tsplx_data), block->data, NULL);
        str_free (&tsplx_data);

        // At this point, we parsed some attributes, and we're setting the new
        // pos pointer in the state. If we had used peek before, the next call
        // to ps_next() will restore pos to the peeked value instead of the one
        // set by the tag content parseing. That's why we need to force the
        // state to mark the last token state as not peeked.
        // TODO: I should really get rid of the token peek AP. Instead use a
        // state marker together with a restore mechanism.
        // :marker_over_peek
        ps->is_peek = false;

    } else {
        // :restore_pos
        ps->pos = backup_pos;
        ps->column_number = backup_column_number;
    }
}

void psx_parse_note_title (struct psx_parser_state_t *ps, sstring_t *title, bool parse_attributes)
{
    struct psx_token_t tok = ps_next (ps);
    if (tok.type != TOKEN_TYPE_TITLE) {
        psx_error (ps, "note has no title");
    }

    if (tok.heading_number != 1) {
        psx_warning (ps, "Note's title doesn't use single #. Will behave as if it was.");
    }

    if (title != NULL) {
        *title = SSTRING(ps->token.value.s, ps->token.value.len);
    }

    if (parse_attributes) {
        // Note attributes are stored in the root block not in the heading
        // block.
        struct psx_block_t *root_block = ps->block_stack[0];
        psx_parse_block_attributes(ps, root_block, ps->ctx.id);

        if (ps->ctx.sd != NULL && root_block->data != NULL) {
            string_t s = {0};
            strn_set(&s, title->s, title->len);
            splx_node_attribute_add (
                ps->ctx.sd,
                root_block->data,
                "name", str_data(&s), SPLX_NODE_TYPE_STRING);
            str_free (&s);
        }
    }

    // TODO: Validate that the title has no inline tags.
}

bool parse_note_title (char *path, char *note_text, string_t *title, struct splx_data_t *sd, string_t *error_msg)
{
    bool success = true;

    struct psx_parser_state_t _ps = {0};
    struct psx_parser_state_t *ps = &_ps;
    ps->ctx.path = path;
    ps->ctx.sd = sd;
    ps_init (ps, note_text);

    sstring_t title_s = {0};
    psx_parse_note_title (ps, &title_s, false);
    if (!ps->error) {
        strn_set (title, title_s.s, title_s.len);
    } else {
        success = false;
    }

    ps_destroy (ps);

    return success;
}

void psx_append_paragraph_continuation_lines (struct psx_parser_state_t *ps, struct psx_block_t *new_paragraph)
{
    struct psx_token_t tok_peek = ps_next_peek(ps);
    while ((tok_peek.type == TOKEN_TYPE_PARAGRAPH ||
            (tok_peek.type == TOKEN_TYPE_NUMBERED_LIST && strncmp("1", tok_peek.value.s, tok_peek.value.len) != 0 && tok_peek.margin >= new_paragraph->margin)))
    {
        // Check if this paragraph token is actually a data header. If it is,
        // then stop appending continuation lines.
        // TODO: Should we make data header a new kind of block level token?.
        if (*tok_peek.value.s == '^' && sstr_find_char(&tok_peek.value, ' ') == NULL) {
            break;
        }

        str_cat_c(&new_paragraph->inline_content, "\n");
        str_cat_literal_token (&new_paragraph->inline_content, tok_peek);
        ps_next(ps);

        tok_peek = ps_next_peek(ps);
    }
}

void psx_parse (struct psx_parser_state_t *ps)
{
    while (!ps->is_eof && !ps->error) {
        struct psx_token_t tok = ps_next(ps);

        // TODO: I think the fact that I have to do this here, means we
        // shouldn't be parsing TOKEN_TYPE_BLANK_LINE as tokens anymore. The
        // scanner should just silently consume blank lines.
        if (tok.type == TOKEN_TYPE_BLANK_LINE) {
            continue;
        }

        int curr_block_idx = ps->block_stack_len - 1;
        struct psx_block_t *next_stack_block = ps->block_stack[curr_block_idx];
        while (curr_block_idx > 0 && tok.margin <= next_stack_block->margin)
        {
            if ((tok.type == TOKEN_TYPE_BULLET_LIST || tok.type == TOKEN_TYPE_NUMBERED_LIST || next_stack_block->type == BLOCK_TYPE_LIST_ITEM) &&
                next_stack_block->margin == tok.margin)
            {
                break;

            } else {
                curr_block_idx--;
                next_stack_block = ps->block_stack[curr_block_idx];
            }
        }

        // It looks like we will continue appending items to an existing list,
        // check the list type is the same. If it isn't pop the list element
        // from the stack so we start a new one.
        if (next_stack_block->type == BLOCK_TYPE_LIST &&
            (tok.type == TOKEN_TYPE_BULLET_LIST || tok.type == TOKEN_TYPE_NUMBERED_LIST) &&
            next_stack_block->list_type != tok.type)
        {
            curr_block_idx--;
            next_stack_block = ps->block_stack[curr_block_idx];
        }

        // Pop all blocks after the current index
        ps->block_stack_len = curr_block_idx+1;

        if (ps_match(ps, TOKEN_TYPE_TITLE, NULL)) {
            struct psx_block_t *heading_block = psx_push_block (ps, psx_leaf_block_new(ps, BLOCK_TYPE_HEADING, tok.margin, tok.value));
            heading_block->heading_number = tok.heading_number;
            if (tok.heading_number == 1) {
                // I was thinking we shouldn't allow multiple titles with
                // heading level 1. Then I found out that it's also bad practice
                // to have multiple h1 elements in HTML [1]. So, ideally we
                // won't allow this.
                //
                // Still, it's possible we will use this parser as a Markdown
                // parser in the future, just because we are mostly compatible.
                // If that's the case, I don't think Markdown has the same
                // restriction and we may get input that fails here. In such
                // cases, we shouldn't fail, instead, send a warning and
                // increase the heading number of everything else by 1.
                // Unfortunately, the edge case of a text having 6 levels of
                // headings will have to collapse the last one because HTML
                // doesn't have <h7> elements.
                //
                // [1]: https://developer.mozilla.org/en-US/docs/Web/HTML/Element/Heading_Elements#multiple_h1
                psx_error (ps, "only the note title can have heading level 1");
            }

            // :pop_leaf_blocks
            ps->block_stack_len--;

        } else if (ps_match(ps, TOKEN_TYPE_PARAGRAPH, NULL)) {
            struct psx_block_t *new_paragraph = psx_push_block (ps, psx_leaf_block_new(ps, BLOCK_TYPE_PARAGRAPH, tok.margin, tok.value));

            // Sigh... more nastiness caused by the peek API...
            // :marker_over_peek
            if (*tok.value.s != '^' || sstr_find_char(&tok.value, ' ') != NULL) {
                psx_append_paragraph_continuation_lines (ps, new_paragraph);

            } else {
                // The block paragraph token was actually the beginning of
                // block attributes this means the block's content is actually
                // empty.
                strn_set (&new_paragraph->inline_content, "", 0);

                // :restore_pos
                ps->pos = tok.value.s;
                ps->column_number = 0;
            }

            psx_parse_block_attributes(ps, new_paragraph, NULL);

            // Paragraph blocks are never left in the stack. Maybe we should
            // just not push leaf blocks into the stack...
            // :pop_leaf_blocks
            ps->block_stack_len--;

        } else if (ps_match(ps, TOKEN_TYPE_CODE_HEADER, NULL)) {
            struct psx_block_t *new_code_block = psx_push_block (ps, psx_leaf_block_new(ps, BLOCK_TYPE_CODE, tok.margin, SSTRING("",0)));

            char *start = ps->pos;
            int min_leading_spaces = INT32_MAX;
            struct psx_token_t tok_peek = ps_next_peek(ps);
            while (tok_peek.is_eol && tok_peek.type == TOKEN_TYPE_CODE_LINE) {
                ps_next(ps);

                if (!is_empty_line(tok_peek.value)) {
                    int space_count = 0;
                    while (is_space (tok_peek.value.s + space_count)) {
                        space_count++;
                    }

                    if (space_count >= 0) { // Ignore empty where we couldn't find any non-whitespace character.
                        min_leading_spaces = MIN (min_leading_spaces, space_count);
                    }
                }

                tok_peek = ps_next_peek(ps);
            }
            ps_next(ps);

            // TODO: I don't think the token peek API was a very good idea.
            // Backing up the position then restoring the parser seems like a
            // more powerful approach as it allows restoring after an arbitrary
            // number of calls to the tokenizer. This seems to be the only place
            // where we heavily use the peek API, maybe refactor this to not use
            // it and then remove the peek functions.
            // :marker_over_peek
            ps_restore_pos (ps, start);

            // Concatenate lines to inline content while removing  the most
            // leading spaces we can remove. I call this automatic space
            // normalization.
            bool is_start = true; // Used to strip trailing empty lines.
            tok_peek = ps_next_peek(ps);
            while (tok_peek.is_eol && tok_peek.type == TOKEN_TYPE_CODE_LINE) {
                ps_next(ps);

                if (!is_start || !is_empty_line(tok_peek.value)) {
                    is_start = false;
                    if (min_leading_spaces < tok_peek.value.len) {
                        strn_cat_c(&new_code_block->inline_content, tok_peek.value.s+min_leading_spaces, tok_peek.value.len-min_leading_spaces);
                    } else {
                        str_cat_c(&new_code_block->inline_content, "\n");
                    }
                }

                tok_peek = ps_next_peek(ps);
            }

            // :pop_leaf_blocks
            ps->block_stack_len--;

        } else if (ps_match(ps, TOKEN_TYPE_BULLET_LIST, NULL) || ps_match(ps, TOKEN_TYPE_NUMBERED_LIST, NULL)) {
            // Only push a list block if we are not currently located at one.
            struct psx_block_t *prnt = ps->block_stack[curr_block_idx];
            if (prnt->type != BLOCK_TYPE_LIST) {
                struct psx_block_t *list_block = psx_push_block (ps, psx_container_block_new(ps, BLOCK_TYPE_LIST, prnt->margin));
                list_block->content_start = tok.content_start;
                list_block->list_type = tok.type;
            }

           struct psx_block_t *list_item = psx_push_block(ps, psx_container_block_new(ps, BLOCK_TYPE_LIST_ITEM, tok.margin));

            struct psx_token_t tok_peek = ps_next_peek(ps);
            if (tok_peek.is_eol && tok_peek.type == TOKEN_TYPE_PARAGRAPH) {
                ps_next(ps);

                list_item->margin = tok_peek.margin;
                struct psx_block_t *new_paragraph = psx_push_block(ps, psx_leaf_block_new(ps, BLOCK_TYPE_PARAGRAPH, tok_peek.margin, tok_peek.value));
                psx_append_paragraph_continuation_lines (ps, new_paragraph);

                // :pop_leaf_blocks
                ps->block_stack_len--;
            }
        }
    }
}

struct psx_block_t* parse_note_text (mem_pool_t *pool, struct psx_parser_ctx_t *ctx, char *note_text)
{
    STACK_ALLOCATE (struct psx_parser_state_t, ps);
    ps->ctx = *ctx;
    ps_init (ps, note_text);

    ps->block_allocation.pool = pool;

    struct psx_block_t *root_block = psx_container_block_new(ps, BLOCK_TYPE_ROOT, 0);
    DYNAMIC_ARRAY_APPEND (ps->block_stack, root_block);

    sstring_t title = {0};
    psx_parse_note_title (ps, &title, true);
    if (!ps->error) {
        struct psx_block_t *title_block = psx_push_block (ps, psx_leaf_block_new(ps, BLOCK_TYPE_HEADING, ps->token.margin, title));
        title_block->heading_number = 1;

        // :pop_leaf_blocks
        ps->block_stack_len--;
    }

    // Parse note's content
    psx_parse (ps);

    if (ps->error) {
        root_block = NULL;
        // TODO: If parsing fails and we allocated blocks, return them to the
        // free list. Right now we leak them.
        // :use_block_free_list_on_fail
    }

    ps_destroy (ps);

    return root_block;
}

struct psx_block_t* psx_parse_string (struct block_allocation_t *ba, char *text, string_t *error_msg)
{
    STACK_ALLOCATE (struct psx_parser_state_t, ps);
    ps->ctx.error_msg = error_msg;
    ps_init (ps, text);

    ps->block_allocation = *ba;

    struct psx_block_t *root_block = psx_container_block_new(ps, BLOCK_TYPE_ROOT, 0);
    DYNAMIC_ARRAY_APPEND (ps->block_stack, root_block);

    // Parse note's content
    psx_parse (ps);

    if (ps->error) {
        root_block = NULL;
        // :use_block_free_list_on_fail
    }

    ps_destroy (ps);

    return root_block;
}

void _str_cat_block_tree (string_t *str, struct psx_block_t *block, int indent, int curr_indent)
{
    str_cat_indented_printf (str, curr_indent, "type: %s\n", psx_block_type_names[block->type]);
    str_cat_indented_printf (str, curr_indent, "margin: %d\n", block->margin);
    str_cat_indented_printf (str, curr_indent, "heading_number: %d\n", block->heading_number);
    str_cat_indented_printf (str, curr_indent, "list_type: %s\n", psx_token_type_names[block->list_type]);
    str_cat_indented_printf (str, curr_indent, "content_start: %d\n", block->content_start);

    str_cat_indented_printf (str, curr_indent, "data: ");
    if (block->data == NULL) {
        str_cat_debugstr (str, curr_indent, 0, "");
    } else {
        str_cat_debugstr (str, curr_indent, ESC_COLOR_CYAN, "TSPLX");
        // TODO: Ideally we would use something like this for debugging. But
        // currently the API requires passing the global simplex data context. I
        // think we shouldn't be asking for that parameter so I'm hesitant to
        // add it throug all the callstack as function parameter. A quick
        // solution would be to add an alias to psx_block_t or to splx_node_t
        // but I still think it's not worth it just fot a debug function...
        //
        //string_t tsplx_str = {0};
        //str_cat_splx_canonical (&tsplx_str, ...? sd, block->data);
        //str_cat_debugstr (str, curr_indent, ESC_COLOR_YELLOW, str_data(&tsplx_str));
        //str_free (&tsplx_str);
    }

    if (block->block_content == NULL) {
        str_cat_indented_printf (str, curr_indent, "inline_content:");

        if (str_len(&block->inline_content) > 0) {
            str_cat_printf (str, "\n");
        } else {
            str_cat_printf (str, " ");
        }

        str_cat_debugstr (str, curr_indent, ESC_COLOR_YELLOW, str_data(&block->inline_content));
        str_cat_printf (str, "\n");

    } else {
        str_cat_indented_printf (str, curr_indent, "block_content:\n");
        LINKED_LIST_FOR (struct psx_block_t*, curr_child, block->block_content) {
            _str_cat_block_tree (str, curr_child, indent, curr_indent + indent);
        }
    }
}

void str_cat_block_tree (string_t *str, struct psx_block_t *root, int indent)
{
    _str_cat_block_tree (str, root, indent, 0);
}

void printf_block_tree (struct psx_block_t *root, int indent)
{
    string_t str = {0};
    str_cat_block_tree (&str, root, indent);
    printf ("%s", str_data(&str));
    str_free (&str);
}

// @AUTO_MACRO_PREFIX(PROCESS_NOTE_)
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


    // Parse                  @AUTO_MACRO(BEGIN)
    note->tree = parse_note_text (pool_l, ctx, markup);
    if (note->tree == NULL) {
        note->error = true;
    }
    // @AUTO_MACRO(END)


    // Create Links           @AUTO_MACRO(BEGIN)
    if (!note->error) {
        psx_create_links (ctx, &note->tree);
    }
    // @AUTO_MACRO(END)


    // User Callbacks         @AUTO_MACRO(BEGIN)
    if (!note->error) {
        psx_block_tree_user_callbacks (ctx, ba, &note->tree);
    }
    // @AUTO_MACRO(END)


    // Generate HTML          @AUTO_MACRO(BEGIN)
    if (!note->error) {
        struct html_t *html = html_new (pool_l, "div");
        html_element_attribute_set (html, html->root, "id", note->id);
        block_tree_to_html (ctx, html, note->tree, html->root);

        if (html != NULL) {
            str_set (&note->html, "");
            str_cat_html (&note->html, html, 2);
            note->is_html_valid = true;

        } else {
            note->error = true;
        }
    }
    // @AUTO_MACRO(END)

    mem_pool_destroy (&_pool_l);

    char *html_out = pom_strndup (pool_out, str_data(&note->html), str_len(&note->html));
    return html_out;
}

// Replace a block with a linked list of blocks. The block to be replaced is
// referred to by the double pointer representing its parent. There is also a
// shorthand for the case where the linked list consists of a single block.
#define psx_replace_block(ba,old_block,new_block) psx_replace_block_multiple(ba,old_block,new_block,new_block)
void psx_replace_block_multiple (struct block_allocation_t *ba, struct psx_block_t **old_block, struct psx_block_t *new_start, struct psx_block_t *new_end)
{
    struct psx_block_t *tmp = *old_block;
    new_end->next = (*old_block)->next;
    *old_block = new_start;
    LINKED_LIST_PUSH (ba->blocks_fl, tmp);
}

// This is here because it's trying to emulate how users would define custom
// tags. We use it for an internal tag just as test for the API.
PSX_USER_TAG_CB (summary_tag_handler)
{
    struct psx_tag_t *tag = ps_parse_tag (ps_inline, NULL);

    struct note_t *note = rt_get_note_by_title (&tag->content);
    if (note != NULL) {
        struct psx_block_t *result = NULL;
        struct psx_block_t *result_end = NULL;

        mem_pool_t pool = {0};
        struct psx_block_t *note_tree = parse_note_text (&pool, ctx, str_data(&note->psplx));

        struct psx_block_t *title = note_tree->block_content;
        struct psx_block_t *new_title = psx_block_new_cpy (block_allocation, title);
        // NOTE: We can use inline tags because they are processed at a later step.
        str_set_printf (&new_title->inline_content, "\\note{%s}", str_data(&title->inline_content));
        // TODO: Don't use fixed heading of 2, instead look at the context
        // where the tag is used and use the highest heading so far. This will
        // be an interesting excercise in making sure this context is easy to
        // access from custom tags.
        new_title->heading_number = 2;
        LINKED_LIST_APPEND (result, new_title);

        struct psx_block_t *after_title = note_tree->block_content->next;
        if (after_title != NULL && after_title->type == BLOCK_TYPE_PARAGRAPH) {
            struct psx_block_t *new_summary = psx_block_new_cpy (block_allocation, after_title);
            LINKED_LIST_APPEND (result, new_summary);
        }

        mem_pool_destroy (&pool);

        psx_replace_block_multiple (block_allocation, block, result, result_end);

        // TODO: Add some user defined wrapper that allows setting a different
        // style for blocks of summary. For example if we ever get inline
        // editing from HTML we would like to disable editing in summary blocks
        // and have some UI to modify the target.

    } else {
        psx_error (ps_inline, "can't find note with title '%s'", str_data(&tag->content));
    }

    return USER_TAG_CB_STATUS_BREAK;
}

enum psx_user_tag_cb_status_t math_tag_handler (struct psx_parser_state_t *ps_inline, struct str_replacement_t *replacement, bool is_display_mode)
{
    char *original_pos;
    struct psx_tag_t *tag = ps_parse_tag_full (ps_inline, &original_pos, true);
    if (tag->has_content) {
        string_t res = {0};
        str_replace (&tag->content, "\n", " ", NULL);

        str_cat_math_strn (&res, is_display_mode, str_len(&tag->content), str_data(&tag->content));

        str_set_printf (&replacement->s, "\\html|%d|%s", str_len(&res), str_data (&res));
        str_free (&res);

    } else {
        ps_restore_pos (ps_inline, original_pos);
    }

    return USER_TAG_CB_STATUS_CONTINUE;
}

PSX_USER_TAG_CB(math_tag_inline_handler)
{
    return math_tag_handler (ps_inline, replacement, false);
}

// TODO: Is this a good way to implement display style?. I think capitalizing
// the starting M is easier to memorize than adding an attribute with a name
// that has to be remembered, is it display, displayMode, displat-mode=true?.
// An alternative could be to automatically detect math tags used as whole
// paragraphs and make these automatically display mode equations.
PSX_USER_TAG_CB(math_tag_display_handler)
{
    return math_tag_handler (ps_inline, replacement, true);
}

PSX_USER_TAG_CB(orphan_list_tag_handler)
{
    string_t res = {0};

    struct note_runtime_t *rt = rt_get ();
    LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
        if (curr_note->back_links == NULL) {
            // TODO: Maybe use a map so we don't do an O(n) search in each iteration
            // to determine if a root note is a title note?.
            bool is_title_note = false;
            for (int i=0; i < rt->title_note_ids_len; i++) {
                if (strcmp (curr_note->id, rt->title_note_ids[i]) == 0) {
                    is_title_note = true;
                }
            }

            if (!is_title_note) {
                str_cat_printf (&res, "- \\note{%s}\n", str_data(&curr_note->title));
            }
        }
    }

    struct psx_block_t *root = psx_parse_string (block_allocation, str_data (&res), NULL);
    psx_replace_block_multiple (block_allocation, block, root->block_content, root->block_content_end);
    return USER_TAG_CB_STATUS_BREAK;
}

// Register internal custom callbacks
#define PSX_INTERNAL_CUSTOM_TAG_TABLE \
PSX_INTERNAL_CUSTOM_TAG_ROW ("math",        math_tag_inline_handler) \
PSX_INTERNAL_CUSTOM_TAG_ROW ("Math",        math_tag_display_handler) \
PSX_INTERNAL_CUSTOM_TAG_ROW ("summary",     summary_tag_handler) \
PSX_INTERNAL_CUSTOM_TAG_ROW ("orphan_list", orphan_list_tag_handler) \

void psx_populate_internal_cb_tree (struct psx_user_tag_cb_t *tree)
{
#define PSX_INTERNAL_CUSTOM_TAG_ROW(name,cb) psx_user_tag_cb_insert (tree, name, cb);
    PSX_INTERNAL_CUSTOM_TAG_TABLE
#undef PSX_INTERNAL_CUSTOM_TAG_ROW
}
