/*
 * Copyright (C) 2021 Santiago León O.
 */

#include <limits.h>
#include "tsplx_parser.h"
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
    mem_pool_t pool;

    string_t path;

    char *id;
    string_t title;
    string_t psplx;

    struct psx_block_t *tree;

    struct html_t *html;

    bool error;
    string_t error_msg;

    struct note_t *next;
};

void note_init (struct note_t *note)
{
    mem_pool_t *pool = &note->pool;
    str_pool (pool, &note->path);
    str_pool (pool, &note->title);
    str_pool (pool, &note->psplx);
    str_pool (pool, &note->error_msg);
}

void note_destroy (struct note_t *note)
{
    mem_pool_destroy (&note->pool);
}

// :content_width
int psx_content_width = 728; // px

struct psx_parser_ctx_t {
    struct note_runtime_t *rt;
    char *id;
    char *path;
    struct file_vault_t *vlt;
    struct splx_data_t *sd;

    struct note_t *note;

    string_t *error_msg;
};

struct psx_parser_state_t {
    struct block_allocation_t block_allocation;

    mem_pool_t pool;

    struct psx_parser_ctx_t ctx;

    bool error;

    struct scanner_t scr;

    bool is_eol;
    bool is_peek;
    char *pos_peek;

    struct psx_token_t token;
    struct psx_token_t token_peek;

    DYNAMIC_ARRAY_DEFINE (struct psx_block_t*, block_stack);
    DYNAMIC_ARRAY_DEFINE (struct psx_block_unit_t*, block_unit_stack);
};

#define PS_SCR (&ps->scr)

void ps_init (struct psx_parser_state_t *ps, char *str)
{
    ps->scr.str = str;
    ps->scr.pos = str;

    DYNAMIC_ARRAY_INIT (&ps->pool, ps->block_stack, 100);
    DYNAMIC_ARRAY_INIT (&ps->pool, ps->block_unit_stack, 100);
}

void ps_destroy (struct psx_parser_state_t *ps)
{
    mem_pool_destroy (&ps->pool);
}

static inline
bool char_is_operator (char c)
{
    return char_in_str(c, ",=[]{}\\^|`");
}

static inline
bool ps_is_operator (struct psx_parser_state_t *ps)
{
    return !PS_SCR->is_eof && char_is_operator(scr_curr_char(PS_SCR));
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
    if (ps->ctx.error_msg == NULL) return;

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
    if (ps->ctx.error_msg == NULL) return;

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
    if (ctx->error_msg == NULL) return;

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

// TODO: Rewrite this as a function that recives a scanner_t and can be called
// iteratively to yield all parameters.
bool ps_parse_tag_parameters (struct psx_parser_state_t *ps, struct psx_tag_parameters_t *parameters)
{
    bool has_parameters = false;

    if (parameters != NULL) {
        parameters->named.pool = &ps->pool;
    }

    char *original_pos = scr_pos(PS_SCR);
    if (scr_curr_char(PS_SCR) == '[') {
        has_parameters = true;

        while (!PS_SCR->is_eof && scr_curr_char(PS_SCR) != ']') {
            scr_advance_char (PS_SCR);

            char *start = scr_pos(PS_SCR);
            if (scr_curr_char(PS_SCR) != '\"') {
                // TODO: Allow quote strings for values. This is to allow
                // values containing "," or "]".
                // :scr_match_double_quoted_string
            }

            while (!PS_SCR->is_eof &&
                   scr_curr_char(PS_SCR) != ',' &&
                   scr_curr_char(PS_SCR) != '=' &&
                   scr_curr_char(PS_SCR) != ']')
            {
                scr_advance_char (PS_SCR);
            }

            if (PS_SCR->is_eof) {
                has_parameters = false;
            }

            if (scr_curr_char(PS_SCR) == ',' || scr_curr_char(PS_SCR) == ']') {
                if (parameters != NULL) {
                    LINKED_LIST_APPEND_NEW (&ps->pool, struct sstring_ll_t, parameters->positional, new_param);
                    new_param->v = sstr_strip(SSTRING(start, scr_pos(PS_SCR) - start));
                }

            } else if (scr_curr_char(PS_SCR) == '=') {
                sstring_t name = sstr_strip(SSTRING(start, scr_pos(PS_SCR) - start));

                // Advance over the '=' character
                scr_advance_char (PS_SCR);

                char *value_start = scr_pos(PS_SCR);
                while (!PS_SCR->is_eof &&
                       scr_curr_char(PS_SCR) != ',' &&
                       scr_curr_char(PS_SCR) != ']')
                {
                    scr_advance_char (PS_SCR);
                }

                if (PS_SCR->is_eof) {
                    has_parameters = false;
                }

                sstring_t value = sstr_strip(SSTRING(value_start, scr_pos(PS_SCR) - value_start));

                if (parameters != NULL) {
                    sstring_map_insert (&parameters->named, name, value);
                }
            }
        }

        if (scr_curr_char(PS_SCR) == ']') {
            scr_advance_char (PS_SCR);
        } else {
            has_parameters = false;
        }
    }

    if (!has_parameters) {
        ps->scr.pos = original_pos;
        ps->scr.is_eof = false;
    }

    return has_parameters;
}

void psx_match_tag_parameters (struct scanner_t *scr,
                               char **parameters, uint32_t *parameters_len)
{
    struct scanner_t bak = *scr;
    if (scr_curr_char(scr) == '[') {
        scr_advance_char (scr);

        char *parameters_start = scr->pos;
        while (!scr->is_eof &&
               (!char_is_operator(scr_curr_char(scr)) || scr_curr_char(scr) == ',')) {
            scr_match_double_quoted_string (scr);

            scr_consume_spaces(scr);
            scr_advance_char (scr); // Skip the ','
            scr_consume_spaces(scr);
        }

        if (scr_curr_char (scr) == ']') {
            if (parameters != NULL) *parameters = parameters_start;
            if (parameters_len != NULL) *parameters_len = scr->pos - parameters_start;

            // Advance over ] character
            scr_advance_char (scr);

        } else {
            *scr = bak;
        }
    }
}

bool psx_match_tag_content (struct scanner_t *scr,
                            bool expect_balanced_braces,
                            char **content, uint32_t *content_len)
{
    char *res = NULL;
    uint32_t res_len = 0;

    if (scr_curr_char(scr) == '{') {
        if (!expect_balanced_braces) {
            scr_advance_char (scr);
            scr_match_until_unescaped_operator (scr, '}', &res, &res_len);

        } else {
            struct scanner_t bak = *scr;
            int brace_level = 1;

            char *content_start = scr->pos + 1/*starting { character*/;
            while (!scr->is_eof && brace_level != 0) {
                scr_advance_char (scr);

                if (scr_curr_char(scr) == '{') {
                    brace_level++;
                } else if (scr_curr_char(scr) == '}') {
                    brace_level--;
                }
            }

            if (!scr->is_eof && brace_level == 0) {
                res = content_start;
                res_len = scr->pos - content_start;
                scr_advance_char (scr);

            } else {
                *scr = bak;
            }
        }

    } else if (scr_curr_char(scr) == '|') {
        scr_advance_char (scr);

        char *start = scr->pos;
        while (!scr->is_eof && *(scr->pos) != '|') {
            scr_advance_char (scr);
        }

        char *content_start = NULL;
        char *number_end;
        size_t size = strtoul (start, &number_end, 10);
        if (scr->pos == number_end) {
            scr_advance_char (scr);
            content_start = scr->pos;

            // TODO: We may be going past the end of the note!!! this is
            // unsafe!
            scr->pos += size;

        } else {
            // TODO: Implement sentinel based tag content parsing
            // \code|SENTINEL|int a[2] = {1,2}SENTINEL
        }

        if (!scr->is_eof && content_start != NULL) {
            res = content_start;
            res_len = scr->pos - content_start;
        }
    }

    if (content != NULL) *content = res;
    if (content_len != NULL) *content_len = res_len;

    return res != NULL;
}

void psx_match_tag_data (struct scanner_t *scr,
                         bool expect_balanced_braces,

                         // Out
                         char **parameters, uint32_t *parameters_len,
                         char **content, uint32_t *content_len)
{
    if (scr_curr_char(scr) == '[' || scr_curr_char(scr) == '{' || scr_curr_char(scr) == '|') {
        psx_match_tag_parameters(scr, parameters, parameters_len);
        psx_match_tag_content(scr, expect_balanced_braces, content, content_len);
    }
}

bool psx_match_tag_id (struct scanner_t *scr,
                       char *start_chars,

                       char **tag, uint32_t *tag_len)
{
    bool success = false;

    struct scanner_t scr_bak = *scr;
    char *start = NULL;
    if (char_in_str (scr_curr_char(scr), start_chars)) {
        success = true;
        scr_advance_char (scr);

        start = scr->pos;
        while (!scr->is_eof &&
               !char_is_operator(scr_curr_char(scr)) &&
               !scr_is_space(scr) &&
               scr_curr_char(scr) != '\n')
        {
            scr_advance_char (scr);
        }
    }

    if (success) {
        if (tag != NULL) *tag = start;
        if (tag_len != NULL) *tag_len = (scr->pos - start);
    } else {
        *scr = scr_bak;
    }

    return success;
}

// Because we want to be very flexible but easy to learn, that often means
// overloading a lot of functionality into very similar syntax. When writing
// text, the concept of "weird" behavior is being overloaded into the tag
// concept. Tags may behave differently depending on their position and we now
// have text and data tags etc. This function matches a tag and returns all the
// positioning information that we may need to make follow up decision on how we
// process a specific tag.
//
// A notable aspect of this function is that it's pure, in the sense that it has
// absolutely no side effects. Its memory allocation profile is net zero. It
// just performs a sophisticated string matching then returns enough pointers
// for other functions to actually perform the necessary side effects. Or not,
// in fact, not doing anything is probably the most commonly ovelooked use case.
bool psx_match_tag (char *str, char *pos,
                    char *start_chars,
                    bool balanced_brace_content,

                    // Out
                    char **tag, uint32_t *tag_len,
                    char **parameters, uint32_t *parameters_len,
                    char **content, uint32_t *content_len,
                    char **end,
                    bool *at_end_of_block)
{
    bool success = true;

    STACK_ALLOCATE(struct scanner_t, scr);
    scr->str = str;
    scr->pos = pos;

    bool after_line_break = (pos > str && scr_prev_char(scr) == '\n');
    scr_consume_spaces (scr);

    success = psx_match_tag_id(scr, start_chars,
                               tag, tag_len);

    if (success) {
        psx_match_tag_data(scr,
                           balanced_brace_content,
                           parameters, parameters_len,
                           content, content_len);

        if (at_end_of_block != NULL) {
            *at_end_of_block = false;

            if ((scr_match_str (scr, "\n\n") || scr_match_str (scr, "\n\0")) && after_line_break)  {
                *at_end_of_block = true;
            }
        }

        if (end != NULL) *end = scr->pos;
    }

    return success;
}

struct psx_token_t ps_next_peek(struct psx_parser_state_t *ps)
{
    STACK_ALLOCATE (struct psx_token_t, tok);

    bool multiline_tag = false;

    char *backup_pos = scr_pos(PS_SCR);
    int backup_column_number = ps->scr.column_number;

    scr_consume_spaces (PS_SCR);
    tok->margin = MAX(ps->scr.column_number-1, 0);
    tok->type = TOKEN_TYPE_PARAGRAPH;

    char *non_space_pos = scr_pos(PS_SCR);
    if (PS_SCR->is_eof || scr_curr_char(PS_SCR) == '\0') {
        // :eof_set
        ps->scr.is_eof = true;
        ps->is_eol = true;
        tok->type = TOKEN_TYPE_END_OF_FILE;

    } else if (scr_match_str(PS_SCR, "- ") || scr_match_str(PS_SCR, "* ")) {
        scr_consume_spaces (PS_SCR);

        tok->type = TOKEN_TYPE_BULLET_LIST;
        tok->value = SSTRING(non_space_pos, 1);
        tok->margin = non_space_pos - backup_pos;
        tok->content_start = scr_pos(PS_SCR) - non_space_pos;

    } else if (scr_match_digits(PS_SCR)) {
        char *number_end = scr_pos(PS_SCR);
        if (scr_pos(PS_SCR) - non_space_pos <= 9 && scr_match_str(PS_SCR, ". ")) {
            scr_consume_spaces (PS_SCR);

            tok->type = TOKEN_TYPE_NUMBERED_LIST;
            tok->value = SSTRING(non_space_pos, number_end - non_space_pos);
            tok->margin = non_space_pos - backup_pos;
            tok->content_start = scr_pos(PS_SCR) - non_space_pos;

        } else {
            // It wasn't a numbered list marker, restore position.
            ps->scr.pos = backup_pos;
            ps->scr.column_number = backup_column_number;
        }

    } else if (scr_match_str(PS_SCR, "#")) {
        int heading_number = 1;
        while (scr_curr_char(PS_SCR) == '#') {
            heading_number++;
            scr_advance_char (PS_SCR);
        }

        if (heading_number <= 6) {
            scr_consume_spaces (PS_SCR);

            tok->value = sstr_strip(scr_advance_line (PS_SCR));
            tok->is_eol = true;
            tok->margin = non_space_pos - backup_pos;
            tok->type = TOKEN_TYPE_TITLE;
            tok->heading_number = heading_number;

        } else {
            ps->scr.pos = backup_pos;
            ps->scr.column_number = backup_column_number;
        }

    } else if (scr_match_str(PS_SCR, "\\code")) {
        psx_match_tag_parameters(PS_SCR, NULL, NULL);

        if (scr_match_str(PS_SCR, "\n")) {
            tok->type = TOKEN_TYPE_CODE_HEADER;
            while (scr_is_space(PS_SCR) || scr_curr_char(PS_SCR) == '\n') {
                scr_advance_char (PS_SCR);
            }

        } else {
            // False alarm, this was an inline code block, restore position.
            // :restore_pos
            ps->scr.pos = backup_pos;
            ps->scr.column_number = backup_column_number;
        }

    } else if (psx_match_tag(PS_SCR->str, PS_SCR->pos, "\\", true,
                             NULL, NULL,
                             NULL, NULL,
                             NULL, NULL,
                             &PS_SCR->pos,
                             NULL) ||
               psx_match_tag(PS_SCR->str, PS_SCR->pos, "\\", false,
                             NULL, NULL,
                             NULL, NULL,
                             NULL, NULL,
                             &PS_SCR->pos,
                             NULL))
    {
        // A tag may contain empty lines in its content. Match such tags at the
        // start of a block and skip over those empty lines so they don't cause
        // a paragraph break. All these lines will be passed as a token of type
        // TOKEN_TYPE_PARAGRAPH.

        multiline_tag = true;
        scr_advance_line (PS_SCR);

        tok->value = sstr_strip(SSTRING(backup_pos, PS_SCR->pos - backup_pos));
        tok->is_eol = true;

    } else if (scr_match_str(PS_SCR, "|")) {
        tok->value = scr_advance_line (PS_SCR);
        tok->is_eol = true;
        tok->type = TOKEN_TYPE_CODE_LINE;

    } else if (scr_match_str(PS_SCR, "\n")) {
        tok->type = TOKEN_TYPE_BLANK_LINE;

    }

    if (tok->type == TOKEN_TYPE_PARAGRAPH && !multiline_tag) {
        tok->value = sstr_strip(scr_advance_line (PS_SCR));
        tok->is_eol = true;
    }

    // Because we are only peeking, restore the old position and store the
    // resulting one in a separate variable.
    assert (!ps->is_peek && "Trying to peek more than once in a row");
    ps->is_peek = true;
    ps->token_peek = *tok;
    ps->pos_peek = scr_pos(PS_SCR);
    ps->scr.pos = backup_pos;

    //printf ("%s: '%.*s'\n", psx_token_type_names[ps->token_peek.type], ps->token_peek.value.len, ps->token_peek.value.s);

    return ps->token_peek;
}

struct psx_token_t ps_next(struct psx_parser_state_t *ps) {
    if (!ps->is_peek) {
        ps_next_peek(ps);
    }

    ps->scr.pos = ps->pos_peek;
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

    if (PS_SCR->is_eof || scr_curr_char(PS_SCR) == '\0') {
        // :eof_set
        ps->scr.is_eof = true;

    } else if (escape_operators && scr_curr_char(PS_SCR) == '\\' &&
               char_is_operator(scr_next_char(PS_SCR)) && scr_next_char(PS_SCR) != '\\') {
        // Move past \ character
        scr_advance_char (PS_SCR);

        // Create a text token with escaped operator as value
        tok.value = SSTRING(scr_pos(PS_SCR), 1);
        tok.type = TOKEN_TYPE_TEXT;
        scr_advance_char (PS_SCR);

    } else if ((scr_curr_char(PS_SCR) == '\\' || scr_curr_char(PS_SCR) == '^') &&
               psx_match_tag_id (PS_SCR, "\\^", &tok.value.s, &tok.value.len)) {
        if (*(tok.value.s - 1) == '\\') {
            tok.type = TOKEN_TYPE_TEXT_TAG;
        } else {
            tok.type = TOKEN_TYPE_DATA_TAG;
        }

    } else if (ps_is_operator(ps)) {
        tok.type = TOKEN_TYPE_OPERATOR;
        tok.value = SSTRING(scr_pos(PS_SCR), 1);
        scr_advance_char (PS_SCR);

    } else if (scr_is_space(PS_SCR)) {
        // This collapses consecutive spaces into a single one.
        tok.coalesced_spaces = 0;
        while (scr_is_space(PS_SCR)) {
            tok.coalesced_spaces++;
            scr_advance_char (PS_SCR);
        }

        tok.value = SSTRING(" ", 1);
        tok.type = TOKEN_TYPE_SPACE;

    } else {
        char *start = scr_pos(PS_SCR);
        while (!PS_SCR->is_eof && !ps_is_operator(ps) && !scr_is_space(PS_SCR)) {
            scr_advance_char (PS_SCR);
        }

        tok.value = SSTRING(start, scr_pos(PS_SCR) - start);
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

struct psx_tag_t* psx_tag_new (mem_pool_t *pool)
{
    assert (pool != NULL);

    struct psx_tag_t *tag = mem_pool_push_struct (pool, struct psx_tag_t);
    *tag = ZERO_INIT (struct psx_tag_t);

    tag->parameters.named.pool = pool;
    str_pool (pool, &tag->content);

    return tag;
}

void ps_restore_pos (struct psx_parser_state_t *ps, char *original_pos)
{
    ps->scr.pos = original_pos;
    ps->scr.is_eof = false;
}

void str_cat_literal_token (string_t *str, struct psx_token_t token)
{
    if (token.type == TOKEN_TYPE_TEXT_TAG) {
        str_cat_c (str, "\\");
        strn_cat_c (str, token.value.s, token.value.len);

    } else if (token.type == TOKEN_TYPE_DATA_TAG) {
        str_cat_c (str, "^");
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

    char *last_start = scr_pos(PS_SCR);
    while (!PS_SCR->is_eof) {
        scr_advance_char (PS_SCR);

        if (scr_curr_char(PS_SCR) == c) {
            if (scr_prev_char(PS_SCR) == '\\') {
                strn_cat_c (tgt, last_start, scr_pos(PS_SCR) - last_start - 1);
                str_cat_char (tgt, c, 1);

                scr_advance_char (PS_SCR);
                last_start = scr_pos(PS_SCR);

                if (scr_curr_char(PS_SCR) == c) break;
            } else {
                break;
            }

        }
    }

    if (scr_curr_char(PS_SCR) == c) {
        strn_cat_c (tgt, last_start, scr_pos(PS_SCR) - last_start);
        last_start = scr_pos(PS_SCR);

        // Advance over the _c_ character
        scr_advance_char (PS_SCR);

    } else {
        success = false;
    }

    return success;
}

bool psx_cat_tag_content (struct psx_parser_state_t *ps, string_t *str, bool expect_balanced_braces)
{
    char *content;
    uint32_t content_len;
    psx_match_tag_content (PS_SCR, expect_balanced_braces, &content, &content_len);

    if (content != NULL) {
        strn_cat_c (str, content, content_len);
    }

    return content != NULL;
}

// TODO: This should also be rewritten, I think we can instead call into
// psx_match_tag_data() and reuse that parsing logic. Then proceed to perform
// parameter parsing. This would decouple a bit tag parsing from parameter
// parsing.
#define ps_parse_tag(ps,end) ps_parse_tag_full(&ps->pool,ps,end,false)
struct psx_tag_t* ps_parse_tag_full (mem_pool_t *pool, struct psx_parser_state_t *ps, char **end, bool expect_balanced_braces)
{
    if (end != NULL) *end = scr_pos(PS_SCR);

    mem_pool_marker_t mrk = mem_pool_begin_temporary_memory (pool);

    struct psx_tag_t *tag = psx_tag_new (pool);
    tag->token = ps->token;

    char *original_pos = scr_pos(PS_SCR);
    if (scr_curr_char(PS_SCR) == '[' || scr_curr_char(PS_SCR) == '{' || scr_curr_char(PS_SCR) == '|') {
        tag->has_parameters = ps_parse_tag_parameters(ps, &tag->parameters);

        if (tag->has_parameters && end != NULL) *end = scr_pos(PS_SCR);

        tag->has_content = psx_cat_tag_content(ps, &tag->content, expect_balanced_braces);

        if (!tag->has_parameters && !tag->has_content) {
            ps->scr.pos = original_pos;
            ps->scr.is_eof = false;
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

struct html_element_t* psx_get_head_html_element (struct psx_parser_state_t *ps)
{
    struct psx_block_unit_t *head_ctx = DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack);
    return head_ctx->html_element;
}

void psx_append_html_element (struct psx_parser_state_t *ps, struct html_t *html, struct html_element_t *html_element)
{
    html_element_append_child (html, psx_get_head_html_element(ps), html_element);
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

static inline
bool entity_is_visible(struct splx_node_t *node)
{
    struct note_runtime_t *rt = rt_get();

    bool is_visible = true;

    if (rt != NULL && rt->is_public) {
        for (int i=0; i<rt->private_types_len; i++) {
            if (splx_node_attribute_contains(node, "a", rt->private_types[i])) {
                is_visible = false;
            }
        }
    }

    return is_visible;
}

static inline
bool entity_is_visible_id(char *id)
{
    struct note_runtime_t *rt = rt_get();

    bool is_visible = true;

    if (rt != NULL) {
        struct splx_node_t *entity = splx_get_node_by_id (&rt->sd, id);
        is_visible = entity_is_visible (entity);
    }

    return is_visible;
}

void psx_maybe_warn_hidden_target(struct psx_parser_state_t *ps, char *target_id)
{
    struct note_runtime_t *rt = rt_get();
    if (rt != NULL && ps != NULL) {
        struct splx_node_t *tgt = splx_get_node_by_id (&rt->sd, target_id);
        if (!entity_is_visible (tgt)) {
            string_t *name = splx_node_get_name (tgt);
            psx_warning (ps, "generated link to hidden target: %s", str_data(name));
        }
    }
}

void str_cat_section_id (string_t *str, char *section, bool user_content)
{
    string_t clean = {0};
    str_set (&clean, section);

    // Remove characters: #!"#$%&'()*+,./:;<=>?@[]^`{|}~
    // TODO: Do this with a loop...
    str_replace(&clean, "!", "", NULL);
    str_replace(&clean, "\"", "", NULL);
    str_replace(&clean, "#", "", NULL);
    str_replace(&clean, "$", "", NULL);
    str_replace(&clean, "%", "", NULL);
    str_replace(&clean, "&", "", NULL);
    str_replace(&clean, "'", "", NULL);
    str_replace(&clean, "(", "", NULL);
    str_replace(&clean, ")", "", NULL);
    str_replace(&clean, "*", "", NULL);
    str_replace(&clean, "+", "", NULL);
    str_replace(&clean, ",", "", NULL);
    str_replace(&clean, ".", "", NULL);
    str_replace(&clean, "/", "", NULL);
    str_replace(&clean, ":", "", NULL);
    str_replace(&clean, ";", "", NULL);
    str_replace(&clean, "<", "", NULL);
    str_replace(&clean, "=", "", NULL);
    str_replace(&clean, ">", "", NULL);
    str_replace(&clean, "?", "", NULL);
    str_replace(&clean, "@", "", NULL);
    str_replace(&clean, "[", "", NULL);
    str_replace(&clean, "\\", "", NULL);
    str_replace(&clean, "]", "", NULL);
    str_replace(&clean, "^", "", NULL);
    str_replace(&clean, "`", "", NULL);
    str_replace(&clean, "{", "", NULL);
    str_replace(&clean, "|", "", NULL);
    str_replace(&clean, "}", "", NULL);
    str_replace(&clean, "~", "", NULL);

    str_replace(&clean, "\t", "", NULL);
    str_replace(&clean, "\n", "", NULL);

    str_replace(&clean, " ", "-", NULL);
    str_replace(&clean, "'", "", NULL);

    char *lowercase = cstr_to_lower(str_data(&clean));
    if (user_content) {
        str_cat_printf (str, "user-content-%s", lowercase);
    } else {
        str_cat_printf (str, "%s", lowercase);
    }
    free (lowercase);

    str_free(&clean);
}

struct html_element_t* html_append_note_link(struct psx_parser_state_t *ps, struct html_t *html, struct html_element_t *parent,
                                             char *target_id,
                                             char *section,
                                             bool display_section,
                                             string_t *text)
{
    string_t buff = {0};

    psx_maybe_warn_hidden_target (ps, target_id);

    struct html_element_t *link_element = html_new_element (html, "a");
    str_set_printf (&buff, "?n=%s", target_id);
    if (section != NULL && *section != '\0') {
        str_cat_c (&buff, "#");
        str_cat_section_id (&buff, section, false);
    }
    html_element_attribute_set (html, link_element, "href", str_data(&buff));

    if (section != NULL && *section != '\0') {
        str_set (&buff, "");
        str_cat_section_id (&buff, section, false);
        str_set_printf (&buff, "return open_note('%s', '%s');", target_id, str_data(&buff));
    } else {
        str_set_printf (&buff, "return open_note('%s');", target_id);
    }
    html_element_attribute_set (html, link_element, "onclick", str_data(&buff));
    html_element_class_add (html, link_element, "note-link");

    html_element_append_strn (html, link_element, str_len(text), str_data(text));
    if (section != NULL && *section != '\0' && display_section) {
        struct html_element_t *bold = html_new_element (html, "b");
        html_element_append_strn (html, bold, 3, " > ");
        html_element_append_child (html, link_element, bold);

        html_element_append_strn (html, link_element, strlen(section), section);
    }
    html_element_append_child (html, parent, link_element);

    str_free(&buff);
    return link_element;
}

struct html_element_t* html_append_entity_link(struct psx_parser_state_t *ps, struct html_t *html, struct html_element_t *parent,
                                               char *target_id,
                                               char *section,
                                               bool display_section,
                                               string_t *text)
{
    string_t buff = {0};

    psx_maybe_warn_hidden_target (ps, target_id);

    struct html_element_t *link_element = html_new_element (html, "a");
    str_set_printf (&buff, "?v=%s", target_id);
    // Is there any useful case for an entity link having a section?...
    if (section != NULL && *section != '\0') {
        str_cat_c (&buff, "#");
        str_cat_section_id (&buff, section, false);
    }
    html_element_attribute_set (html, link_element, "href", str_data(&buff));

    str_set_printf (&buff, "return open_virtual_entity('%s');", target_id);
    html_element_attribute_set (html, link_element, "onclick", str_data(&buff));
    html_element_class_add (html, link_element, "note-link");

    html_element_append_strn (html, link_element, str_len(text), str_data(text));
    if (section != NULL && *section != '\0' && display_section) {
        struct html_element_t *bold = html_new_element (html, "b");
        html_element_append_strn (html, bold, 3, " > ");
        html_element_append_child (html, link_element, bold);

        html_element_append_strn (html, link_element, strlen(section), section);
    }
    html_element_append_child (html, parent, link_element);

    str_free(&buff);
    return link_element;
}

void psx_advance_tag (struct psx_parser_state_t *ps)
{
    ps_inline_next (ps);
    assert(ps_match(ps, TOKEN_TYPE_TEXT_TAG, NULL) || ps_match(ps, TOKEN_TYPE_DATA_TAG, NULL));
    psx_match_tag_data (&ps->scr, true, NULL, NULL, NULL, NULL);
}

void html_append_link (struct psx_parser_state_t *ps,
                       struct html_t *html,
                       struct html_element_t *parent,
                       string_t *text,
                       struct splx_node_t *target,
                       char *section,
                       string_t *type, string_t *name)
{
    if (type == NULL || name == NULL) return;

    // TODO: Does removing this cause any issues? If not, then also remove the
    // passing of type/name parameters
    //struct note_runtime_t *rt = rt_get();
    //struct splx_node_t *target = splx_get_node_by_name_optional_type(&rt->sd, str_data(type), str_data(name));

    bool display_section = false;
    string_t *html_text = text;
    if (text == NULL || str_len(text) == 0) {
        html_text = splx_node_get_name(target);
        display_section = true;
    }

    if (entity_is_visible(target)) {
        if (splx_node_is_referenceable(target)) {
            html_append_note_link (ps, html,
                                   parent,
                                   str_data(splx_node_get_id(target)),
                                   section,
                                   display_section,
                                   html_text);

        } else {
            struct splx_node_t *virtual_id = splx_node_get_attribute (target, "t:virtual_id");
            html_append_entity_link (ps, html,
                                     parent,
                                     str_data(splx_node_get_id (virtual_id)),
                                     section,
                                     display_section,
                                     html_text);
        }
    }
}

void psx_parse_link (char *str, string_t *text_o, string_t *reference_o, string_t *section_o)
{
    string_t text = {0};
    string_t reference = {0};
    string_t section = {0};

    STACK_ALLOCATE(struct scanner_t, scr);
    scr->str = str;
    scr->pos = str;

    bool unquoted = false;

    scr_consume_spaces (scr);
    if (scr_curr_char(scr) == '\"')
    {
        bool match = false;

        sstring_t t1 = {0};
        sstring_t t2 = {0};
        sstring_t t3 = {0};

        t1.s = scr->pos+1;
        match = scr_match_double_quoted_string (scr);
        t1.len = match ? scr->pos - t1.s - 1 : 0;

        scr_consume_spaces (scr);
        if (scr_match_str (scr, "->")) {
            scr_consume_spaces (scr);

            t2.s = scr->pos+1;
            match = scr_match_double_quoted_string (scr);
            t2.len = match ? scr->pos - t2.s - 1 : 0;
        }

        scr_consume_spaces (scr);
        while (scr_match_str (scr, ">")) {
            scr_consume_spaces (scr);

            t3.s = scr->pos+1;
            match = scr_match_double_quoted_string (scr);
            t3.len = match ? scr->pos - t3.s - 1 : 0;

            scr_consume_spaces (scr);
        }

        if (scr_curr_char (scr) != '\0') {
            unquoted = true;

        } else {
            if (t1.len > 0 && t2.len == 0 && t3.len == 0) {
                str_set_sstr (&reference, &t1);

            } else if (t1.len > 0 && t2.len > 0 && t3.len == 0) {
                str_set_sstr (&text, &t1);
                str_set_sstr (&reference, &t2);

            } else if (t1.len > 0 && t2.len == 0 && t3.len > 0) {
                str_set_sstr (&reference, &t1);
                str_set_sstr (&section, &t3);

            } else {
                str_set_sstr (&text, &t1);
                str_set_sstr (&reference, &t2);
                str_set_sstr (&section, &t3);
            }
        }
    } else {
        unquoted = true;
    }

    if (unquoted) {
        char *arrow =  "->";
        int arrow_len =  strlen(arrow);
        char *last_arrow = NULL;
        for (char *s=scr_advance_until_str(scr, arrow);
             s;
             s = scr_advance_until_str(scr, arrow))
        {
            scr_match_str(scr, arrow);
            last_arrow = s;
        }

        if (last_arrow != NULL && *(last_arrow + arrow_len) != '\0') {
            strn_set (&text, scr->str, last_arrow - scr->str);
            str_set (&reference, last_arrow + arrow_len);
        } else {
            str_set (&reference, str);

            // In unquoted syntax a reference being set to "->" is very likely
            // to be broken notation. To create a page titled "->" use quoted
            // syntax.
            str_strip(&reference);
            if (strcmp ("->", str_data(&reference)) == 0) {
                 str_set (&reference, "");
            }
        }
    }

    str_replace(&text, "\\\"", "\"", NULL);
    str_normalize_spaces(&text, " \t\n");

    str_replace(&reference, "\\\"", "\"", NULL);
    str_normalize_spaces(&reference, " \t\n");

    if (!unquoted) {
        str_replace(&section, "\\\"", "\"", NULL);
        str_normalize_spaces(&section, " \t\n");
    }


    if (text_o != NULL) str_cpy(text_o, &text);
    if (reference_o != NULL) str_cpy(reference_o, &reference);
    if (section_o != NULL) str_cpy(section_o, &section);
}

void html_redact_or_append_link (struct psx_parser_state_t *ps,
                                 struct html_t *html,
                                 struct html_element_t *parent,
                                 string_t *type, string_t *name)
{
    // TODO: Some types pass null here, see references.
    if (type == NULL || name == NULL) return;

    struct note_runtime_t *rt = rt_get();


    string_t text = {0};
    string_t reference = {0};
    string_t section = {0};
    psx_parse_link (str_data(name), &text, &reference, &section);

    bool reference_is_id = (splx_get_node_by_id(&rt->sd, str_data(&reference)) != NULL);
    struct splx_node_t *target = NULL;
    if (reference_is_id) {
        target = splx_get_node_by_id (&rt->sd, str_data(&reference));

    } else {
        target = splx_get_node_by_name_optional_type(&rt->sd, str_data(type), str_data(&reference));
    }


    // TODO: Should really be doing...
    //assert (target != NULL);
    //
    // Can't do because some entities may not be created because
    // we don't recurse into \todo and \block when creating links
    // :recursive_create_links
    if (target != NULL) {
        // Maybe redact instead of creating a link
        bool redacted = false;
        if (rt!=NULL && rt->is_public && !entity_is_visible(target)) {
            struct html_element_t *html_parent = psx_get_head_html_element(ps);

            string_t text = {0};
            LINKED_LIST_FOR (struct html_element_t *, e1, html_parent->children) {
                str_cat(&text, &e1->text);
            }

            // TODO: A tag with a body containing unbalanced braces can cause an
            // early exit of a redacted parenthesis. Consider the following:
            //
            //   Public (\note{Right Parenthesis ')'} \note{Non-public) secret stuff)

            size_t pos;
            int count;
            char *text_cstr = str_data(&text);
            bool has_open_parenthesis = cstr_find_open_parenthesis(text_cstr, &pos, &count);

            if (has_open_parenthesis) {
                int curr_pos = 0;
                struct html_element_t *pprev = NULL;
                struct html_element_t *prev = NULL;
                LINKED_LIST_FOR (struct html_element_t *, curr, html_parent->children) {
                    if (curr_pos >= pos) {
                        break;
                    }

                    pprev = prev;
                    prev = curr;
                    curr_pos += str_len(&curr->text);
                }

                // TODO: This leaks the nodes that were redacted.
                if (prev != NULL) {
                    if (prev->next != NULL && prev->next->children != NULL) {
                        // If we landed at a non leaf (text) HTML node, skip
                        // until we find one. Because has_open_parenthesis == true
                        // we expect this will end and prev will never be NULL.
                        while (prev->next->children != NULL) {
                            prev = prev->next;
                        }
                        assert (prev != NULL);

                    } else if (is_empty_str(str_data(&prev->text))) {
                        // If we landed at a text node with empty text, move
                        // back to strip the space before the redaacting
                        // parenthesis.
                        prev = pprev;
                    }

                    // Strip all HTML nodes after prev.
                    prev->next = NULL;

                } else {
                    // If there's no prev, then update the parent's head, child
                    // list should be empty now.
                    html_parent->children = NULL;
                }
                html_parent->children_end = prev;

                // Handle opening parenthesis not at the start of element's
                // text.
                if (curr_pos > pos) {
                    str_shrink(&prev->text, pos - (curr_pos - str_len(&prev->text)));
                    str_rstrip (&prev->text);
                }

                cstr_find_close_parenthesis (ps->scr.pos, count, &pos);
                if (ps->scr.pos[pos] == ')') {
                    pos++;
                }
                ps_restore_pos (ps, ps->scr.pos + pos);

                redacted = true;
            }

            str_free (&text);
        }

        if (!redacted) {
            html_append_link (ps, html, parent, &text, target, str_data(&section), type, name);
        }

    } else {
        //printf ("\\%s{%s}\n", str_data(type), str_data(name));
    }

}

void psx_name_clean (string_t *str)
{
    str_replace (str, "\n", " ", NULL);
    // TODO: Reduce multiple space sequences to single space.
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

    // Remove escaping character of escaped title sequences or escaped '\' at
    // the start.
    if (content[0] == '\\' && (content[1] == '#' || content[1] == '\\')) {
        scr_advance_char(PS_SCR);
    }

    // TODO: I'm starting to think inline parser shouldn't use a stack based
    // state for handling nested tags. I'm starting to think that a recursive
    // parser would be better?, but it's just a feeling, It's possible the code
    // will still look the same.
    DYNAMIC_ARRAY_APPEND (ps->block_unit_stack, psx_block_unit_new (&ps->pool, BLOCK_UNIT_TYPE_ROOT, container));
    while (!PS_SCR->is_eof && !ps->error) {
        struct psx_token_t tok = ps_inline_next (ps);

        str_set (&buff, "");
        if (ps_match(ps, TOKEN_TYPE_TEXT, NULL) || ps_match(ps, TOKEN_TYPE_SPACE, NULL)) {
            struct psx_block_unit_t *curr_unit = DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack);

            // Don't append a space token if it's at the beginning of a
            // paragraph (there is no HTML chidren), or if the last children of
            // the paragraph ended in space.
            if (!ps_match(ps, TOKEN_TYPE_SPACE, NULL) ||
                (curr_unit->html_element->children != NULL && !html_element_child_ends_in_space(curr_unit->html_element)))
            {
                str_set_sstr (&buff, &tok.value);
                str_replace (&buff, "\n", " ", NULL);
                html_element_append_strn (html, curr_unit->html_element, str_len(&buff), str_data(&buff));
            }

        } else if (ps_match(ps, TOKEN_TYPE_OPERATOR, "}") && DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack)->type != BLOCK_UNIT_TYPE_ROOT) {
            psx_block_unit_pop (ps);

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "i") || ps_match(ps, TOKEN_TYPE_TEXT_TAG, "b")) {
            struct psx_token_t tag = ps->token;

            char *original_pos = scr_pos(PS_SCR);
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

                    string_t file_extension = {0};
                    str_set (&file_extension, str_data(&file->extension));
                    ascii_to_lower (str_data(&file_extension));

                    // TODO: Put image extensions into an array.
                    while (strncmp(str_data(&file_extension), "png", 3) != 0 &&
                           strncmp(str_data(&file_extension), "jpg", 3) != 0 &&
                           strncmp(str_data(&file_extension), "jpeg", 4) != 0 &&
                           strncmp(str_data(&file_extension), "svg", 3) != 0 &&
                           strncmp(str_data(&file_extension), "gif", 3) != 0 &&
                           strncmp(str_data(&file_extension), "webp", 4) != 0)
                    {
                        file = file->next;
                        str_set (&file_extension, str_data(&file->extension));
                        ascii_to_lower (str_data(&file_extension));
                    }

                    if (file != NULL) {
                        str_set_printf (&buff, "files/%s", str_data(&file->path));
                    }

                    str_free (&file_extension);

                } else if (cstr_starts_with(str_data(&tag->content), "http://") ||
                           cstr_starts_with(str_data(&tag->content), "https://")) {
                    str_set_printf (&buff, "%s", str_data(&tag->content));

                } else {
                    str_set_printf (&buff, "files/%s", str_data(&tag->content));
                }
                html_element_attribute_set (html, img_element, "src", str_data(&buff));

                psx_append_html_element(ps, html, img_element);

            } else {
                ps_restore_pos (ps, original_pos);
                ps_html_cat_literal_tag (ps, tag, html);
            }

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "code") || ps_match(ps, TOKEN_TYPE_OPERATOR, "`")) {
            if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "code")) {
                // TODO: Actually do something with the passed language name
                struct psx_tag_t *tag = ps_parse_tag_full (&ps->pool, ps, &original_pos, true);
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

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "html")) {
            struct psx_tag_t *tag = ps_parse_tag (ps, &original_pos);
            if (tag->has_content) {
                struct psx_block_unit_t *head_unit = ps->block_unit_stack[ps->block_unit_stack_len-1];
                html_element_append_no_escape_strn (html, head_unit->html_element, str_len(&tag->content), str_data(&tag->content));
            } else {
                ps_restore_pos (ps, original_pos);
                ps_html_cat_literal_tag (ps, tag, html);
            }

        } else if (ps_match(ps, TOKEN_TYPE_TEXT_TAG, "block") ||
                ps_match(ps, TOKEN_TYPE_TEXT_TAG, "todo") ||
                ps_match(ps, TOKEN_TYPE_TEXT_TAG, "bibtex") ||
                ps_match(ps, TOKEN_TYPE_TEXT_TAG, "cite"))
        {
            // TODO: Not implemented...
            struct psx_tag_t *tag = ps_parse_tag (ps, &original_pos);
            ps_restore_pos (ps, original_pos);
            ps_html_cat_literal_tag (ps, tag, html);

        } else if (rt_get() != NULL && ctx->note != NULL && ps_match(ps, TOKEN_TYPE_TEXT_TAG, NULL)) {
            struct note_runtime_t *rt = rt_get ();
            strn_set (&buff, ps->token.value.s, ps->token.value.len);
            psx_late_user_tag_cb_t *cb = psx_late_user_tag_cb_get(&rt->user_late_cb_tree, str_data(&buff));

            if (cb != NULL) {
                struct psx_tag_t *tag = ps_parse_tag_full (&rt->pool, ps, &original_pos, false);
                rt_queue_late_callback (ctx->note, tag, psx_get_head_html_element(ps), cb);

            } else {
                struct psx_tag_t *tag = ps_parse_tag (ps, &original_pos);

                if (tag->has_content) {
                    string_t type = {0};

                    string_t name = {0};
                    str_set (&name, str_data(&tag->content));
                    psx_name_clean (&name);

                    str_set_sstr (&type, &tag->token.value);
                    html_redact_or_append_link (ps, html, psx_get_head_html_element(ps), &type, &name);

                    str_free (&type);
                    str_free (&name);

                } else {
                    ps_restore_pos (ps, original_pos);
                    ps_html_cat_literal_tag (ps, tag, html);
                }
            }

        } else if (rt_get() != NULL && ctx->note != NULL && ps_match(ps, TOKEN_TYPE_DATA_TAG, NULL)) {
            struct psx_tag_t *tag = ps_parse_tag (ps, &original_pos);

            if (tag->parameters.positional != NULL) {
                string_t type = {0};
                string_t name = {0};

                str_set_sstr (&name, &tag->parameters.positional->v);
                str_set_sstr (&type, &tag->token.value);
                html_redact_or_append_link (ps, html, psx_get_head_html_element(ps), &type, &name);

                str_free (&type);
                str_free (&name);

            } else {
                ps_restore_pos (ps, original_pos);
                ps_html_cat_literal_tag (ps, tag, html);
            }

        } else if (PS_SCR->is_eof) {
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
    str_set_sstr (&new_block->inline_content, &inline_content);
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
// :early_tag_api
#define PSX_USER_TAG_CB(funcname) enum psx_user_tag_cb_status_t funcname(struct psx_parser_ctx_t *ctx, struct block_allocation_t *block_allocation, struct psx_parser_state_t *ps_inline, struct psx_block_t **block, struct str_replacement_t *replacement)
typedef PSX_USER_TAG_CB(psx_user_tag_cb_t);
BINARY_TREE_NEW (psx_user_tag_cb, char*, psx_user_tag_cb_t*, strcmp(a, b));
void psx_populate_internal_cb_tree (struct psx_user_tag_cb_t *tree);

// TODO: Early user callbacks will be modifying the block tree. It's possible
// the user messes up and for example adds a cycle into the tree, we should
// detect such problem and avoid maybe later entering an infinite loop.
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
        char *block_start = ps_inline->scr.pos;
        while (!ps_inline->scr.is_eof && !ps_inline->error) {
            char *tag_start = ps_inline->scr.pos;
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
                    tmp_replacement.len = ps_inline->scr.pos - tag_start;

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

void psx_set_virtual_id (struct splx_node_t *node)
{
    struct note_runtime_t *rt = rt_get();
    string_t virtual_id = {0};
    str_cat_id_random (&virtual_id);
    splx_node_attribute_append_c_str(&rt->sd, node, "t:virtual_id", str_data(&virtual_id), SPLX_NODE_TYPE_STRING);
    rt->next_virtual_id++;
    str_free(&virtual_id);
}

struct splx_node_t* psx_get_or_set_entity(struct splx_data_t *sd,
                                          char *id, char *type, char *name)
{
    struct splx_node_t *entity = NULL;

    bool found = false;

    string_t name_clean = {0};
    str_set(&name_clean, name);
    psx_name_clean (&name_clean);

    if (id != NULL) {
        entity = splx_get_node_by_id (sd, id);
        if (entity == NULL) {
            entity = splx_node (sd, id, SPLX_NODE_TYPE_OBJECT);
        }

        found = true;

        splx_node_attribute_append_once_c_str(sd, entity, "a", type, SPLX_NODE_TYPE_OBJECT);

        if (!splx_node_attribute_contains(entity, "name", NULL)) {
            splx_node_attribute_append_once_c_str(sd, entity, "name", str_data(&name_clean), SPLX_NODE_TYPE_STRING);
        } else if (name != NULL) {
            // TODO: Log a real warning here...
            printf (ECMA_YELLOW("warning:") " attempting to set multiple names to an entity.");
        }
    }

    if (!found) {
        struct query_ctx_t qctx = {0};

        while ((entity = splx_next_by_name (&qctx, sd, str_data(&name_clean))) != NULL) {
            if (!splx_node_is_referenceable(entity) && !splx_node_attribute_contains(entity, "a", type)) {
                splx_node_attribute_append_c_str(sd, entity, "a", type, SPLX_NODE_TYPE_OBJECT);
                break;
            }

            if (splx_node_attribute_contains(entity, "a", type)) {
                found = true;
                break;
            }
        }
    }

    if (!found && entity == NULL) {
        entity = splx_node (sd, id, SPLX_NODE_TYPE_OBJECT);
        psx_set_virtual_id(entity);
        splx_node_attribute_append_c_str(sd, entity, "a", type, SPLX_NODE_TYPE_OBJECT);
        splx_node_attribute_append_c_str(sd, entity, "name", str_data(&name_clean), SPLX_NODE_TYPE_STRING);
    }

    str_free (&name_clean);

    return entity;
}

void psx_create_link(struct splx_data_t *sd,
                     struct splx_node_t *src_entity, char *src_id,
                     string_t *tgt_type, string_t *tgt_name)
{
    string_t text = {0};
    string_t reference = {0};
    string_t section = {0};
    psx_parse_link (str_data(tgt_name), &text, &reference, &section);

    if (str_len(&reference) > 0) {
        //prnt_debug_string (str_data(&text));

        bool reference_is_id = (splx_get_node_by_id(sd, str_data(&reference)) != NULL);

        struct splx_node_t *tgt_entity = NULL;
        if (reference_is_id) {
            //prnt_debug_string (str_data(&reference));
            tgt_entity = psx_get_or_set_entity(sd, str_data(&reference), str_data(tgt_type), NULL);

        } else {
            //prnt_debug_string (str_data(&reference));
            tgt_entity = psx_get_or_set_entity(sd, NULL, str_data(tgt_type), str_data(&reference));
        }

        //prnt_debug_string (str_data(&section));

        if (tgt_entity != NULL) {
            if(splx_node_is_referenceable(tgt_entity)) {
                rt_link_entities_by_id (src_id, str_data(splx_node_get_id(tgt_entity)),
                    str_data(&text), str_data(&section));
            } else {
                rt_link_entities (src_entity, tgt_entity, str_data(&text), str_data(&section));
            }
        }
    }

    str_free (&text);
    str_free (&reference);
    str_free (&section);
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

    } else if (block->type != BLOCK_TYPE_CODE) {
        STACK_ALLOCATE (struct psx_parser_state_t, ps_inline);
        ps_init (ps_inline, str_data(&block->inline_content));

        while (!ps_inline->scr.is_eof && !ps_inline->error) {
            ps_inline_next (ps_inline);
            if (ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "link") ||
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "html") ||
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "image") ||
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "code") ||
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "i") || // TODO: Recurse :recursive_create_links
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "b") || // :recursive_create_links
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "block") || // :recursive_create_links // TODO: Not implemented
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "todo") || // TODO: Not implemented
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "bibtex") || // TODO: Not implemented
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "cite") || // TODO: Not implemented
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "summary") ||
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "math") ||
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "Math") ||
                ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, "youtube"))
            {
                // Skip tag's content.
                psx_match_tag_data (&ps_inline->scr, true, NULL, NULL, NULL, NULL);

            } else if (ps_match(ps_inline, TOKEN_TYPE_DATA_TAG, NULL)) {
                char *parameters = NULL;
                uint32_t parameters_len = 0;

                psx_match_tag_data (&ps_inline->scr, true, &parameters, &parameters_len, NULL, NULL);

                if (parameters_len > 0) {
                    string_t type = {0};
                    str_set_sstr(&type, &ps_inline->token.value);
                    string_t name = strn_new (parameters, parameters_len);

                    // TODO: What should happen if an inline data tag has content?...
                    // TODO: What if it has multiple parameters or named parameters?

                    psx_create_link(&ctx->rt->sd, ctx->note->tree->data, ctx->note->id, &type, &name);

                    str_free(&type);
                    str_free(&name);
                }

            } else if (ps_match(ps_inline, TOKEN_TYPE_TEXT_TAG, NULL)) {
                char *content = NULL;
                uint32_t content_len = 0;

                psx_match_tag_data (&ps_inline->scr, true, NULL, NULL, &content, &content_len);

                if (content_len > 0) {
                    string_t type = {0};
                    str_set_sstr(&type, &ps_inline->token.value);
                    string_t name = strn_new (content, content_len);

                    psx_create_link(&ctx->rt->sd, ctx->note->tree->data, ctx->note->id, &type, &name);

                    str_free(&type);
                    str_free(&name);
                }
            }
        }

        ps_destroy (ps_inline);
    }
}


char *note_internal_attributes[] = {"name", "a", "t:virtual_id", "link", "backlink"};
char *block_internal_attributes[] = {"a", "t:virtual_id"};

void attributes_html_append (struct html_t *html, struct psx_block_t *block, struct html_element_t *parent,
                             char **skip_attrs, int skip_attrs_len)
{
    // Types
    struct splx_node_list_t *type_list = cstr_to_splx_node_list_map_get (&block->data->attributes, "a");

    if (type_list != NULL) {
        struct html_element_t *types = html_new_element (html, "div");
        html_element_class_add (html, types, "type-list");

        int visible_types_cnt = 0;
        LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, type_list) {
            char *type_str = str_data(&curr_list_node->node->str);
            if (strcmp(type_str, "note") != 0) {
                struct html_element_t *value = html_new_element (html, "div");
                html_element_class_add (html, value, "type");
                html_element_append_cstr (html, value, type_str);
                html_element_append_child (html, types, value);
                visible_types_cnt++;
            }
        }

        if (visible_types_cnt > 0) {
            html_element_append_child (html, parent, types);
        }
    }

    int num_visible_attributes = block->data->attributes.num_nodes;
    BINARY_TREE_FOR (cstr_to_splx_node_list_map, &block->data->attributes, curr_attribute) {
        if (c_str_array_contains(skip_attrs, skip_attrs_len, curr_attribute->key)) {
            num_visible_attributes--;
        }
    }

    // Attributes

    // NOTE: Avoid generating any attribute elements if the only present
    // attributer are those already shown soehwre else (note's title, type
    // labels etc.).
    if (num_visible_attributes > 0) {
        struct html_element_t *container = html_new_element (html, "div");
        html_element_class_add (html, container, "attributes");

        BINARY_TREE_FOR (cstr_to_splx_node_list_map, &block->data->attributes, curr_attribute) {
            // Type attributes "a" are rendered as labels, the "name" attribute is
            // the note's title.
            if (c_str_array_contains(skip_attrs, skip_attrs_len, curr_attribute->key)) {
                continue;
            }

            struct html_element_t *attribute = html_new_element (html, "div");
            html_element_attribute_set (html, attribute, "style", "align-items: baseline; display: flex; color: var(--secondary-fg-color)");

            struct html_element_t *label = html_new_element (html, "div");
            html_element_class_add (html, label, "attribute-label");
            html_element_append_cstr (html, label, curr_attribute->key);
            html_element_append_child (html, attribute, label);

            // NOTE: This additionar wrapper div for values is added so that they
            // wrap nicely and are left aligned with all other values.
            struct html_element_t *values = html_new_element (html, "div");
            html_element_attribute_set (html, values, "style", "row-gap: 3px; display: flex; flex-wrap: wrap; overflow-x: auto;");
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

// I'm anticipating the actual HTML for attributes to be changing a lot, so I
// don't want to break all tests every time a change is made. Instead, for tests
// we compare TSPLX data in canonical form and in the HTML we just output a
// placeholder div that represents all attributes.
void note_attributes_html_append (struct html_t *html, struct psx_block_t *block, struct html_element_t *parent)
#ifdef ATTRIBUTE_PLACEHOLDER
{
    int num_visible_attributes = block->data->attributes.num_nodes;
    BINARY_TREE_FOR (cstr_to_splx_node_list_map, &block->data->attributes, curr_attribute) {
        if (c_str_array_contains(note_internal_attributes, ARRAY_SIZE(note_internal_attributes), curr_attribute->key)) {
            num_visible_attributes--;
        }
    }

    if (num_visible_attributes > 0) {
        struct html_element_t *attributes = html_new_element (html, "div");
        html_element_attribute_set (html, attributes, "id", "__attributes");
        html_element_append_child (html, parent, attributes);
    }
}
#else
{
    attributes_html_append (html, block, parent, note_internal_attributes, ARRAY_SIZE(note_internal_attributes));
}
#endif

void attributed_block_html_append (struct psx_parser_ctx_t *ctx, struct html_t *html, struct psx_block_t *block, struct html_element_t *parent)
#ifdef ATTRIBUTE_PLACEHOLDER
{
    assert(block->data != NULL);

    struct html_element_t *new_dom_element = html_new_element (html, "p");
    html_element_attribute_set (html, new_dom_element, "data-block-attributes", "");
    html_element_append_child (html, parent, new_dom_element);
    block_content_parse_text (ctx, html, new_dom_element, str_data(&block->inline_content));
}
#else
{
    assert(block->data != NULL);

    struct html_element_t *padding = html_new_element (html, "div");
    html_element_append_child (html, parent, padding);
    html_element_attribute_set (html, padding, "style", "padding: 0.5em 0;");

    struct html_element_t *new_dom_element = html_new_element (html, "div");
    html_element_append_child (html, padding, new_dom_element);
    html_element_class_add (html, new_dom_element, "attributed-block");

    if (str_len(&block->inline_content) > 0) {
        struct html_element_t *inline_content = html_new_element (html, "p");
        html_element_append_child (html, new_dom_element, inline_content);
        block_content_parse_text (ctx, html, inline_content, str_data(&block->inline_content));
    }

    struct html_element_t *block_attributes = html_new_element (html, "div");
    html_element_append_child (html, new_dom_element, block_attributes);

    string_t style = {0};
    str_set (&style, "padding: var(--bleed); background-color: var(--secondary-bg-color);");
    if (str_len(&block->inline_content) > 0) {
        str_cat_c (&style,  "border-top: 1px solid #0000001c;");
    }
    html_element_attribute_set (html, block_attributes, "style", str_data(&style));

    attributes_html_append (html, block, block_attributes, block_internal_attributes, ARRAY_SIZE(block_internal_attributes));
}
#endif

void block_tree_to_html (struct psx_parser_ctx_t *ctx, struct html_t *html, struct psx_block_t *block, struct html_element_t *parent)
{
    string_t buff = {0};

    struct note_runtime_t *rt = rt_get();
    if (rt != NULL && rt->is_public && block->is_private) {
        // Do nothing

    } else if (block->type == BLOCK_TYPE_PARAGRAPH) {
        if (block->data == NULL) {
            char *block_content = str_data(&block->inline_content);

            char *tag = NULL;
            char *end = NULL;
            uint32_t tag_len = 0;
            psx_match_tag (block_content, block_content, "\\", true,
                &tag, &tag_len,
                NULL, NULL, NULL, NULL,
                &end,
                NULL);

            struct html_element_t *new_dom_element = NULL;
            if (end != NULL && *end == '\0' && strncmp(tag, "html", 4) == 0) {
                // Blocks consisting only of an html tags don't have a wrapper paragraph.
                new_dom_element = parent;

                // Add a line break to mark the start of the HTML. Only if it's
                // not within a list item.
                if (strcmp(str_data(&new_dom_element->tag), "li") != 0) {
                    html_element_append_no_escape_strn (html, new_dom_element, 1, "\n");
                }

            } else {
                new_dom_element = html_new_element (html, "p");
                html_element_append_child (html, parent, new_dom_element);
            }

            block_content_parse_text (ctx, html, new_dom_element, block_content);

        } else {
            attributed_block_html_append (ctx, html, block, parent);
        }

    } else if (block->type == BLOCK_TYPE_HEADING) {
        str_set_printf (&buff, "h%i", block->heading_number);
        struct html_element_t *new_dom_element = html_new_element (html, str_data(&buff));

        str_set (&buff, "");
        str_cat_section_id (&buff, str_data(&block->inline_content), true);
        html_element_attribute_set (html, new_dom_element, "id", str_data(&buff));

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
        // it will be horrible if we ever implement inplace editing because the
        // cursor of the user will be jumping so the block gets recentered (if
        // the block gets recentered at all, which wouldn't be the case if I
        // kept using this hack).
        //
        // For this reason we now just make all code blocks have the full width
        // of the note. We could think of using the hack back when enabling a
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
            // the first block which sould've been the title (heading).
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

        // TODO: There's an unhandled case where all list items of a list are
        // hidden, then the list itself should not be created.
        LINKED_LIST_FOR (struct psx_block_t*, sub_block, block->block_content) {
            block_tree_to_html(ctx, html, sub_block, new_dom_element);
        }

        html_element_append_child (html, parent, new_dom_element);

    } else if (block->type == BLOCK_TYPE_LIST_ITEM) {
        struct html_element_t *new_dom_element = html_new_element (html, "li");

        bool has_content = false;
        LINKED_LIST_FOR (struct psx_block_t*, sub_block, block->block_content) {
            if (rt == NULL || !rt->is_public || !sub_block->is_private) {
                block_tree_to_html(ctx, html, sub_block, new_dom_element);
                has_content = true;
            }
        }

        if (has_content) {
            html_element_append_child (html, parent, new_dom_element);
        }
    }

    str_free (&buff);
}

void psx_parse_block_attributes (struct psx_parser_state_t *ps,
                                 struct psx_block_t *block,
                                 char *node_id,
                                 struct splx_node_t *parent_entity)
{
    assert (block != NULL);

    char *backup_pos = scr_pos(PS_SCR);
    int backup_column_number = ps->scr.column_number;

    // We don't want to add data nodes for all blocks, it feels wasteful to have
    // one for each paragraph (?). Instead we only add a node if it has an ID,
    // or later when we are sure there was a data block after the paragraph.
    // That being said, we do force one data node for each note because we need
    // them to make references through links anyway.
    // :block_data_node_instantiation
    if (node_id != NULL) {
        block->data = splx_node_get_or_create(ps->ctx.sd, node_id, SPLX_NODE_TYPE_OBJECT);
    }

    scr_consume_spaces (PS_SCR);

    struct psx_token_t tok = ps_inline_next (ps);
    if (ps_match(ps, TOKEN_TYPE_DATA_TAG, NULL)) {
        assert(ps->ctx.sd != NULL);

        // :block_data_node_instantiation
        if (block->data == NULL) {
            block->data = splx_node_get_or_create(ps->ctx.sd, node_id, SPLX_NODE_TYPE_OBJECT);
        }

        if (tok.value.len > 0) {
            char *internal_backup_pos;
            int internal_backup_column_number;

            do {
                internal_backup_pos = scr_pos(PS_SCR);
                internal_backup_column_number = ps->scr.column_number;

                string_t s = {0};
                str_set_sstr(&s, &tok.value);
                struct splx_node_t *type = splx_node_get_or_create (ps->ctx.sd, str_data(&s), SPLX_NODE_TYPE_OBJECT);
                splx_node_attribute_append (
                    ps->ctx.sd,
                    block->data,
                    "a", type);
                str_free (&s);

                tok = ps_inline_next (ps);
            } while (ps_match(ps, TOKEN_TYPE_DATA_TAG, NULL));

            ps->scr.pos = internal_backup_pos;
            ps->scr.column_number = internal_backup_column_number;
        }

        string_t tsplx_data = {0};
        psx_cat_tag_content (ps, &tsplx_data, true);
        tsplx_parse_str_name_full(ps->ctx.sd, str_data(&tsplx_data), block->data, ps->ctx.error_msg);
        str_free (&tsplx_data);

        if (!splx_node_has_name(block->data)) {
            psx_set_virtual_id(block->data);

            if (parent_entity != NULL) {
                rt_link_entities (parent_entity, block->data, NULL, NULL);
            }
        }

        // At this point, we parsed some attributes, and we're setting the new
        // pos pointer in the state. If we had used peek before, the next call
        // to ps_next() will restore pos to the peeked value instead of the one
        // set by the tag content parseing. That's why we need to force the
        // state to mark the last token state as not peeked.
        // TODO: I should really get rid of the token peek API. Instead use a
        // state marker together with a restore mechanism.
        // :marker_over_peek
        ps->is_peek = false;

    } else {
        // :restore_pos
        ps->scr.pos = backup_pos;
        ps->scr.column_number = backup_column_number;
    }

    struct note_runtime_t *rt = rt_get();
    if (rt != NULL) {
        for (int i=0; i<rt->private_types_len; i++) {
            if (splx_node_attribute_contains(block->data, "a", rt->private_types[i])) {
                block->is_private = true;
            }
        }
    }
}

static inline
bool note_is_visible (struct note_t *note)
{
    struct note_runtime_t *rt = rt_get();
    return rt == NULL || !rt->is_public || !note->tree->is_private;
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
        psx_parse_block_attributes(ps, root_block, ps->ctx.id, NULL);
    }

    // TODO: Validate that the title has no inline tags.
}

bool parse_note_title (char *path, char *note_text, string_t *title, struct splx_data_t *sd, string_t *error_msg)
{
    bool success = true;

    STACK_ALLOCATE(struct psx_parser_state_t, ps);
    ps->ctx.path = path;
    ps->ctx.sd = sd;
    ps_init (ps, note_text);

    sstring_t title_s = {0};
    psx_parse_note_title (ps, &title_s, false);
    if (!ps->error) {
        str_set_sstr (title, &title_s);
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
        bool at_end_of_block = false;
        if (*tok_peek.value.s == '^' &&
            psx_match_tag(ps->scr.str, scr_pos(PS_SCR), "^",
                          true,
                          NULL, NULL,
                          NULL, NULL,
                          NULL, NULL,
                          NULL,
                          &at_end_of_block) &&
            at_end_of_block)
        {
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
    struct splx_node_t *note_entity = ps->block_stack[0]->data;

    while (!PS_SCR->is_eof && !ps->error) {
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

            psx_parse_block_attributes(ps, heading_block, NULL, note_entity);

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
                ps->scr.pos = tok.value.s;
                ps->scr.column_number = 0;
            }

            psx_parse_block_attributes(ps, new_paragraph, NULL, note_entity);

            // Paragraph blocks are never left in the stack. Maybe we should
            // just not push leaf blocks into the stack...
            // :pop_leaf_blocks
            ps->block_stack_len--;

        } else if (ps_match(ps, TOKEN_TYPE_CODE_HEADER, NULL)) {
            struct psx_block_t *new_code_block = psx_push_block (ps, psx_leaf_block_new(ps, BLOCK_TYPE_CODE, tok.margin, SSTRING("",0)));

            char *start = scr_pos(PS_SCR);
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
                psx_parse_block_attributes(ps, new_paragraph, NULL, note_entity);

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
        note->html = html_new (&note->pool, "div");
        html_element_attribute_set (note->html, note->html->root, "id", note->id);
        block_tree_to_html (ctx, note->html, note->tree, note->html->root);

        if (note->html == NULL) {
            note->error = true;
        }
    }
    // @AUTO_MACRO(END)


    string_t *html_out = str_pool(pool_out, "");
    if (!note->error) {
        str_cat_html (html_out, note->html, 2);
    }

    mem_pool_destroy (&_pool_l);

    return str_data(html_out);
}

void render_backlinks(struct note_runtime_t *rt, struct note_t *note)
{
    if (note->tree != NULL) {
        struct splx_node_list_t *backlinks = splx_node_get_attributes (note->tree->data, "backlink");
        int num_backlinks = 0;
        {
            LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, backlinks) {
                struct splx_node_t *node = curr_list_node->node;

                struct note_t *note = rt_get_note_by_id (str_data(splx_node_get_id(node)));
                if (note_is_visible(note)) {
                    num_backlinks++;
                }
            }
        }

        if (num_backlinks > 0) {
            struct html_t *html = note->html;

            struct html_element_t *title = html_new_element (html, "h4");
            html_element_append_child(html, html->root, title);
            html_element_append_cstr(html, title, "Backlinks");

            struct html_element_t *backlinks_element = html_new_element (html, "div");
            html_element_append_child(html, html->root, backlinks_element);
            html_element_class_add (html, backlinks_element, "backlinks");

            LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, backlinks) {
                struct splx_node_t *node = curr_list_node->node;

                struct note_t *note = rt_get_note_by_id (str_data(splx_node_get_id(node)));
                if (note_is_visible(note)) {
                    struct html_element_t *wrapper = html_new_element (html, "p");
                    html_element_append_child(html, backlinks_element, wrapper);

                    string_t *name = splx_node_get_name (node);
                    if (name != NULL) {
                        html_append_note_link (NULL, html, wrapper, str_data(&node->str), NULL, false, name);

                    } else {
                        // TODO: When does this happen?, is it for all linked
                        // virtual entities?.
                        //printf (ECMA_RED("error:") "failed to add backlink to node:\n");
                        //print_splx_node (node);
                    }
                }
            }
        }

    } else {
        // :empty_note_file
    }
}

void render_all_backlinks(struct note_runtime_t *rt)
{
    LINKED_LIST_FOR (struct note_t *, curr_note, rt->notes) {
        render_backlinks (rt, curr_note);
    }
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

        // We don't want this additional parsing of summarized notes to
        // interfere with the parent note. That's why we need empty_ctx and
        // empty_sd. Just passing ctx causes the parent note to get all the
        // attributes of the summarized ones.
        mem_pool_t pool = {0};
        STACK_ALLOCATE (struct splx_data_t, empty_sd);
        STACK_ALLOCATE (struct psx_parser_ctx_t, empty_ctx);
        empty_ctx->sd = empty_sd;
        struct psx_block_t *note_tree = parse_note_text (&pool, empty_ctx, str_data(&note->psplx));
        splx_destroy (empty_sd);

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

        rt_link_entities_by_id(ctx->note->id, note->id, NULL, NULL);

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
    struct psx_tag_t *tag = ps_parse_tag_full (&ps_inline->pool, ps_inline, &original_pos, true);
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

// Register internal custom callbacks
// :early_tag_api
#define PSX_EARLY_INTERNAL_TAG_TABLE \
PSX_INTERNAL_CUSTOM_TAG_ROW ("math",        math_tag_inline_handler) \
PSX_INTERNAL_CUSTOM_TAG_ROW ("Math",        math_tag_display_handler) \
PSX_INTERNAL_CUSTOM_TAG_ROW ("summary",     summary_tag_handler) \

void psx_populate_internal_cb_tree (struct psx_user_tag_cb_t *tree)
{
#define PSX_INTERNAL_CUSTOM_TAG_ROW(name,cb) psx_user_tag_cb_insert (tree, name, cb);
    PSX_EARLY_INTERNAL_TAG_TABLE
#undef PSX_INTERNAL_CUSTOM_TAG_ROW
}


/////////////////////
// Late Callback API
//
// This is a different user callback implementation where a custom tag can be
// defined, a placeholder HTML element will be emmitted at parse time, but the
// callback will be deferred until later when all internal PSPLX parsing is
// complete. This ensures for example that TSPLX contains all links. It also
// avoids the perills of being called during parsing execution and the
// possibility of messing up the parser's state.

PSX_LATE_USER_TAG_CB(orphan_list_tag_handler)
{
    str_set(&html_placeholder->tag, "ul");

    LINKED_LIST_FOR (struct note_t *, curr_note, rt->notes) {
        // TODO: Maybe use a map so we don't do an O(n) search in each iteration
        // to determine if a root note is a title note?.
        bool is_title_note = false;
        for (int i=0; i < rt->title_note_ids_len; i++) {
            if (strcmp (curr_note->id, rt->title_note_ids[i]) == 0) {
                is_title_note = true;
            }
        }

        if (curr_note->tree != NULL) {
            struct splx_node_list_t *backlinks = splx_node_get_attributes (curr_note->tree->data, "backlink");
            if (backlinks == NULL && !is_title_note && note_is_visible(curr_note)) {
                struct html_element_t *list_item = html_new_element (note->html, "li");
                html_element_append_child (note->html, html_placeholder, list_item);

                struct html_element_t *paragraph = html_new_element (note->html, "p");
                html_element_append_child (note->html, list_item, paragraph);

                struct note_t *target_note = rt_get_note_by_title (&curr_note->title);
                if (target_note != NULL) {
                    html_append_note_link (NULL, note->html,
                        paragraph,
                        target_note->id,
                        NULL,
                        false,
                        &curr_note->title);
                }
            }

        } else {
            // A note with empty content will hit this and would crash if we
            // don't check.
            // TODO: Make it so we don't need to check for this and notes that
            // are empty files are properly handled. Probably just by warning
            // about them in the console.
            // :empty_note_file
        }
    }
}

PSX_LATE_USER_TAG_CB(entity_list_tag_handler)
{
    str_set(&html_placeholder->tag, "ul");

    if (note->tree->data == NULL) {
        note->tree->data = splx_node_get_or_create(&rt->sd, note->id, SPLX_NODE_TYPE_OBJECT);
    }

    LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, rt->sd.entities->floating_values) {
        struct splx_node_t *entity = curr_list_node->node;

        struct splx_node_list_t *types = splx_node_get_attributes (entity, "a");
        LINKED_LIST_FOR (struct splx_node_list_t *, curr_type, types) {
            if (strcmp(str_data(&curr_type->node->str), str_data(&tag->content)) == 0 &&
                entity_is_visible(entity))
            {
                struct html_element_t *list_item = html_new_element (note->html, "li");
                html_element_append_child (note->html, html_placeholder, list_item);

                struct html_element_t *paragraph = html_new_element (note->html, "p");
                html_element_append_child (note->html, list_item, paragraph);

                string_t *name = splx_node_get_name(entity);
                html_append_link (NULL, note->html, paragraph, NULL, entity, NULL, &tag->content, name);

                if (name != NULL) {
                    // TODO: This isn't working right now. It causes a weird
                    // infinite loop in my own notes. I need to reproduce this
                    // in a smaller environment to be able to debug it. It
                    // breaks backlinks to database pages.
                    //psx_create_link(&rt->sd, note->tree->data, note->id, &tag->content, name);
                }
            }
        }
    }
}

PSX_LATE_USER_TAG_CB(virtual_list_tag_handler)
{
    str_set(&html_placeholder->tag, "ul");

    LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, rt->sd.entities->floating_values) {
        struct splx_node_t *entity = curr_list_node->node;
        struct splx_node_t *virtual_id = splx_node_get_attribute (entity, "t:virtual_id");

        if (virtual_id != NULL && entity_is_visible(entity)) {
            struct html_element_t *list_item = html_new_element (note->html, "li");
            html_element_append_child (note->html, html_placeholder, list_item);

            struct html_element_t *paragraph = html_new_element (note->html, "p");
            html_element_append_child (note->html, list_item, paragraph);

            // TODO: How can there be virtual entities without a name?...
            string_t name = {0};
            string_t *entity_name = splx_node_get_name (entity);
            if (entity_name == NULL) {
                str_cpy (&name, &virtual_id->str);
            } else {
                str_cpy (&name, entity_name);
            }

            html_append_entity_link (NULL, note->html,
                paragraph,
                str_data(&virtual_id->str),
                NULL,
                false,
                &name);

            str_free (&name);
        }
    }
}

// Register callbacks
// :late_tag_api
#define PSX_LATE_INTERNAL_TAG_TABLE \
PSX_INTERNAL_CUSTOM_TAG_ROW ("orphan_list", orphan_list_tag_handler) \
PSX_INTERNAL_CUSTOM_TAG_ROW ("entity_list", entity_list_tag_handler) \
PSX_INTERNAL_CUSTOM_TAG_ROW ("virtual_list", virtual_list_tag_handler) \

void psx_populate_internal_late_cb_tree (struct psx_late_user_tag_cb_t *tree)
{
#define PSX_INTERNAL_CUSTOM_TAG_ROW(name,cb) psx_late_user_tag_cb_insert (tree, name, cb);
    PSX_LATE_INTERNAL_TAG_TABLE
#undef PSX_INTERNAL_CUSTOM_TAG_ROW
}

