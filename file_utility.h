/*
 * Copyright (C) 2022 Santiago León O.
 */

#include "lib/regexp.h"

char *identifier_r = "^[23456789CFGHJMPQRVWX]{8,}$";
char *canonical_fname_r = "^(?:([0-9]+)_)?(?:(.+?)_)?([23456789CFGHJMPQRVWX]{8,})((?:\\.[0-9]+)+)?(?: (.+?))?(?:\\.([^.]+))?$";

struct vlt_file_t {
    // It's always relative to base_dir in file_vault_t
    string_t path;

    uint64_t idx; // Maybe 32 bits are enough?...
    string_t prefix;
    uint64_t id;
    DYNAMIC_ARRAY_DEFINE(int, location);
    string_t name;
    string_t extension;

    struct vlt_file_t *next;
};
BINARY_TREE_NEW (id_to_vlt_file, uint64_t, struct vlt_file_t*, (a==b) ? 0 : (a<b ? -1 : 1));

int vlt_file_compare (struct vlt_file_t *a, struct vlt_file_t *b)
{
    int result = 0;

    if (a->idx != b->idx) {
        if (a->idx < b->idx) {
            result = -1;
        } else {
            result = 1;
        }

    } else if (a->id != b->id) {
        if (a->id < b->id) {
            result = -1;
        } else {
            result = 1;
        }

    } else {
        if (a->location_len == b->location_len) {
            for (int i=0; i<a->location_len; i++) {
                if (a->location[i] < b->location[i]) {
                    result = -1;
                } else if (a->location[i] > b->location[i]) {
                    result = 1;
                }
            }

        } else {
            printf (ECMA_RED("error:") " could not compare files because locations differ in size.\n");
        }
    }

    if (result == 0) {
        if (str_len(&a->name) < str_len(&b->name)) {
            result = -1;

        } else {
            result = strncmp (str_data(&a->name), str_data(&b->name), str_len(&a->name));
        }
    }

    return result;
}
templ_sort_ll(vlt_file_sort,struct vlt_file_t, vlt_file_compare(a,b) < 0);

// TODO: A vault should be composed of file_repo_t which represent a single
// directory whose file structure is controlled by weaver and derived from
// metadata associated with the file IDs.
struct file_vault_t {
    mem_pool_t pool;

    // Must always be an absolute path
    char *base_dir;

    struct id_to_vlt_file_t files;
};

// NOTE: Assign X the 0 value (opposite to what Plus Codes do). Allows appending
// X characters at the beginning without changing the ID's value. This makes it
// possible to always use an ID as an identifier in programming languages where
// the first digit can't be a number (C, Python, Java, JavaScript, etc.).
char id_digit_lookup['X' - '0' + 1] = {
    [2] = 19,
    [3] = 18,
    [4] = 17,
    [5] = 16,
    [6] = 15,
    [7] = 14,
    [8] = 13,
    [9] = 12,
    ['C' - '0'] = 11,
    ['F' - '0'] = 10,
    ['G' - '0'] = 9,
    ['H' - '0'] = 8,
    ['J' - '0'] = 7,
    ['M' - '0'] = 6,
    ['P' - '0'] = 5,
    ['Q' - '0'] = 4,
    ['R' - '0'] = 3,
    ['V' - '0'] = 2,
    ['W' - '0'] = 1,
    ['X' - '0'] = 0,
};

uint64_t canonical_id_parse (char *s, size_t len)
{
    uint64_t id = 0;

    if (len == 0) {
        for (; s[len] != '\0'; len++);
    }

    uint64_t pow = 1;
    for (int i=len-1; i>=0; i--) {
        id += ((uint64_t)id_digit_lookup[s[i] - '0']) * pow;
        pow *= 20;
    }

    return id;
}

char id_value_to_digit['X' - '0' + 1] = {'X','W','V','R','Q','P','M','J','H','G','F','C','9','8','7','6','5','4','3','2'};

#define ID_DEFAULT_LEN 10

void str_cat_id (string_t *s, uint64_t id)
{
    string_t buff = {0};
    while (id > 0) {
        str_cat_char(&buff, id_value_to_digit[id%20], 1);
        id /= 20;
    }

    char *reverse_id = str_data(&buff);
    str_cat_char(s, 'X', MAX(0, ID_DEFAULT_LEN - str_len(&buff)));
    for (int i=str_len(&buff)-1; i>=0; i--) {
        str_cat_char(s, reverse_id[i], 1);
    }
}

// NOTE: Use srand() ONCE before using this.
void strn_cat_id_random (string_t *s, int len)
{
    string_t tmp = {0};

    // Uses an approximation of log_2(20^len)
    //   log_2(20^x) = x*log_2(20) ~ x*4.5
    str_cat_id (&tmp, rand_bits((len*9) >> 1));
    strn_cat_c (s, str_data(&tmp), len);

    str_free (&tmp);
}

// NOTE: Use srand() ONCE before using this.
static inline
void str_cat_id_random(string_t *s)
{
    strn_cat_id_random(s, ID_DEFAULT_LEN);
}


static inline
sstring_t r_group (Resub m, int i)
{
    return SSTRING((char*)m.sub[i].sp, m.sub[i].ep - m.sub[i].sp);
}

struct vlt_file_t* canonical_fname_parse (mem_pool_t *pool, char *s, size_t len)
{
    struct vlt_file_t *result = NULL;

    const char *error;
    Resub m;
    Reprog *regex = regcomp(canonical_fname_r, 0, &error);

    if (!regexec(regex, s, &m, 0)) {
        result = mem_pool_push_struct (pool, struct vlt_file_t);
        *result = ZERO_INIT(struct vlt_file_t);

        str_pool (pool, &result->path);
        str_pool (pool, &result->prefix);
        str_pool (pool, &result->name);
        str_pool (pool, &result->extension);
        DYNAMIC_ARRAY_INIT(pool, result->location, 0);

        sstring_t group = r_group(m, 1);
        if (group.len > 0) {
            result->idx = strtoul (group.s, NULL, 10);
        }

        group = r_group(m, 2);
        strn_set(&result->prefix, group.s, group.len);

        group = r_group(m, 3);
        result->id = canonical_id_parse (group.s, group.len);

        group = r_group(m, 4);
        if (group.len > 0) {
            char *c = group.s;
            if (*c == '.') c++;

            while ((c - group.s) < group.len) {
                DYNAMIC_ARRAY_APPEND (result->location, strtoul (c, &c, 10));
                c++; // Skip '.' character
            }
        }

        group = r_group(m, 5);
        strn_set(&result->name, group.s, group.len);

        group = r_group(m, 6);
        strn_set(&result->extension, group.s, group.len);
    }

    return result;
}

ITERATE_DIR_CB (maybe_process_canonical_file)
{
    struct file_vault_t *vlt = (struct file_vault_t*)data;
    if (!is_dir) {
        char *basename = path_basename (fname);
        struct vlt_file_t *file = canonical_fname_parse (&vlt->pool, basename, 0);
        if (file != NULL) {
            str_set (&file->path, fname + strlen(vlt->base_dir));

            struct id_to_vlt_file_node_t *node = NULL;
            if (id_to_vlt_file_lookup (&vlt->files, file->id, &node)) {
                LINKED_LIST_PUSH(node->value, file);
            } else {
                id_to_vlt_file_insert (&vlt->files, file->id, file);
            }
        }
    }
}

void vlt_init (struct file_vault_t *vlt)
{
    mem_pool_variable_ensure((&vlt->files));

    iterate_dir (vlt->base_dir, maybe_process_canonical_file, vlt);
}

bool is_canonical_id (char *s)
{
    const char *error;
    Resub m;
    Reprog *regex = regcomp(identifier_r, 0, &error);
    int result = !regexec(regex, s, &m, 0);

    return result;
}

#define file_id_lookup(vlt,id) file_id_lookup_full(vlt,id,true)
struct vlt_file_t* file_id_lookup_full (struct file_vault_t *vlt, uint64_t id, bool sorted)
{
    struct vlt_file_t *file = NULL;

    struct id_to_vlt_file_node_t *node = NULL;
    if (id_to_vlt_file_lookup (&vlt->files, id, &node)) {
        file = node->value;
    }

    if (sorted) {
        vlt_file_sort (&file, -1);
    }

    return file;
}
