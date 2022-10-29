/*
 * Copyright (C) 2021 Santiago León O.
 */

#include "common.h"
#include "binary_tree.c"
#include "test_logger.c"
#include "cli_parser.c"
#include "automacros.h"

#include "file_utility.h"
#include "block.h"
#include "note_runtime.h"

#include "psplx_parser.c"
#include "note_runtime.c"

//////////////////////////////////////
// Platform functions for testing

void rt_init_push_file (struct note_runtime_t *rt, char *fname)
{
    size_t basename_len = 0;
    char *p = fname + strlen (fname) - 1;
    while (p != fname && *p != '/') {
        basename_len++;
        p--;
    }
    p++; // advance '/'

    struct note_t *new_note = rt_new_note (rt, p, basename_len);

    str_pool (&rt->pool, &new_note->error_msg);
    str_set_pooled (&rt->pool, &new_note->path, fname);

    size_t source_len;
    char *source = full_file_read (NULL, fname, &source_len);
    str_pool (&rt->pool, &new_note->psplx);
    strn_set (&new_note->psplx, source, source_len);
    free (source);

    str_pool (&rt->pool, &new_note->title);
    if (!parse_note_title (fname, str_data(&new_note->psplx), &new_note->title, &rt->sd, &new_note->error_msg)) {
        new_note->error = true;

    } else {
        title_to_note_insert (&rt->notes_by_title, &new_note->title, new_note);
    }

    str_pool (&rt->pool, &new_note->html);
}

ITERATE_DIR_CB(test_dir_iter)
{
    struct note_runtime_t *rt = (struct note_runtime_t*)data;

    if (!is_dir) {
        rt_init_push_file (rt, fname);
    }
}

void rt_init_push_dir (struct note_runtime_t *rt, char *path)
{
    iterate_dir (path, test_dir_iter, rt);
}

void rt_init (struct note_runtime_t *rt, struct splx_data_t *config)
{
    rt->notes_by_id.pool = &rt->pool;
    rt->notes_by_title.pool = &rt->pool;

    splx_get_value_cstr_arr (config, &rt->pool, CFG_TITLE_NOTES, &rt->title_note_ids, &rt->title_note_ids_len);
}

//
//////////////////////////////////////

struct config_t {
    string_t config_path;
    string_t source_path;
    string_t files_path;
    string_t target_path;
};

void cfg_destroy (struct config_t *cfg)
{
    str_free (&cfg->config_path);
    str_free (&cfg->source_path);
    str_free (&cfg->target_path);
}

#define APP_HOME "~/.weaver"

enum cli_command_t {
    CLI_COMMAND_GENERATE,
    CLI_COMMAND_LOOKUP,
    CLI_COMMAND_NONE
};

enum cli_output_type_t {
    CLI_OUTPUT_TYPE_STATIC_SITE,
    CLI_OUTPUT_TYPE_HTML,
    CLI_OUTPUT_TYPE_CSV,
    CLI_OUTPUT_TYPE_DEFAULT
};

int main(int argc, char** argv)
{
    int retval = 0;
    bool success = true;

    __g_note_runtime = ZERO_INIT (struct note_runtime_t);
    struct note_runtime_t *rt = &__g_note_runtime;

    STACK_ALLOCATE (struct cli_ctx_t, cli_ctx);

    int has_js = 1;
#ifdef NO_JS
    has_js = 0;
#endif
    if (get_cli_bool_opt_ctx (cli_ctx, "--has-js", argv, argc)) return has_js;

    // TODO: If configuration file doesn't exist, copy the default one from the
    // resources. The resources should be embedded in the app's binary.

    STACK_ALLOCATE (struct config_t, cfg);

    str_set_path (&cfg->source_path, APP_HOME "/notes/");
    str_set_path (&cfg->files_path, APP_HOME "/files/");
    str_set_path (&cfg->target_path, "~/.cache/weaver/www/notes/");
    str_set_path (&cfg->config_path, APP_HOME "/config.tsplx");

    struct splx_data_t config = {0};
    tsplx_parse_name (&config, str_data(&cfg->config_path));
    rt_init (rt, &config);

    rt->vlt.base_dir = str_data(&cfg->files_path);
    vlt_init (&rt->vlt);

    // TODO: Read these paths from some configuration file and from command line
    // parameters.
    if (!dir_exists (str_data(&cfg->source_path))) {
        success = false;
        printf (ECMA_RED("error: ") "source directory does not exist\n");
    }

    if (!ensure_path_exists (str_data(&cfg->files_path))) {
        success = false;
        printf (ECMA_RED("error: ") "files directory could not be created\n");
    }

    if (!ensure_path_exists (str_data(&cfg->target_path))) {
        success = false;
        printf (ECMA_RED("error: ") "target directory could not be created\n");
    }

    string_t error_msg = {0};

    enum cli_command_t command = CLI_COMMAND_NONE;
    if (strcmp (argv[1], "generate") == 0) {
        command = CLI_COMMAND_GENERATE;

    } else if (strcmp (argv[1], "lookup") == 0) {
        command = CLI_COMMAND_LOOKUP;
    }

    enum cli_output_type_t output_type = CLI_OUTPUT_TYPE_DEFAULT;
    if (get_cli_bool_opt_ctx (cli_ctx, "--static", argv, argc)) {
        output_type = CLI_OUTPUT_TYPE_STATIC_SITE;
    }
    if (get_cli_bool_opt_ctx (cli_ctx, "--html", argv, argc)) {
        output_type = CLI_OUTPUT_TYPE_HTML;
    }
    if (get_cli_bool_opt_ctx (cli_ctx, "--csv", argv, argc)) {
        output_type = CLI_OUTPUT_TYPE_CSV;
    }

    char *output_dir = get_cli_arg_opt_ctx (cli_ctx, "--output-dir", argv, argc);

    char *no_opt = get_cli_no_opt_arg_full (cli_ctx, argv, argc, command == CLI_COMMAND_NONE ? 1 : 2);

    // COLLECT INPUT DATA
    //
    // Final CLI UI should support this:
    //
    //  - File paths (no_opt_args from CLI)
    //     - Directories are expanded recursively collecting any .psplx
    //       extension files.
    //
    //     - File paths are assumed to contain PSPLX data even if they don't
    //       match the extension (or have no extension).
    //
    //  - Single PSPLX note data. Can come from a single filename pased in CLI
    //    or stdin.
    //
    // TODO: Right now we only support passing a single CLI parameter, either a
    // file or a directory. Implement the other methods for passing the input
    // files to be processed.
    bool require_target_dir = false;

    if (no_opt != NULL) {
        char *note_path = no_opt;
        require_target_dir = true;

        string_t single_note_path = {0};
        str_set_path (&single_note_path, note_path);
        char *curr_note_path = str_data(&single_note_path);

        if (path_isdir (curr_note_path)) {
            rt_init_push_dir (rt, curr_note_path);

        } else if (path_exists (curr_note_path)) {
            rt_init_push_file (rt, curr_note_path);
        }

    } else {
        rt_init_push_dir (rt, str_data(&cfg->source_path));
    }

    // PROCESS DATA
    if (rt->notes_len > 0) {
        rt_process_notes (rt, &error_msg);
    }

    // GENERATE OUTPUT
    if (success) {
        if (command == CLI_COMMAND_GENERATE) {
            if (output_type == CLI_OUTPUT_TYPE_STATIC_SITE) {
                bool has_output = false;
                if (!has_js) {
                    printf (ECMA_YELLOW("warning: ") "generating static site without javascript engine\n");
                    has_output = true;
                }

                if (!require_target_dir || output_dir != NULL) {
                    string_t html_path = {0};
                    if (output_dir != NULL) {
                        str_set_path (&html_path, output_dir);
                        path_ensure_dir (str_data(&html_path));
                    } else {
                        str_set (&html_path, str_data(&cfg->target_path));
                    }
                    str_cat_path (&html_path, ""); // Ensure path ends in '/'

                    size_t end = str_len (&html_path);
                    LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
                        str_put_printf (&html_path, end, "%s", curr_note->id);
                        full_file_write (str_data(&curr_note->html), str_len(&curr_note->html), str_data(&html_path));
                    }


                    if (str_len(&error_msg) > 0) {
                        if (has_output) {
                            printf ("\n");
                        }

                        printf ("%s", str_data(&error_msg));
                        has_output = true;
                    }

                    str_free (&html_path);

                } else {
                    // TODO: I thought this was useful to avoid rewriting the full
                    // static site with a version that contains some single note
                    // that was used for testing. I'm not sure anymore, seems a bit
                    // pointless too because we can just regerate everything as the
                    // sources didn't change.
                    printf (ECMA_RED("error: ") "refusing to generate static site. Using command line input but output directory is missing as parameter, use --output-dir\n");
                }

            } else if (output_type == CLI_OUTPUT_TYPE_HTML && rt->notes_len == 1) {
                // Even though multi note processing should also work in the single
                // note case, I want to have at least one code path where we use the
                // base signle-file PSPLX to HTML implementation. To avoid rotting
                // of this code and increase usage of it.
                char *html = markup_to_html (NULL, &rt->vlt, &rt->sd, str_data(&rt->notes->path), str_data(&rt->notes->psplx), str_data(&rt->notes->path), &error_msg);
                if (html != NULL) {
                    printf ("%s", html);
                }
                free (html);

                // TODO: Show same output as psplx tests.

                if (str_len(&error_msg) > 0) {
                    printf ("%s", str_data(&error_msg));
                }
            }

        } else if (command == CLI_COMMAND_LOOKUP) {
            char *query = no_opt;

            // One off code to list all canonicaly named files, later this
            // should be possible to do using a query.
            //string_t id = {0};
            //BINARY_TREE_FOR(id_to_vlt_file, &rt->vlt.files, node) {
            //    str_set(&id, "");
            //    canonical_id_cat (node->value->id, &id);
            //    printf ("%s - '%s'\n", str_data(&id), str_data(&node->value->path));
            //}

            uint64_t id = 0;
            if (is_canonical_id(query)) {
                id = canonical_id_parse (query, 0);

                if (id != 0) {
                   struct vlt_file_t *file = file_id_lookup (&rt->vlt, id);

                   if (file != NULL) {
                       LINKED_LIST_FOR(struct vlt_file_t *, f, file) {
                           string_t result = str_new(rt->vlt.base_dir);
                           path_cat(&result, str_data(&f->path));

                           if (output_type == CLI_OUTPUT_TYPE_CSV) {
                               printf ("%s\n", str_data(&result));
                           } else {
                               printf ("'%s'\n", str_data(&result));
                           }
                       }
                   }

                } else {
                    printf (ECMA_RED("error:") " Invalid identifier.\n");
                }
            }
        }

        str_free (&error_msg);
    }

    mem_pool_destroy (&rt->pool);

    cfg_destroy (cfg);
    js_destroy ();
    return retval;
}
