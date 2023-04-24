/*
 * Copyright (C) 2021 Santiago León O.
 */
#include "common.h"
#include "scanner.c"
#include "binary_tree.c"
#include "test_logger.c"
#include "cli_parser.c"
#include "html_builder.h"
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
    note_init(new_note);

    str_set (&new_note->path, fname);

    size_t source_len;
    char *source = full_file_read (NULL, fname, &source_len);
    strn_set (&new_note->psplx, source, source_len);
    free (source);

    if (!parse_note_title (fname, str_data(&new_note->psplx), &new_note->title, &rt->sd, &new_note->error_msg)) {
        new_note->error = true;

    } else {
        title_to_note_insert (&rt->notes_by_title, &new_note->title, new_note);

        psx_get_or_set_entity(&rt->sd, new_note->id, "note", str_data(&new_note->title));
    }
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

    if (!rt->is_public) {
        splx_get_value_cstr_arr (config, &rt->pool, CFG_TITLE_NOTES, &rt->title_note_ids, &rt->title_note_ids_len);
    } else {
        splx_get_value_cstr_arr (config, &rt->pool, CFG_PUBLIC_TITLE_NOTES, &rt->title_note_ids, &rt->title_note_ids_len);
    }
    splx_get_value_cstr_arr (config, &rt->pool, CFG_PRIVATE_TYPES, &rt->private_types, &rt->private_types_len);

    rt->user_late_cb_tree.pool = &rt->pool;
    psx_populate_internal_late_cb_tree (&rt->user_late_cb_tree);
}

//
//////////////////////////////////////

struct config_t {
    string_t home;

    string_t config_path;

    string_t source_notes_path;
    string_t source_files_path;

    string_t target_path;
    string_t target_notes_path;
};

void cfg_destroy (struct config_t *cfg)
{
    str_free (&cfg->home);
    str_free (&cfg->config_path);
    str_free (&cfg->source_notes_path);
    str_free (&cfg->source_files_path);
    str_free (&cfg->target_path);
    str_free (&cfg->target_notes_path);
}

#define DEFAULT_HOME_DIR "~/.weaver"
#define DEFAULT_TARGET_DIR "~/.cache/weaver/www/"

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

    if (!get_cli_bool_opt_ctx (cli_ctx, "--deterministic", argv, argc)) {
        srandom(time(NULL));
    } else {
        srandom(0);
    }

    if (get_cli_bool_opt_ctx (cli_ctx, "--has-js", argv, argc)) return has_js;

    // TODO: If configuration file doesn't exist, copy the default one from the
    // resources. The resources should be embedded in the app's binary.

    STACK_ALLOCATE (struct config_t, cfg);

    char *cli_home = get_cli_arg_opt_ctx (cli_ctx, "--home", argv, argc);
    if (cli_home != NULL) {
        str_set_path (&cfg->home, cli_home);
    } else {
        str_set_path (&cfg->home, DEFAULT_HOME_DIR);
    }

    str_set_path (&cfg->config_path, str_data(&cfg->home));
    str_cat_path (&cfg->config_path, "config.tsplx");

    str_set_path (&cfg->source_notes_path, str_data(&cfg->home));
    str_cat_path (&cfg->source_notes_path, "notes/");

    str_set_path (&cfg->source_files_path, str_data(&cfg->home));
    str_cat_path (&cfg->source_files_path, "files/");

    struct splx_data_t config = {0};
    tsplx_parse_name (&config, str_data(&cfg->config_path));

    rt->is_public = get_cli_bool_opt_ctx (cli_ctx, "--public", argv, argc);
    rt_init (rt, &config);

    rt->vlt.base_dir = str_data(&cfg->source_files_path);
    vlt_init (&rt->vlt);

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

    bool is_verbose = get_cli_bool_opt_ctx (cli_ctx, "--verbose", argv, argc);

    char *output_dir = get_cli_arg_opt_ctx (cli_ctx, "--output-dir", argv, argc);
    if (output_dir != NULL) {
        str_set_path (&cfg->target_path, output_dir);
    } else {
        str_set_path (&cfg->target_path, DEFAULT_TARGET_DIR);

        struct splx_node_t *tgt_dir = splx_node_get_attribute (config.root, CFG_TARGET_DIR);
        if (tgt_dir != NULL) {
            str_set_path (&cfg->target_path, str_data(&tgt_dir->str));
        }
    }

    if (!ensure_path_exists (str_data(&cfg->home))) {
        success = false;
        printf (ECMA_RED("error: ") "app home directory could not be created\n");
    }

    if (!ensure_path_exists (str_data(&cfg->target_path))) {
        success = false;
        printf (ECMA_RED("error: ") "target directory could not be created\n");
    }

    if (!dir_exists (str_data(&cfg->source_notes_path))) {
        success = false;
        printf (ECMA_RED("error: ") "source directory does not exist\n");
    }

    if (!ensure_path_exists (str_data(&cfg->source_files_path))) {
        success = false;
        printf (ECMA_RED("error: ") "files directory could not be created\n");
    }

    string_t output_data_file = {0};
    str_set_path (&output_data_file, str_data(&cfg->target_path));
    path_cat (&output_data_file, "data.js");

    str_set_path (&cfg->target_notes_path, str_data(&cfg->target_path));
    path_cat (&cfg->target_notes_path, "notes");
    if (!ensure_path_exists (str_data(&cfg->target_notes_path))) {
        success = false;
        printf (ECMA_RED("error: ") "target notes directory could not be created\n");
    }

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

    if (command == CLI_COMMAND_NONE && no_opt != NULL) {
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
        rt_init_push_dir (rt, str_data(&cfg->source_notes_path));
    }

    // PROCESS DATA
    if (rt->notes_len > 0) {
        rt_process_notes (rt, &error_msg);

        rt_late_user_callbacks (rt);

        render_all_backlinks(rt);
    }

    //print_splx_dump (&rt->sd, rt->sd.entities);

    // GENERATE OUTPUT
    if (success) {
        if (command == CLI_COMMAND_GENERATE) {
            path_ensure_dir (str_data(&cfg->target_notes_path));

            if (output_type == CLI_OUTPUT_TYPE_STATIC_SITE) {
                bool has_output = false;
                if (!has_js) {
                    printf (ECMA_YELLOW("warning: ") "generating static site without javascript engine\n");
                    has_output = true;
                }

                if (!require_target_dir || str_len(&cfg->target_notes_path) > 0) {
                    // Clear the notes directory before writing into it.
                    if (path_exists(str_data(&cfg->target_notes_path))) {
                        path_rmrf(str_data(&cfg->target_notes_path));
                    }

                    string_t html_path = {0};
                    if (str_len(&cfg->target_notes_path) > 0) {
                        str_set_path (&html_path, str_data(&cfg->target_notes_path));
                        path_ensure_dir (str_data(&html_path));
                    } else {
                        str_set (&html_path, str_data(&cfg->target_path));
                    }
                    str_cat_path (&html_path, ""); // Ensure path ends in '/'

                    size_t end = str_len (&html_path);
                    LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
                        str_put_printf (&html_path, end, "%s", curr_note->id);

                        if (!curr_note->error && note_is_visible(curr_note))
                        {
                            string_t html_str = {0};
                            str_cat_html (&html_str, curr_note->html, 2);
                            full_file_write (str_data(&html_str), str_len(&html_str), str_data(&html_path));
                            str_free(&html_str);
                        }
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


                string_t generated_data = {0};

                bool is_first = true;
                str_cat_c(&generated_data, "title_notes = [");
                for (int i=0; i<rt->title_note_ids_len; i++) {
                    if (!is_first) str_cat_c(&generated_data, ",");
                    is_first = false;

                    str_cat_printf(&generated_data, "\"%s\"", rt->title_note_ids[i]);
                }
                str_cat_c(&generated_data, "];\n\n");

                is_first = true;
                str_cat_c(&generated_data, "id_to_note_title = {");
                LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
                    if (!is_first) str_cat_c(&generated_data, ", ");
                    is_first = false;

                    str_cat_printf(&generated_data, "\"%s\":\"%s\"", curr_note->id, str_data(&curr_note->title));
                }
                str_cat_c(&generated_data, "};\n\n");


                str_cat_c(&generated_data, "virtual_entities = {\n");

                LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, rt->sd.entities->floating_values) {
                    struct splx_node_t *entity = curr_list_node->node;

                    if (!splx_node_is_referenceable (entity)) {
                        struct splx_node_t *virtual_id = splx_node_get_attribute (entity, "t:virtual_id");

                        // Generate PSPLX
                        string_t psplx = {0};
                        str_cat_c (&psplx, "# ");

                        char *name_str = NULL;
                        {
                            string_t *name = splx_node_get_name (entity);
                            if (name != NULL) {
                                name_str = str_data(name);
                            } else {
                                name_str = "<++>";
                            }
                        }
                        str_cat_c (&psplx, name_str);
                        str_replace (&psplx, "'", "\\'", NULL);
                        str_replace (&psplx, "\n", " ", NULL);
                        str_cat_c (&psplx, "\n");

                        struct splx_node_list_t *types = splx_node_get_attributes (entity, "a");
                        LINKED_LIST_FOR (struct splx_node_list_t *, curr_attr, types) {
                            struct splx_node_t *node = curr_attr->node;

                            str_cat_c (&psplx, "^");
                            str_cat_c (&psplx, str_data(&node->str));
                        }

                        str_replace (&psplx, "\n", "\\n", NULL);


                        // Generate HTML
                        string_t html = {0};

                        STACK_ALLOCATE (struct note_t, dummy_note);
                        note_init (dummy_note);
                        str_set (&dummy_note->title, name_str);

                        STACK_ALLOCATE (struct psx_parser_state_t, ps);
                        STACK_ALLOCATE (struct psx_parser_ctx_t, ctx);
                        ps->block_allocation.pool = &ps->pool;

                        dummy_note->tree = psx_container_block_new(ps, BLOCK_TYPE_ROOT, 0);
                        dummy_note->tree->data = entity;
                        struct psx_block_t *heading = psx_container_block_new(ps, BLOCK_TYPE_HEADING, 0);
                        str_set (&heading->inline_content, name_str);
                        heading->heading_number = 1;
                        LINKED_LIST_APPEND (dummy_note->tree->block_content, heading);

                        dummy_note->html = html_new (&dummy_note->pool, "div");
                        if (virtual_id != NULL) {
                            html_element_attribute_set (dummy_note->html, dummy_note->html->root, "id", str_data(splx_node_get_id (virtual_id)));
                        }
                        block_tree_to_html (ctx, dummy_note->html, dummy_note->tree, dummy_note->html->root);
                        render_backlinks (rt, dummy_note);
                        str_cat_html (&html, dummy_note->html, 2);
                        str_replace (&html, "'", "\\'", NULL);
                        str_replace (&html, "\n", "\\n", NULL);
                        note_destroy (dummy_note);
                        ps_destroy (ps);


                        // Generate TSPLX
                        string_t tsplx = {0};
                        str_cat_splx_canonical_shallow (&tsplx, entity);
                        str_replace (&tsplx, "'", "\\'", NULL);
                        str_replace (&tsplx, "\n", "\\n", NULL);


                        string_t name_escaped = {0};
                        str_set (&name_escaped, name_str);
                        str_replace (&name_escaped, "\n", " ", NULL);
                        str_replace (&name_escaped, "'", "\\'", NULL);

                        if (virtual_id != NULL) {
                            str_cat_printf(&generated_data,
                                "  '%s': {psplx: '%s', html: '%s', tsplx: '%s', 'title': '%s'},\n",
                                str_data(splx_node_get_id (virtual_id)),
                                str_data(&psplx),
                                str_data(&html),
                                str_data(&tsplx),
                                str_data(&name_escaped));
                        }

                        str_free (&tsplx);
                        str_free (&html);
                        str_free (&psplx);
                    }
                }

                str_cat_c(&generated_data, "};\n");
                full_file_write (str_data(&generated_data), str_len(&generated_data), str_data(&output_data_file));
                str_free (&generated_data);

                if (is_verbose) {
                    printf ("target: %s\n", str_data(&cfg->target_path));
                }

            } else if (rt->notes_len == 1) {
                if (output_type == CLI_OUTPUT_TYPE_HTML) {
                    // Even though multi note processing should also work in the single
                    // note case, I want to have at least one code path where we use the
                    // base signle-file PSPLX to HTML implementation. To avoid rotting
                    // of this code and increase usage of it.
                    char *html = markup_to_html (NULL, &rt->vlt, &rt->sd, str_data(&rt->notes->path), str_data(&rt->notes->psplx), str_data(&rt->notes->path), &error_msg);
                    if (html != NULL) {
                        printf ("%s", html);
                    }
                    free (html);

                    if (str_len(&error_msg) > 0) {
                        printf ("%s", str_data(&error_msg));
                    }

                } else if (output_type == CLI_OUTPUT_TYPE_DEFAULT) {
                    struct note_t *note = rt->notes;

                    printf (ECMA_MAGENTA("HTML") "\n");

                    string_t html_str = {0};
                    str_cat_html (&html_str, note->html, 2);
                    prnt_debug_string (str_data(&html_str));
                    str_free(&html_str);

                    if (note->tree->data != NULL) {
                        string_t tsplx_str = {0};
                        str_cat_splx_canonical (&tsplx_str, &rt->sd, note->tree->data);

                        printf ("\n");
                        printf (ECMA_MAGENTA("TSPLX") "\n");
                        prnt_debug_string (str_data(&tsplx_str));

                        str_free (&tsplx_str);
                    }
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

            } else if (strcmp(query, "type()") == 0) {
                struct splx_node_t *entities_node = rt->sd.entities;

                struct cstr_to_splx_node_list_map_t type_index = {0};

                LINKED_LIST_FOR(struct splx_node_list_t *, curr_list_node, entities_node->floating_values) {
                    struct splx_node_t *entitity = curr_list_node->node;

                    BINARY_TREE_FOR (cstr_to_splx_node_list_map, &entitity->attributes, curr_attribute) {
                        if (strcmp(curr_attribute->key, "a") == 0) {
                            LINKED_LIST_FOR (struct splx_node_list_t *, curr_node_list_element, curr_attribute->value) {
                                struct splx_node_list_t *new_node_list_element =
                                    tps_wrap_in_list_node (&rt->sd, curr_node_list_element->node);

                                char *type = str_data(&curr_node_list_element->node->str);
                                struct splx_node_list_t *existing_list = cstr_to_splx_node_list_map_get (&type_index, type);
                                if (existing_list == NULL) {
                                    cstr_to_splx_node_list_map_insert (&type_index, type, new_node_list_element);
                                } else {
                                    new_node_list_element->next = existing_list->next;
                                    existing_list->next = new_node_list_element;
                                }
                            }
                        }
                    }
                }


                BINARY_TREE_FOR (cstr_to_splx_node_list_map, &type_index, curr_type) {
                    int num_instances = 0;
                    LINKED_LIST_FOR (struct splx_node_list_t *, curr_node_list_element, curr_type->value) {
                        num_instances++;
                    }

                    printf ("%s (%i)\n", curr_type->key, num_instances);
                }

                cstr_to_splx_node_list_map_destroy (&type_index);
            }
        }

        str_free (&error_msg);
    }

    mem_pool_destroy (&rt->pool);

    cfg_destroy (cfg);
    js_destroy ();
    return retval;
}
