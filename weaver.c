/*
 * Copyright (C) 2021 Santiago León O.
 */

#include "common.h"
#include "binary_tree.c"
#include "test_logger.c"
#include "cli_parser.c"

#define MARKUP_PARSER_IMPL
#include "note_runtime.h"
#include "markup_parser.h"

#include "note_runtime.c"

//////////////////////////////////////
// Platform functions for testing

ITERATE_DIR_CB(test_dir_iter)
{
    struct note_runtime_t *rt = (struct note_runtime_t*)data;

    if (!is_dir) {
        size_t basename_len = 0;
        char *p = fname + strlen (fname) - 1;
        while (p != fname && *p != '/') {
            basename_len++;
            p--;
        }
        p++; // advance '/'

        LINKED_LIST_PUSH_NEW (&rt->pool, struct note_t, rt->notes, new_note);
        new_note->id = pom_strndup (&rt->pool, p, basename_len);
        id_to_note_tree_insert (&rt->notes_by_id, new_note->id, new_note);

        str_pool (&rt->pool, &new_note->error_msg);
        str_set_pooled (&rt->pool, &new_note->path, fname);

        size_t source_len;
        char *source = full_file_read (NULL, fname, &source_len);
        str_pool (&rt->pool, &new_note->psplx);
        strn_set (&new_note->psplx, source, source_len);
        free (source);

        str_pool (&rt->pool, &new_note->title);
        if (!parse_note_title (fname, str_data(&new_note->psplx), &new_note->title, &new_note->error_msg)) {
            new_note->error = true;
            printf ("%s", str_data(&new_note->error_msg));

        } else {
            title_to_note_tree_insert (&rt->notes_by_title, &new_note->title, new_note);
        }

        str_pool (&rt->pool, &new_note->html);
    }
}

void rt_init_from_dir (struct note_runtime_t *rt, char *path)
{
    rt->notes_by_id.pool = &rt->pool;
    rt->notes_by_title.pool = &rt->pool;

    iterate_dir (path, test_dir_iter, rt);
}
//
//////////////////////////////////////

char* full_file_read_no_trailing_newline (mem_pool_t *pool, const char *path, uint64_t *len)
{
    char *data = full_file_read (pool, path, len);

    char *p = data + *len;
    if (*p == '\0') p--;
    if (*p == '\n') *p = '\0';

    return data;
}

struct config_t {
    string_t source_path;
    string_t target_path;
};

int main(int argc, char** argv)
{
    int retval = 0;
    bool success = true;

    __g_note_runtime = ZERO_INIT (struct note_runtime_t);
    struct note_runtime_t *rt = &__g_note_runtime;

    struct config_t _cfg = {0};
    struct config_t *cfg = &_cfg;

    str_set_path (&cfg->source_path, "~/.weaver/notes/");
    str_set_path (&cfg->target_path, "~/.cache/weaver/www/notes/");

    // TODO: Read these paths from some configuration file and from command line
    // parameters.
    if (!dir_exists (str_data(&cfg->source_path))) {
        success = false;
        printf (ECMA_RED("error: ") "source directory does not exist\n");
    }

    if (!ensure_path_exists (str_data(&cfg->target_path))) {
        success = false;
        printf (ECMA_RED("error: ") "target directory could not be created\n");
    }

    if (success) {
        rt_init_from_dir (rt, str_data(&cfg->source_path));

        if (get_cli_bool_opt ("--generate-static", argv, argc)) {
#ifdef NO_JS
            printf (ECMA_YELLOW("warning: ") "generating static site without javascript engine\n\n");
#endif

            string_t html_path = str_new (str_data(&cfg->target_path));
            size_t end = str_len (&cfg->target_path);
            LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
                string_t error_msg = {0};
                str_put_printf (&html_path, end, "%s", curr_note->id);

                bool success = rt_parse_to_html (rt, curr_note, &error_msg);
                if (!success) {
                    printf ("%s", str_data(&error_msg));
                }

                full_file_write (str_data(&curr_note->html), str_len(&curr_note->html), str_data(&html_path));
                str_free (&error_msg);
            }
        }
    }

    mem_pool_destroy (&rt->pool);

    str_free (&cfg->source_path);
    str_free (&cfg->target_path);

    js_destroy ();
    return retval;
}
