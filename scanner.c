/*
 * Copyright (C) 2022 Santiago León O.
 */

struct scanner_t {
    char *str;
    char *pos;

    int line_number;
    int column_number;

    bool is_eof;
};

static inline
char* scr_pos (struct scanner_t *scr)
{
    return scr->pos;
}

static inline
char scr_curr_char (struct scanner_t *scr)
{
    return *scr_pos(scr);
}

static inline
char scr_prev_char (struct scanner_t *scr)
{
    assert (scr->str < scr->pos);
    return *(scr->pos-1);
}

static inline
char scr_next_char (struct scanner_t *scr)
{
    if (scr->is_eof) return '\0';
    return *(scr->pos+1);
}

static inline
bool scr_is_start (struct scanner_t *scr)
{
    return scr->pos == scr->str;
}

static inline
bool scr_is_digit (struct scanner_t *scr)
{
    return !scr->is_eof && char_in_str(scr_curr_char(scr), "1234567890");
}

static inline
bool scr_is_space (struct scanner_t *scr)
{
    return !scr->is_eof && is_space(scr->pos);
}

void scr_advance_char (struct scanner_t *scr)
{
    if (scr_curr_char(scr) != '\0') {
        scr->pos++;

        scr->column_number++;

        if (*(scr->pos) == '\n') {
            scr->line_number++;
            scr->column_number = 0;
        }

    } else {
        // This is not the only place where is_eof is set. I've had to also do
        // this in tokenizers. Be careful when assuming EOF is only set here.
        // :eof_set
        scr->is_eof = true;
    }
}

// NOTE: c_str MUST be null terminated!!
bool scr_match_str(struct scanner_t *scr, char *c_str)
{
    char *backup_pos = scr->pos;

    bool found = true;
    while (*c_str) {
        if (scr_curr_char(scr) != *c_str) {
            found = false;
            break;
        }

        scr_advance_char (scr);
        c_str++;
    }

    if (!found) scr->pos = backup_pos;

    return found;
}

static inline
bool scr_match_digits (struct scanner_t *scr)
{
    bool found = false;
    if (scr_is_digit(scr)) {
        found = true;
        while (scr_is_digit(scr)) {
            scr_advance_char (scr);
        }
    }
    return found;
}

bool scr_match_until_unescaped_operator (struct scanner_t *scr, char operator, char **str, uint32_t *str_len)
{
    char *res = NULL;
    uint32_t res_len = 0;

    struct scanner_t scr_bak = *scr;
    char *start = scr->pos;
    while (!scr->is_eof && scr_curr_char(scr) != operator) {
        scr_advance_char (scr);

        if (scr_curr_char(scr) == operator && scr_prev_char(scr) == '\\') {
            scr_advance_char (scr);
        }
    }

    if (scr_curr_char(scr) == operator) {
        res = start;
        res_len = scr->pos - start;
        scr_advance_char(scr);
    } else {
        *scr = scr_bak;
    }

    if (str != NULL) *str = res;
    if (str_len != NULL) *str_len = res_len;

    return res != NULL;
}

// TODO: Some places do this but are not calling into this function. When we
// have a macro based scanner, make them use that.
// :scr_match_double_quoted_string
bool scr_match_double_quoted_string (struct scanner_t *scr)
{
    bool found = false;

    if (scr_curr_char(scr) == '"' && (scr_is_start(scr) || scr_prev_char(scr) != '\\')) {
        scr_advance_char (scr);

        found = scr_match_until_unescaped_operator (scr, '"', NULL, NULL);

        scr_advance_char (scr);
    }

    return found;
}

// NOTE: The resulting sstring_t does include the first found \n character. This
// has been relevan because this character may triggers different behaviors
// sometimes they are ignored, sometimes they are replaced to space. If they are
// not necessary, it's trivial to just subtract 1 from the returned length.
sstring_t scr_advance_line(struct scanner_t *scr)
{
    char *start = scr->pos;
    while (!scr->is_eof && scr_curr_char(scr) != '\n') {
        scr_advance_char (scr);
    }

    scr_advance_char (scr);

    return SSTRING(start, scr->pos - start);
}

static inline
void scr_consume_spaces (struct scanner_t *scr)
{
    while (scr_is_space(scr)) {
        scr_advance_char (scr);
    }
}
