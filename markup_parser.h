/*
 * Copyright (C) 2021 Santiago León O.
 */

#include <limits.h>
#include "html_builder.h"
#include "lib/regexp.h"
#include "lib/regexp.c"

#include "js.c"

int psx_content_width = 588; // px

struct html_t* markup_to_html (mem_pool_t *pool, char *path, char *markup, char *id, int x, string_t *error_msg);

#if defined(MARKUP_PARSER_IMPL)

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

typedef struct {
    char *s;
    uint32_t len;
} sstring_t;
#define SSTRING(s,len) ((sstring_t){s,len})
#define SSTRING_C(s) SSTRING(s,strlen(s))

static inline
sstring_t sstr_set (char *s, uint32_t len)
{
    return SSTRING(s, len);
}

static inline
sstring_t sstr_trim (sstring_t str)
{
    while (is_space (str.s)) {
        str.s++;
        str.len--;
    }

    while (is_space (str.s + str.len - 1) || str.s[str.len - 1] == '\n') {
        str.len--;
    }

    return str;
}

static inline
void sstr_extend (sstring_t *str1, sstring_t *str2)
{
    assert (str2->s != NULL);

    if (str1->s == NULL) {
        str1->s = str2->s;
        str1->len = str2->len;

    } else {
        assert (str1->s + str1->len == str2->s);
        str1->len += str2->len;
    }
}

struct psx_token_t {
    sstring_t value;

    enum psx_token_type_t type;
    int margin;
    int content_start;
    int heading_number;
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

    struct psx_block_t *next;
};

struct psx_parser_state_t {
    mem_pool_t *result_pool;

    mem_pool_t pool;

    struct note_runtime_t *rt;

    char *path;

    // TODO: Actually use this free list, right now we only add stuff, but never
    // use it. Although, how useful is it?, I don't think a single parser
    // execution would make good use of this. Maybe block elements should be
    // allocated at the runtime level.
    struct psx_block_t *blocks_fl;

    bool error;
    string_t error_msg;

    bool is_eof;
    bool is_eol;
    bool is_peek;
    char *str;

    char *pos;
    char *pos_peek;

    struct psx_token_t token;
    struct psx_token_t token_peek;

    DYNAMIC_ARRAY_DEFINE (struct psx_block_t*, block_stack);
    DYNAMIC_ARRAY_DEFINE (struct psx_block_unit_t*, block_unit_stack);
};

#define ps_init(ps,str) ps_init_path(ps, ps->path,str)
void ps_init_path (struct psx_parser_state_t *ps, char *path, char *str)
{
    ps->str = str;
    ps->pos = str;

    ps->path = path;

    str_pool (&ps->pool, &ps->error_msg);
    DYNAMIC_ARRAY_INIT (&ps->pool, ps->block_stack, 100);
    DYNAMIC_ARRAY_INIT (&ps->pool, ps->block_unit_stack, 100);
}

void ps_destroy (struct psx_parser_state_t *ps)
{
    mem_pool_destroy (&ps->pool);
}

bool char_in_str (char c, char *str)
{
    while (*str != '\0') {
        if (*str == c) {
            return true;
        }

        str++;
    }

    return false;
}

static inline
char ps_curr_char (struct psx_parser_state_t *ps)
{
    return *(ps->pos);
}

bool pos_is_eof (struct psx_parser_state_t *ps)
{
    return ps_curr_char(ps) == '\0';
}

bool pos_is_operator (struct psx_parser_state_t *ps)
{
    return !pos_is_eof(ps) && char_in_str(ps_curr_char(ps), ",=[]{}\\|");
}

bool pos_is_digit (struct psx_parser_state_t *ps)
{
    return !pos_is_eof(ps) && char_in_str(ps_curr_char(ps), "1234567890");
}

bool pos_is_space (struct psx_parser_state_t *ps)
{
    return !pos_is_eof(ps) && is_space(ps->pos);
}

bool is_empty_line (sstring_t line)
{
    int count = 0;
    while (is_space(line.s + count)) {
        count++;
    }

    return line.s[count] == '\n' || count == line.len;
}

static inline
void ps_advance_char (struct psx_parser_state_t *ps)
{
    if (!pos_is_eof(ps)) {
        ps->pos++;

    } else {
        ps->is_eof = true;
    }
}

// NOTE: c_str MUST be null terminated!!
bool ps_match_str(struct psx_parser_state_t *ps, char *c_str)
{
    char *backup_pos = ps->pos;

    bool found = true;
    while (*c_str) {
        if (ps_curr_char(ps) != *c_str) {
            found = false;
            break;
        }

        ps_advance_char (ps);
        c_str++;
    }

    if (!found) ps->pos = backup_pos;

    return found;
}

static inline
bool ps_match_digits (struct psx_parser_state_t *ps)
{
    bool found = false;
    if (pos_is_digit(ps)) {
        found = true;
        while (pos_is_digit(ps)) {
            ps_advance_char (ps);
        }
    }
    return found;
}

static inline
void ps_consume_spaces (struct psx_parser_state_t *ps)
{
    while (pos_is_space(ps)) {
        ps_advance_char (ps);
    }
}

sstring_t advance_line(struct psx_parser_state_t *ps)
{
    char *start = ps->pos;
    while (!pos_is_eof(ps) && ps_curr_char(ps) != '\n') {
        ps_advance_char (ps);
    }

    // Advance over the \n character. We leave \n characters because they are
    // handled by the inline parser. Sometimes they are ignored (between URLs),
    // and sometimes they are replaced to space (multiline paragraphs).
    ps_advance_char (ps);

    return SSTRING(start, ps->pos - start);
}

BINARY_TREE_NEW (sstring_map, sstring_t, sstring_t, strncmp(a.s, b.s, MIN(a.len, b.len)))

struct sstring_ll_l {
    sstring_t v;
    struct sstring_ll_l *next;
};

struct psx_tag_parameters_t {
    struct sstring_ll_l *positional;
    struct sstring_ll_l *positional_end;

    struct sstring_map_tree_t named;
};

GCC_PRINTF_FORMAT(2, 3)
void psx_error (struct psx_parser_state_t *ps, char *format, ...)
{
    if (ps->path != NULL) {
         str_cat_printf (&ps->error_msg, ECMA_DEFAULT("%s:") ECMA_RED(" error: "), ps->path);
    } else {
         str_cat_printf (&ps->error_msg, ECMA_RED(" error: "));
    }

    PRINTF_INIT (format, size, args);
    size_t old_len = str_len(&ps->error_msg);
    str_maybe_grow (&ps->error_msg, str_len(&ps->error_msg) + size - 1, true);
    PRINTF_SET (str_data(&ps->error_msg) + old_len, size, format, args);

    str_cat_c (&ps->error_msg, "\n");
    ps->error = true;
}

GCC_PRINTF_FORMAT(2, 3)
void psx_warning (struct psx_parser_state_t *ps, char *format, ...)
{
    if (ps->path != NULL) {
         str_cat_printf (&ps->error_msg, ECMA_DEFAULT("%s:") ECMA_YELLOW(" warning: "), ps->path);
    } else {
         str_cat_printf (&ps->error_msg, ECMA_RED(" warning: "));
    }

    PRINTF_INIT (format, size, args);
    size_t old_len = str_len(&ps->error_msg);
    str_maybe_grow (&ps->error_msg, str_len(&ps->error_msg) + size - 1, true);
    PRINTF_SET (str_data(&ps->error_msg) + old_len, size, format, args);

    str_cat_c (&ps->error_msg, "\n");
}

void ps_parse_tag_parameters (struct psx_parser_state_t *ps, struct psx_tag_parameters_t *parameters)
{
    if (parameters != NULL) {
        parameters->named.pool = &ps->pool;
    }

    if (ps_curr_char(ps) == '[') {
        while (!ps->is_eof && ps_curr_char(ps) != ']') {
            ps_advance_char (ps);

            char *start = ps->pos;
            if (ps_curr_char(ps) != '\"') {
                // TODO: Allow quote strings for values. This is to allow
                // values containing "," or "]".
            }

            while (ps_curr_char(ps) != ',' &&
                   ps_curr_char(ps) != '=' &&
                   ps_curr_char(ps) != ']') {
                ps_advance_char (ps);
            }

            if (ps_curr_char(ps) == ',' || ps_curr_char(ps) == ']') {
                if (parameters != NULL) {
                    LINKED_LIST_APPEND_NEW (&ps->pool, struct sstring_ll_l, parameters->positional, new_param);
                    new_param->v = sstr_trim(SSTRING(start, ps->pos - start));
                }

            } else {
                sstring_t name = sstr_trim(SSTRING(start, ps->pos - start));

                // Advance over the '=' character
                ps_advance_char (ps);

                char *value_start = ps->pos;
                while (ps_curr_char(ps) != ',' &&
                       ps_curr_char(ps) != ']') {
                    ps_advance_char (ps);
                }

                sstring_t value = sstr_trim(SSTRING(value_start, ps->pos - value_start));

                if (parameters != NULL) {
                    sstring_map_tree_insert (&parameters->named, name, value);
                }
            }

            if (ps_curr_char(ps) != ']') {
                ps_advance_char (ps);
            }
        }

        ps_advance_char (ps);
    }
}

struct psx_token_t ps_next_peek(struct psx_parser_state_t *ps)
{
    struct psx_token_t _tok = {0};
    struct psx_token_t *tok = &_tok;

    char *backup_pos = ps->pos;
    ps_consume_spaces (ps);

    tok->type = TOKEN_TYPE_PARAGRAPH;
    char *non_space_pos = ps->pos;
    if (pos_is_eof(ps)) {
        ps->is_eof = true;
        ps->is_eol = true;
        tok->type = TOKEN_TYPE_END_OF_FILE;

    } else if (ps_match_str(ps, "- ") || ps_match_str(ps, "* ")) {
        ps_consume_spaces (ps);

        tok->type = TOKEN_TYPE_BULLET_LIST;
        tok->value = SSTRING(non_space_pos, 1);
        tok->margin = non_space_pos - backup_pos;
        tok->content_start = ps->pos - non_space_pos;

    } else if (ps_match_digits(ps)) {
        char *number_end = ps->pos;
        if (ps->pos - non_space_pos <= 9 && ps_match_str(ps, ". ")) {
            ps_consume_spaces (ps);

            tok->type = TOKEN_TYPE_NUMBERED_LIST;
            tok->value = SSTRING(non_space_pos, number_end - non_space_pos);
            tok->margin = non_space_pos - backup_pos;
            tok->content_start = ps->pos - non_space_pos;

        } else {
            // It wasn't a numbered list marker, restore position.
            ps->pos = backup_pos;
        }

    } else if (ps_match_str(ps, "#")) {
        int heading_number = 1;
        while (ps_curr_char(ps) == '#') {
            heading_number++;
            ps_advance_char (ps);
        }

        if (pos_is_space (ps) && heading_number <= 6) {
            ps_consume_spaces (ps);

            tok->value = sstr_trim(advance_line (ps));
            tok->is_eol = true;
            tok->margin = non_space_pos - backup_pos;
            tok->type = TOKEN_TYPE_TITLE;
            tok->heading_number = heading_number;
        }

    } else if (ps_match_str(ps, "\\code")) {
        ps_parse_tag_parameters(ps, NULL);

        if (!ps_match_str(ps, "{")) {
            tok->type = TOKEN_TYPE_CODE_HEADER;
            while (pos_is_space(ps) || ps_curr_char(ps) == '\n') {
                ps_advance_char (ps);
            }

        } else {
            // False alarm, this was an inline code block, restore position.
            ps->pos = backup_pos;
        }

    } else if (ps_match_str(ps, "|")) {
        tok->value = advance_line (ps);
        tok->is_eol = true;
        tok->type = TOKEN_TYPE_CODE_LINE;

    } else if (ps_match_str(ps, "\n")) {
        tok->type = TOKEN_TYPE_BLANK_LINE;
    }

    if (tok->type == TOKEN_TYPE_PARAGRAPH) {
        tok->value = sstr_trim(advance_line (ps));
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

struct psx_token_t ps_inline_next(struct psx_parser_state_t *ps)
{
    struct psx_token_t tok = {0};

    if (pos_is_eof(ps)) {
        ps->is_eof = true;

    } else if (ps_curr_char(ps) == '\\') {
        // :tag_parsing
        ps_advance_char (ps);

        char *start = ps->pos;
        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps)) {
            ps_advance_char (ps);
        }
        tok.value = SSTRING(start, ps->pos - start);
        tok.type = TOKEN_TYPE_TAG;

    } else if (pos_is_operator(ps)) {
        tok.type = TOKEN_TYPE_OPERATOR;
        tok.value = SSTRING(ps->pos, 1);
        ps_advance_char (ps);

    } else if (pos_is_space(ps) || ps_curr_char(ps) == '\n') {
        // This consumes consecutive spaces into a single one.
        while (pos_is_space(ps) || ps_curr_char(ps) == '\n') {
            ps_advance_char (ps);
        }

        tok.value = SSTRING(" ", 1);
        tok.type = TOKEN_TYPE_SPACE;

    } else {
        char *start = ps->pos;
        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps) && ps_curr_char(ps) != '\n') {
            ps_advance_char (ps);
        }

        tok.value = SSTRING(start, ps->pos - start);
        tok.type = TOKEN_TYPE_TEXT;
    }

    if (pos_is_eof(ps)) {
        ps->is_eof = true;
    }

    ps->token = tok;

    //console.log(psx_token_type_names[tok.type] + ": " + tok.value)
    return tok;
}

void ps_expect_inline(struct psx_parser_state_t *ps, enum psx_token_type_t type, char *cstr)
{
    ps_inline_next (ps);
    if (!ps_match(ps, type, cstr)) {
        if (ps->token.type != type) {
            if (cstr == NULL) {
                psx_error (ps, "Expected token of type %s, got '%.*s' of type %s.",
                           psx_token_type_names[type], ps->token.value.len, ps->token.value.s, psx_token_type_names[ps->token.type]);
            } else {
                psx_error (ps, "Expected token '%s' of type %s, got '%.*s' of type %s.",
                           cstr, psx_token_type_names[type], ps->token.value.len, ps->token.value.s, psx_token_type_names[ps->token.type]);
            }

        } else {
            // Value didn't match.
            psx_error (ps, "Expected '%s', got '%.*s'.", cstr, ps->token.value.len, ps->token.value.s);
        }
    }
}

struct psx_tag_t {
    struct psx_tag_parameters_t parameters;
    string_t content;
};

// TODO: Add balanced brace parsing here.
void psx_cat_tag_content (struct psx_parser_state_t *ps, string_t *str)
{
    struct psx_token_t tok = ps_inline_next(ps);
    if (ps_match(ps, TOKEN_TYPE_OPERATOR, "{")) {
        tok = ps_inline_next(ps);
        while (!ps->is_eof && !ps->error && !ps_match(ps, TOKEN_TYPE_OPERATOR, "}")) {
            strn_cat_c (str, tok.value.s, tok.value.len);
            tok = ps_inline_next(ps);
        }

    } else if (ps_match(ps, TOKEN_TYPE_OPERATOR, "|")) {
        char *start = ps->pos;
        while (!ps->is_eof && *(ps->pos) != '|') {
            ps_advance_char (ps);
        }

        if (ps->is_eof) {
            psx_error (ps, "Unexpected end of block. Incomplete definition of block content delimiter.");

        } else {
            char *number_end;
            size_t size = strtoul (start, &number_end, 10);
            if (ps->pos == number_end) {
                ps_advance_char (ps);
                strn_cat_c (str, ps->pos, size);
                ps->pos += size;

            } else {
                // TODO: Implement sentinel based tag content parsing
                // \code|SENTINEL|int a[2] = {1,2}SENTINEL
            }
        }
    }
}

struct psx_tag_t* ps_tag_new (struct psx_parser_state_t *ps)
{
    struct psx_tag_t *tag = mem_pool_push_struct (&ps->pool, struct psx_tag_t);
    *tag = ZERO_INIT (struct psx_tag_t);
    tag->parameters.named.pool = &ps->pool;
    str_pool (&ps->pool, &tag->content);

    return tag;
}

struct psx_tag_t* ps_parse_tag (struct psx_parser_state_t *ps)
{
    mem_pool_marker_t mrk = mem_pool_begin_temporary_memory (&ps->pool);

    struct psx_tag_t *tag = ps_tag_new (ps);

    ps_parse_tag_parameters(ps, &tag->parameters);
    psx_cat_tag_content(ps, &tag->content);

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

// This function returns the same string that was parsed as the current token.
// It's used as fallback, for example in the case of ',' and '#' characters in
// the middle of paragraphs, or unrecognized tag sequences.
//
// TODO: I feel that using this for operators isn't nice, we should probably
// have a stateful tokenizer that will consider ',' as part of a TEXT token
// sometimes and as operators other times. It's probably best to just have an
// attribute tokenizer/parser, then the inline parser only deals with tags.
void str_cat_literal_token (string_t *str, struct psx_parser_state_t *ps)
{
    if (ps_match (ps, TOKEN_TYPE_TAG, NULL)) {
        str_cat_c (str, "\\");
    }
    strn_cat_c (str, ps->token.value.s, ps->token.value.len);
}

void parse_balanced_brace_block(struct psx_parser_state_t *ps, string_t *str)
{
    assert (str != NULL);

    if (ps_curr_char(ps) == '{') {
        int brace_level = 1;
        ps_expect_inline (ps, TOKEN_TYPE_OPERATOR, "{");

        while (!ps->is_eof && brace_level != 0) {
            ps_inline_next (ps);
            if (ps_match (ps, TOKEN_TYPE_OPERATOR, "{")) {
                brace_level++;
            } else if (ps_match (ps, TOKEN_TYPE_OPERATOR, "}")) {
                brace_level--;
            }

            if (brace_level != 0) { // Avoid appending the closing }
                str_cat_literal_token(str, ps);
            }
        }
    }
}

struct psx_tag_t* ps_parse_tag_balanced_braces (struct psx_parser_state_t *ps)
{
    mem_pool_marker_t mrk = mem_pool_begin_temporary_memory (&ps->pool);

    struct psx_tag_t *tag = ps_tag_new (ps);

    ps_parse_tag_parameters(ps, &tag->parameters);
    parse_balanced_brace_block(ps, &tag->content);

    if (ps->error) {
        mem_pool_end_temporary_memory (mrk);
        tag = NULL;
    }

    return tag;
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

// This function parses the content of a block of text. The formatting is
// limited to tags that affect the formating inline. This parsing function
// will not add nested blocks like paragraphs, lists, code blocks etc.
//
// TODO: How do we handle the prescence of nested blocks here?, ignore them and
// print them or raise an error and stop parsing.
void block_content_parse_text (struct html_t *html, struct html_element_t *container, char *content)
{
    string_t buff = {0};
    struct psx_parser_state_t _ps = {0};
    struct psx_parser_state_t *ps = &_ps;
    ps_init (ps, content);

    DYNAMIC_ARRAY_APPEND (ps->block_unit_stack, psx_block_unit_new (&ps->pool, BLOCK_UNIT_TYPE_ROOT, container));
    while (!ps->is_eof && !ps->error) {
        struct psx_token_t tok = ps_inline_next (ps);

        if (ps_match(ps, TOKEN_TYPE_TEXT, NULL) || ps_match(ps, TOKEN_TYPE_SPACE, NULL)) {
            struct psx_block_unit_t *curr_unit = DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack);
            html_element_append_strn (html, curr_unit->html_element, tok.value.len, tok.value.s);

        } else if (ps_match(ps, TOKEN_TYPE_OPERATOR, "}") && DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack)->type != BLOCK_UNIT_TYPE_ROOT) {
            psx_block_unit_pop (ps);

        } else if (ps_match(ps, TOKEN_TYPE_TAG, "i") || ps_match(ps, TOKEN_TYPE_TAG, "b")) {
            struct psx_token_t tag = ps->token;
            ps_expect_inline (ps, TOKEN_TYPE_OPERATOR, "{");
            psx_push_block_unit_new (ps, html, tag.value);

        } else if (ps_match(ps, TOKEN_TYPE_TAG, "link")) {
             struct psx_tag_t *tag = ps_parse_tag (ps);

            // We parse the URL from the title starting at the end. I thinkg is
            // far less likely to have a non URL encoded > character in the
            // URL, than a user wanting to use > inside their title.
            //
            // TODO: Support another syntax for the rare case of a user that
            // wants a > character in a URL. Or make sure URLs are URL encoded
            // from the UI that will be used to edit this.
            ssize_t pos = tag->content.len - 1;
            while (pos > 0 && str_data(&tag->content)[pos] != '>') {
                pos--;
            }

            sstring_t url = SSTRING(str_data(&tag->content), str_len(&tag->content));
            sstring_t title = SSTRING(str_data(&tag->content), str_len(&tag->content));
            if (pos > 0 && str_data(&tag->content)[pos - 1] == '-') {
                pos--;
                url = sstr_trim(SSTRING(str_data(&tag->content) + pos + 2, str_len(&tag->content) - (pos + 2)));
                title = sstr_trim(SSTRING(str_data(&tag->content), pos));

                // TODO: It would be nice to heve this API...
                //sstr_trim(sstr_substr(pos+2));
                //sstr_trim(sstr_substr(0, pos));
            }

            struct html_element_t *link_element = html_new_element (html, "a");
            strn_set (&buff, url.s, url.len);
            html_element_attribute_set (html, link_element, "href", str_data(&buff));
            html_element_attribute_set (html, link_element, "target", "_blank");
            html_element_append_strn (html, link_element, title.len, title.s);

            psx_append_html_element(ps, html, link_element);

        } else if (ps_match(ps, TOKEN_TYPE_TAG, "youtube")) {
            struct psx_tag_t *tag = ps_parse_tag (ps);

            sstring_t video_id = {0};
            const char *error;
            Resub m;
            Reprog *regex = regcomp("^.*(youtu.be\\/|youtube(-nocookie)?.com\\/(v\\/|.*u\\/\\w\\/|embed\\/|.*v=))([\\w-]{11}).*", 0, &error);
            if (!regexec(regex, str_data(&tag->content), &m, 0)) {
                video_id = SSTRING((char*) m.sub[4].sp, m.sub[4].ep - m.sub[4].sp);
            }

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

        } else if (ps_match(ps, TOKEN_TYPE_TAG, "image")) {
            struct psx_tag_t *tag = ps_parse_tag (ps);

            struct html_element_t *img_element = html_new_element (html, "img");
            str_set_printf (&buff, "files/%s", str_data(&tag->content));
            html_element_attribute_set (html, img_element, "src", str_data(&buff));
            str_set_printf (&buff, "%d", psx_content_width);
            html_element_attribute_set (html, img_element, "width", str_data(&buff));
            psx_append_html_element(ps, html, img_element);

        } else if (ps_match(ps, TOKEN_TYPE_TAG, "code")) {
            ps_parse_tag_parameters(ps, NULL);
            // TODO: Actually do something with the passed language name

            string_t code_content = {0};
            parse_balanced_brace_block(ps, &code_content);
            if (str_len(&code_content) > 0) {
                struct html_element_t *code_element = html_new_element (html, "code");
                html_element_class_add(html, code_element, "code-inline");
                html_element_append_strn (html, code_element, str_len(&code_content), str_data(&code_content));

                psx_append_html_element(ps, html, code_element);
            }
            str_free (&code_content);

        } else if (ps_match(ps, TOKEN_TYPE_TAG, "note")) {
            struct psx_tag_t *tag = ps_parse_tag (ps);

            struct html_element_t *link_element = html_new_element (html, "a");
            str_set_printf (&buff, "return open_note_by_title('%.*s');", str_len(&tag->content), str_data(&tag->content));
            html_element_attribute_set (html, link_element, "onclick", str_data(&buff));
            html_element_attribute_set (html, link_element, "href", "#");
            html_element_class_add (html, link_element, "note-link");
            html_element_append_strn (html, link_element, str_len(&tag->content), str_data(&tag->content));

            psx_append_html_element(ps, html, link_element);

        } else if (ps_match(ps, TOKEN_TYPE_TAG, "html")) {
            // TODO: How can we support '}' characters here?. I don't think
            // assuming there will be balanced braces is an option here, as it
            // is in the \code tag. We most likely will need to implement user
            // defined termintating strings.
            struct psx_tag_t *tag = ps_parse_tag (ps);
            struct psx_block_unit_t *head_unit = ps->block_unit_stack[ps->block_unit_stack_len-1];
            html_element_append_strn (html, head_unit->html_element, str_len(&tag->content), str_data(&tag->content));

        } else {
            struct psx_block_unit_t *curr_unit = DYNAMIC_ARRAY_GET_LAST(ps->block_unit_stack);
            string_t buff = {0};
            str_cat_literal_token (&buff, ps);
            html_element_append_strn (html, curr_unit->html_element, str_len(&buff), str_data (&buff));
            str_free (&buff);
        }
    }

    ps_destroy (ps);
    str_free (&buff);
}

#define psx_block_new(ps) psx_block_new_cpy(ps,NULL)
struct psx_block_t* psx_block_new_cpy(struct psx_parser_state_t *ps, struct psx_block_t *original)
{
    struct psx_block_t *new_block = mem_pool_push_struct (ps->result_pool, struct psx_block_t);
    *new_block = ZERO_INIT (struct psx_block_t);
    str_pool (ps->result_pool, &new_block->inline_content);

    if (original != NULL) {
        new_block->type = original->type;
        new_block->margin = original->margin;
        str_cpy (&new_block->inline_content, &original->inline_content);
    }

    return new_block;
}


struct psx_block_t* psx_leaf_block_new(struct psx_parser_state_t *ps, enum psx_block_type_t type, int margin, sstring_t inline_content)
{
    struct psx_block_t *new_block = psx_block_new (ps);
    strn_set (&new_block->inline_content, inline_content.s, inline_content.len);
    new_block->type = type;
    new_block->margin = margin;

    return new_block;
}

struct psx_block_t* psx_container_block_new(struct psx_parser_state_t *ps, enum psx_block_type_t type, int margin)
{
    struct psx_block_t *new_block = psx_block_new (ps);
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
#define PSX_USER_TAG_CB(funcname) enum psx_user_tag_cb_status_t funcname(struct psx_parser_state_t *ps, struct psx_parser_state_t *ps_inline, struct psx_block_t **block, struct str_replacement_t *replacement)
typedef PSX_USER_TAG_CB(psx_user_tag_cb_t);

BINARY_TREE_NEW (psx_user_tag_cb, char*, psx_user_tag_cb_t*, strcmp(a, b));

void psx_populate_internal_cb_tree (struct psx_user_tag_cb_tree_t *tree);

// TODO: User callbacks will be modifying the tree. It's possible the user
// messes up and for example adds a cycle into the tree, we should detect such
// problem and avoid maybe later entering an infinite loop.
#define psx_block_tree_user_callbacks(ps,root) psx_block_tree_user_callbacks_full(ps,root,NULL)
void psx_block_tree_user_callbacks_full (struct psx_parser_state_t *ps, struct psx_block_t **root, struct psx_block_t **block_p)
{
    if (block_p == NULL) block_p = root;

    struct psx_block_t *block = *block_p;

    if (block->block_content != NULL) {
        struct psx_block_t **sub_block = &((*block_p)->block_content->next);
        while (*sub_block != NULL) {
            psx_block_tree_user_callbacks_full (ps, root, sub_block);
            sub_block = &(*sub_block)->next;
        }

    } else {
        struct psx_parser_state_t _ps_inline = {0};
        struct psx_parser_state_t *ps_inline = &_ps_inline;
        ps_init (ps_inline, str_data(&block->inline_content));

        struct psx_user_tag_cb_tree_t user_cb_tree = {0};
        user_cb_tree.pool = &ps_inline->pool;
        psx_populate_internal_cb_tree (&user_cb_tree);

        struct str_replacement_t *replacements = NULL;
        struct str_replacement_t *replacements_end = NULL;
        char *block_start = ps_inline->pos;
        while (!ps_inline->is_eof && !ps_inline->error) {
            char *tag_start = ps_inline->pos;
            struct psx_token_t tok = ps_inline_next (ps_inline);
            if (ps_match(ps_inline, TOKEN_TYPE_TAG, NULL)) {
                string_t tag_name = strn_new (tok.value.s, tok.value.len);

                psx_user_tag_cb_t* cb = psx_user_tag_cb_get (&user_cb_tree, str_data(&tag_name));
                if (cb != NULL) {
                    struct str_replacement_t tmp_replacement = {0};
                    enum psx_user_tag_cb_status_t status = cb (ps, ps_inline, block_p, &tmp_replacement);
                    tmp_replacement.start_idx = tag_start - block_start;
                    tmp_replacement.len = ps_inline->pos - tag_start;

                    if (ps_inline->error) {
                        psx_warning (ps, "failed parsing custom tag '%s'", str_data(&tag_name));
                        str_cat_indented_c (&ps->error_msg, str_data(&ps_inline->error_msg), 2);
                    }

                    if (str_len (&tmp_replacement.s) > 0) {
                        struct str_replacement_t *replacement = mem_pool_push_struct (&ps_inline->pool, struct str_replacement_t);
                        *replacement = tmp_replacement;
                        str_pool (&ps_inline->pool, &replacement->s);
                        LINKED_LIST_APPEND (replacements, replacement);
                    }

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

void block_tree_to_html (struct html_t *html, struct psx_block_t *block,  struct html_element_t *parent)
{
    string_t buff = {0};

    if (block->type == BLOCK_TYPE_PARAGRAPH) {
        struct html_element_t *new_dom_element = html_new_element (html, "p");
        html_element_append_child (html, parent, new_dom_element);
        block_content_parse_text (html, new_dom_element, str_data(&block->inline_content));

    } else if (block->type == BLOCK_TYPE_HEADING) {
        str_set_printf (&buff, "h%i", block->heading_number);
        struct html_element_t *new_dom_element = html_new_element (html, str_data(&buff));

        html_element_append_child (html, parent, new_dom_element);
        block_content_parse_text (html, new_dom_element, str_data(&block->inline_content));

    } else if (block->type == BLOCK_TYPE_CODE) {
        struct html_element_t *pre_element = html_new_element (html, "pre");
        html_element_append_child (html, parent, pre_element);

        struct html_element_t *code_element = html_new_element (html, "code");
        html_element_class_add(html, code_element, "code-block");
        // If we use line numbers, this should be the padding WRT the
        // column containing the numbers.
        // html_element_style_set(html, code_element, "padding-left", "0.25em");

        html_element_append_cstr (html, code_element, str_data(&block->inline_content));
        html_element_append_child (html, pre_element, code_element);

        // TODO: This hack should happen in the client side because it requires
        // feedback from the browser's renderer to get the computed width of
        // widgets.
        //
        // This is a hack. It's the only way I found to show tight, centered
        // code blocks if they are smaller than psx_content_width and at the same
        // time show horizontal scrolling if they are wider.
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
        LINKED_LIST_FOR (struct psx_block_t*, sub_block, block->block_content) {
            block_tree_to_html(html, sub_block, parent);
        }

    } else if (block->type == BLOCK_TYPE_LIST) {
        struct html_element_t *new_dom_element = html_new_element (html, "ul");
        if (block->list_type == TOKEN_TYPE_NUMBERED_LIST) {
            new_dom_element = html_new_element (html, "ol");
        }
        html_element_append_child (html, parent, new_dom_element);

        LINKED_LIST_FOR (struct psx_block_t*, sub_block, block->block_content) {
            block_tree_to_html(html, sub_block, new_dom_element);
        }

    } else if (block->type == BLOCK_TYPE_LIST_ITEM) {
        struct html_element_t *new_dom_element = html_new_element (html, "li");
        html_element_append_child (html, parent, new_dom_element);

        LINKED_LIST_FOR (struct psx_block_t*, sub_block, block->block_content) {
            block_tree_to_html(html, sub_block, new_dom_element);
        }
    }

    str_free (&buff);
}

void psx_parse_note_title (struct psx_parser_state_t *ps)
{
    struct psx_token_t tok = ps_next (ps);
    if (tok.type != TOKEN_TYPE_TITLE) {
        psx_error (ps, "note has no title");
    }

    // TODO: Validate that the title has no inline tags.
}

bool parse_note_title (char *path, char *note_text, string_t *title, string_t *error_msg)
{
    bool success = true;

    struct psx_parser_state_t _ps = {0};
    struct psx_parser_state_t *ps = &_ps;
    ps_init_path (ps, path, note_text);

    psx_parse_note_title (ps);
    if (!ps->error) {
        strn_set (title, ps->token.value.s, ps->token.value.len);
    } else {
        success = false;
    }

    ps_destroy (ps);

    return success;
}

struct psx_block_t* parse_note_text (mem_pool_t *pool, char *path, char *note_text, string_t *error_msg)
{
    mem_pool_marker_t mrk = mem_pool_begin_temporary_memory (pool);

    struct psx_parser_state_t _ps = {0};
    struct psx_parser_state_t *ps = &_ps;
    ps->result_pool = pool;
    ps_init_path (ps, path, note_text);

    struct psx_block_t *root_block = psx_container_block_new(ps, BLOCK_TYPE_ROOT, 0);
    DYNAMIC_ARRAY_APPEND (ps->block_stack, root_block);

    psx_parse_note_title (ps);
    if (!ps->error) {
        struct psx_block_t *title_block = psx_push_block (ps, psx_leaf_block_new(ps, BLOCK_TYPE_HEADING, ps->token.margin, ps->token.value));
        title_block->heading_number = 1;

        if (ps->token.heading_number != 1) {
            psx_warning (ps, "forcing note title to heading level 1, this note starts with different level");
        }
    }

    // Parse note's content
    while (!ps->is_eof && !ps->error) {
        int curr_block_idx = 0;
        struct psx_token_t tok = ps_next(ps);

        // Match the indentation of the received token.
        struct psx_block_t *next_stack_block = ps->block_stack[curr_block_idx+1];
        while (curr_block_idx+1 < ps->block_stack_len &&
               next_stack_block->type == BLOCK_TYPE_LIST &&
               (tok.margin > next_stack_block->margin ||
                   (tok.type == next_stack_block->list_type && tok.margin == next_stack_block->margin))) {
            curr_block_idx++;
            next_stack_block = ps->block_stack[curr_block_idx+1];
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

        } else if (ps_match(ps, TOKEN_TYPE_PARAGRAPH, NULL)) {
            struct psx_block_t *new_paragraph = psx_push_block (ps, psx_leaf_block_new(ps, BLOCK_TYPE_PARAGRAPH, tok.margin, tok.value));

            // Append all paragraph continuation lines. This ensures all paragraphs
            // found at the beginning of the iteration followed an empty line.
            struct psx_token_t tok_peek = ps_next_peek(ps);
            while (tok_peek.is_eol && tok_peek.type == TOKEN_TYPE_PARAGRAPH) {
                str_cat_c(&new_paragraph->inline_content, " ");
                strn_cat_c(&new_paragraph->inline_content, tok_peek.value.s, tok_peek.value.len);
                ps_next(ps);

                tok_peek = ps_next_peek(ps);
            }

        } else if (ps_match(ps, TOKEN_TYPE_CODE_HEADER, NULL)) {
            struct psx_block_t *new_code_block = psx_push_block (ps, psx_leaf_block_new(ps, BLOCK_TYPE_CODE, tok.margin, SSTRING("",0)));

            char *start = ps->pos;
            int min_leading_spaces = INT32_MAX;
            struct psx_token_t tok_peek = ps_next_peek(ps);
            while (tok_peek.is_eol && tok_peek.type == TOKEN_TYPE_CODE_LINE) {
                ps_next(ps);

                if (!is_empty_line(tok_peek.value)) {
                    int space_count = -1;
                    while (is_space (tok_peek.value.s + space_count + 1)) {
                        space_count++;
                    }

                    if (space_count >= 0) { // Ignore empty where we couldn't find any non-whitespace character.
                        min_leading_spaces = MIN (min_leading_spaces, space_count + 1);
                    }
                }

                tok_peek = ps_next_peek(ps);
            }
            ps_next(ps);

            // Concatenate lines to inline content while removing  the most
            // leading spaces we can remove. I call this automatic space
            // normalization.
            ps->pos = start;
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

        } else if (ps_match(ps, TOKEN_TYPE_BULLET_LIST, NULL) || ps_match(ps, TOKEN_TYPE_NUMBERED_LIST, NULL)) {
            struct psx_block_t *prnt = ps->block_stack[curr_block_idx];
            if (prnt->type != BLOCK_TYPE_LIST ||
                tok.margin >= prnt->margin + prnt->content_start) {
                struct psx_block_t *list_block = psx_push_block (ps, psx_container_block_new(ps, BLOCK_TYPE_LIST, tok.margin));
                list_block->content_start = tok.content_start;
                list_block->list_type = tok.type;
            }

            psx_push_block(ps, psx_container_block_new(ps, BLOCK_TYPE_LIST_ITEM, tok.margin));

            struct psx_token_t tok_peek = ps_next_peek(ps);
            if (tok_peek.is_eol && tok_peek.type == TOKEN_TYPE_PARAGRAPH) {
                ps_next(ps);

                // Use list's margin... maybe this will never be read?...
                struct psx_block_t *new_paragraph = psx_push_block(ps, psx_leaf_block_new(ps, BLOCK_TYPE_PARAGRAPH, tok.margin, tok_peek.value));

                // Append all paragraph continuation lines. This ensures all paragraphs
                // found at the beginning of the iteration followed an empty line.
                tok_peek = ps_next_peek(ps);
                while (tok_peek.is_eol && tok_peek.type == TOKEN_TYPE_PARAGRAPH) {
                    strn_cat_c(&new_paragraph->inline_content, tok_peek.value.s, tok_peek.value.len);
                    ps_next(ps);

                    tok_peek = ps_next_peek(ps);
                }
            }
        }
    }

    if (!ps->error) {
        psx_block_tree_user_callbacks (ps, &root_block);
    }

    // Copy error message even if !ps->error becaue there may be warnings.
    if (error_msg != NULL) {
        str_cat (error_msg, &ps->error_msg);
    }

    if (ps->error) {
        root_block = NULL;
        mem_pool_end_temporary_memory (mrk);
    }

    ps_destroy (ps);

    return root_block;
}

void str_cat_indented_debug_multiline (string_t *str, int curr_indent, char *c_str)
{
    char *pos = c_str;
    while (*pos != '\0') {
        char *start = pos;
        while (*pos != '\n' && *pos != '\0') pos++;

        str_cat_indented_printf (str, curr_indent, ECMA_S_YELLOW(0,"%.*s"), (int) (pos - start), start);
        if (*pos == '\n') {
            str_cat_printf (str, "↲");
            pos++;
        }

        if (*pos == '\0') {
            str_cat_printf (str, "∎\n");
        } else {
            str_cat_printf (str, "\n");
        }
    }

    //str_cat_printf (str, "'%s'\n", c_str);
}

void _str_cat_block_tree (string_t *str, struct psx_block_t *block, int indent, int curr_indent)
{
    str_cat_indented_printf (str, curr_indent, "type: %s\n", psx_block_type_names[block->type]);
    str_cat_indented_printf (str, curr_indent, "margin: %d\n", block->margin);
    str_cat_indented_printf (str, curr_indent, "heading_number: %d\n", block->heading_number);
    str_cat_indented_printf (str, curr_indent, "list_type: %s\n", psx_token_type_names[block->list_type]);
    str_cat_indented_printf (str, curr_indent, "content_start: %d\n", block->content_start);

    if (block->block_content == NULL) {
        str_cat_indented_printf (str, curr_indent, "inline_content:\n");
        str_cat_indented_debug_multiline (str, curr_indent, str_data(&block->inline_content));
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

struct html_t* markup_to_html (mem_pool_t *pool, char *path, char *markup, char *id, int x, string_t *error_msg)
{
    mem_pool_t pool_l = {0};

    struct psx_block_t *root_block = parse_note_text (&pool_l, path, markup, error_msg);
    //printf_block_tree (root_block, 4);

    struct html_t *html = NULL;
    if (root_block != NULL) {
        html = mem_pool_push_struct (pool, struct html_t);
        *html = ZERO_INIT (struct html_t);
        html->pool = pool;

        string_t buff = {0};

        struct html_element_t *root = html_new_element (html, "div");
        html->root = root;
        html_element_attribute_set (html, root, "id", id);
        html_element_class_add (html, root, "note");
        html_element_class_add (html, root, "expanded");

        // TODO: Make html_element_attribute_set() receive printf parameters
        // and format string.
        str_set_printf (&buff, "%ipx", x);
        html_element_attribute_set (html, root, "style", str_data(&buff));

        block_tree_to_html(html, root_block, root);
    }

    mem_pool_destroy (&pool_l);

    return html;
}

// This is here because it's trying to emulate how users would define custom
// tags. We use it for an internal tag just as test for the API.
PSX_USER_TAG_CB (summary_tag_handler)
{
    struct psx_tag_t *tag = ps_parse_tag (ps_inline);

    struct note_t *note = NULL;
    struct note_runtime_t *rt = rt_get ();
    if (rt != NULL) {
        note = title_to_note_get (&rt->notes_by_title, &tag->content);
    } else {
        // TODO: There is no runtime in the parser state. Is there any reason we
        // would use the parser without a runtime?. If that's the case, then tag
        // evaluations like this one, that reffer to other notes won't work.
    }

    if (note != NULL) {
        struct psx_block_t *result = NULL;
        struct psx_block_t *result_end = NULL;

        mem_pool_t pool = {0};
        struct psx_block_t *note_tree = parse_note_text (&pool, NULL, str_data(&note->psplx), NULL);

        struct psx_block_t *title = note_tree->block_content;
        struct psx_block_t *new_title = psx_block_new_cpy (ps, title);
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
            struct psx_block_t *new_summary = psx_block_new_cpy (ps, after_title);
            LINKED_LIST_APPEND (result, new_summary);
        }

        mem_pool_destroy (&pool);

        // Replace the original block in the tree with the new ones.
        struct psx_block_t *tmp = *block;
        result_end->next = (*block)->next;
        *block = result;
        LINKED_LIST_PUSH (ps->blocks_fl, tmp);

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
    string_t res = {0};
    struct psx_tag_t *tag = ps_parse_tag_balanced_braces (ps_inline);

    str_cat_math_strn (&res, is_display_mode, str_len(&tag->content), str_data(&tag->content));

    str_set_printf (&replacement->s, "\\html|%d|%s", str_len(&res), str_data (&res));
    str_free (&res);

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
#define PSX_INTERNAL_CUSTOM_TAG_TABLE \
PSX_INTERNAL_CUSTOM_TAG_ROW ("math", math_tag_inline_handler) \
PSX_INTERNAL_CUSTOM_TAG_ROW ("Math", math_tag_display_handler) \
PSX_INTERNAL_CUSTOM_TAG_ROW ("summary", summary_tag_handler) \

void psx_populate_internal_cb_tree (struct psx_user_tag_cb_tree_t *tree)
{
#define PSX_INTERNAL_CUSTOM_TAG_ROW(name,cb) psx_user_tag_cb_tree_insert (tree, name, cb);
    PSX_INTERNAL_CUSTOM_TAG_TABLE
#undef PSX_INTERNAL_CUSTOM_TAG_ROW
}


#endif
