/*
 * Copyright (C) 2021 Santiago León O.
 */
#include "common.h"
#include "datetime.c"
#include "scanner.c"
#include "binary_tree.c"
#include "test_logger.c"
#include "cli_parser.c"
#include "html_builder.h"
#include "automacros.h"

#include "file_utility.h"
#include "block.h"
#include "note_runtime.h"

// Mustache Templating
#include "lib/cJSON.h"
#include "lib/mustach.h"
#include "lib/mustach-wrap.h"
#include "lib/mustach-cjson.h"

#include "psplx_parser.c"
#include "note_runtime.c"

//////////////////////////////////////
// Platform functions for testing

#define META_CREATED_AT "created-at"
#define META_UPDATED_AT "updated-at"

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

        struct splx_node_t *note_node = psx_get_or_set_entity(&rt->sd, new_note->id, "note", str_data(&new_note->title));

        // TODO: This assumes statx() always exists and contains birth time.
        // This seems to be a somewhat modern API that's not guaranteed to work
        // for all operating systems or C runtime libraries. What would be a
        // better cross platform alternative?, should probably add it into
        // common.h.
        char *fname_abs = abs_path (fname, NULL);
        struct statx s = {0};
        int res = statx(0, fname_abs, 0, STATX_BTIME, &s);
        if (res == 0) {
            char *metadata_created_at = NULL;
            char *metadata_updated_at = NULL;
            if (rt->metadata != NULL) {
                struct splx_node_t *metadata_node = splx_get_node_by_id (rt->metadata, new_note->id);

                if (metadata_node != NULL) {
                    struct splx_node_t *created_at_node = splx_node_get_attribute (metadata_node, META_CREATED_AT);
                    metadata_created_at = str_data(&created_at_node->str);

                    struct splx_node_t *updated_at_node = splx_node_get_attribute (metadata_node, META_UPDATED_AT);
                    metadata_updated_at = str_data(&updated_at_node->str);
                }
            }

            bool use_metadata_created_at = false; // True if create-at in the metadata file is older than the one from the filesystem
            struct date_t date = {0};

            char fs_created_at[DATE_TIMESTAMP_MAX_LEN];
            char result_created_at[DATE_TIMESTAMP_MAX_LEN];
            date = date_read_unix(s.stx_btime.tv_sec);
            date_write_rfc3339(&date, fs_created_at);
            strcpy(result_created_at, fs_created_at); // Default result for create-at is the filesystem's
            if (metadata_created_at != NULL) {
                bool success = false;
                int cmp = date_cmp_str(metadata_created_at, result_created_at, &success, NULL);
                if (success && cmp < 0) {
                    // If there's a metadata file containing an older create-at
                    // make that the result instead.
                    use_metadata_created_at = true;
                    strcpy(result_created_at, metadata_created_at);
                }
            } else {
                str_cat_printf(&rt->changes_log, "A %s - %s\n", new_note->id, str_data(&new_note->title));
            }
            splx_node_attribute_append_c_str(&rt->sd, note_node, META_CREATED_AT, result_created_at, SPLX_NODE_TYPE_STRING);

            char result_updated_at[DATE_TIMESTAMP_MAX_LEN];
            date = date_read_unix(s.stx_mtime.tv_sec);
            date_write_rfc3339(&date, result_updated_at);

            bool success = false;
            int cmp = date_cmp_str(fs_created_at, result_updated_at, &success, NULL);
            if (success && use_metadata_created_at && cmp == 0 && metadata_updated_at != NULL) {
                // If we're using created-at from the metadata file (metadata
                // file contains an older create-at than the file system) and
                // both create-at and update-at in the filesystem are equal,
                // then it's very likely the whole note database was just
                // copy/pasted and the actual content of the page was not
                // modified. Then we better use the metadata's update-at, as
                // long as there's one.
                strcpy(result_updated_at, metadata_updated_at);
            }

            if (metadata_updated_at != NULL) {
                success = false;
                int cmp = date_cmp_str(metadata_updated_at, result_updated_at, &success, NULL);
                if (success && cmp < 0) {
                    // If there's a metadata file with an older update-at than
                    // the file system's, then the file content changed. Notify
                    // that this was the case.
                    str_cat_printf(&rt->changes_log, "M %s - %s\n", new_note->id, str_data(&new_note->title));
                }
            }

            splx_node_attribute_append_c_str(&rt->sd, note_node, META_UPDATED_AT, result_updated_at, SPLX_NODE_TYPE_STRING);

            // TODO: There's notes with dynamic data:
            //
            // - Those that show a list of entities. For these updated-at
            //   should be the last generation date, instead of the page's
            //   content modification date. Should this happen at the "note
            //   entity" data level, or at the frontend level by just marking
            //   some notes as "dynamic" and passing the latest generation
            //   date?.
            //
            // - Virtual notes. What should be the create/update dates for
            //   them?.
        }

        free (fname_abs);
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

    string_t metadata_path;

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
    CLI_OUTPUT_TYPE_CUSTOM_SITE,
    CLI_OUTPUT_TYPE_HTML,
    CLI_OUTPUT_TYPE_CSV,
    CLI_OUTPUT_TYPE_DEFAULT
};

void generate_data_json (struct note_runtime_t *rt, char *out_fname)
{
    // TODO: This should create an identity map serialization of all entities.
    // It should call into a generic JSON serializer for TSPLX data.

    string_t generated_data = {0};

    bool is_first = true;

    string_t escaped_title = {0};
    is_first = true;
    str_cat_c(&generated_data, "{");
    LINKED_LIST_FOR (struct note_t*, curr_note, rt->notes) {
        if (!is_first) str_cat_c(&generated_data, ",\n");
        is_first = false;

        str_set (&escaped_title, str_data(&curr_note->title));
        str_replace (&escaped_title, "\"", "\\\"", NULL);
        str_cat_printf(&generated_data, "\"%s\":", curr_note->id);

        str_cat_c(&generated_data, "{");
        str_cat_printf(&generated_data, "\"name\":\"%s\",", str_data(&escaped_title));
        str_cat_printf(&generated_data, "\"@type\":\"page\"");
        str_cat_c(&generated_data, "}");
    }
    str_cat_c(&generated_data, "}\n");
    str_free(&escaped_title);

    full_file_write (str_data(&generated_data), str_len(&generated_data), out_fname);
    str_free (&generated_data);
}

void generate_metadata (struct note_runtime_t *rt, struct config_t *cfg)
{
    string_t metadata_str = {0};
    STACK_ALLOCATE (struct splx_data_t, metadata);

    LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, rt->sd.entities->floating_values) {
        struct splx_node_t *entity = curr_list_node->node;

        bool is_note = false;
        struct splx_node_list_t *types = splx_node_get_attributes(entity, "a");
        LINKED_LIST_FOR (struct splx_node_list_t *, curr_attr, types) {
            struct splx_node_t *node = curr_attr->node;

            if (strcmp("note", str_data(&node->str)) == 0) {
                is_note = true;
            }
        }

        if (is_note) {
            char *entity_id = str_data(splx_node_get_id (entity));

            if (strlen(entity_id) > 0) {
                struct splx_node_t *metadata_node = splx_node(metadata, entity_id, SPLX_NODE_TYPE_OBJECT);

                struct splx_node_t *created_at_node = splx_node_get_attribute (entity, META_CREATED_AT);
                splx_node_attribute_append_c_str(metadata, metadata_node, META_CREATED_AT, str_data(&created_at_node->str), SPLX_NODE_TYPE_STRING);

                struct splx_node_t *updated_at_node = splx_node_get_attribute (entity, META_UPDATED_AT);
                splx_node_attribute_append_c_str(metadata, metadata_node, META_UPDATED_AT, str_data(&updated_at_node->str), SPLX_NODE_TYPE_STRING);

                str_cat_splx_canonical_shallow(&metadata_str, metadata_node);
                str_cat_c(&metadata_str, "\n");

            } else {
                 // TODO: Which entities of note type don't have an id?... I've
                // seen some hit this case, haven't looked closer at where they
                // were created.
            }
        }
    }

    full_file_write (str_data(&metadata_str), str_len(&metadata_str), str_data(&cfg->metadata_path));
    str_free(&metadata_str);
}

void generate_data_javascript (struct note_runtime_t *rt, char *out_fname, char *home)
{
    string_t generated_data = {0};

    str_cat_printf(&generated_data, "home_path = '%s';\n\n", home);

    bool is_first = true;
    str_cat_c(&generated_data, "title_notes = [");
    for (int i=0; i<rt->title_note_ids_len; i++) {
        if (!is_first) str_cat_c(&generated_data, ",");
        is_first = false;

        str_cat_printf(&generated_data, "\"%s\"", rt->title_note_ids[i]);
    }
    str_cat_c(&generated_data, "];\n\n");


    str_cat_c(&generated_data, "virtual_entities = {\n");

    LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, rt->sd.entities->floating_values) {
        struct splx_node_t *entity = curr_list_node->node;

        if (!splx_node_is_referenceable (entity) &&  entity_is_visible(entity)) {
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
                html_element_attribute_set (dummy_note->html, dummy_note->html->root, SSTR("id"), SSTR(str_data(splx_node_get_id (virtual_id))));
            }
            block_tree_to_html (ctx, dummy_note->html, dummy_note->tree, dummy_note->html->root, false);
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
                               "  '%s': {psplx: '%s', html: '%s', tsplx: '%s', title: '%s'},\n",
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
    full_file_write (str_data(&generated_data), str_len(&generated_data), out_fname);
    str_free (&generated_data);
}

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

    str_set_path (&cfg->metadata_path, str_data(&cfg->home));
    str_cat_path (&cfg->metadata_path, "metadata.tsplx");

    str_set_path (&cfg->source_notes_path, str_data(&cfg->home));
    str_cat_path (&cfg->source_notes_path, "notes/");

    str_set_path (&cfg->source_files_path, str_data(&cfg->home));
    str_cat_path (&cfg->source_files_path, "files/");

    struct splx_data_t config = {0};
    tsplx_parse_name (&config, str_data(&cfg->config_path));

    STACK_ALLOCATE(struct splx_data_t, metadata);
    if (path_exists(str_data(&cfg->metadata_path))) {
        tsplx_parse_name (metadata, str_data(&cfg->metadata_path));
    } else {
        metadata = NULL;
    }

    rt->is_public = get_cli_bool_opt_ctx (cli_ctx, "--public", argv, argc);
    rt_init (rt, &config);
    rt->metadata = metadata;

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

    char *custom_sources_dir = get_cli_arg_opt_ctx (cli_ctx, "--custom", argv, argc);
    if (custom_sources_dir != NULL) {
        output_type = CLI_OUTPUT_TYPE_CUSTOM_SITE;
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

    string_t output_json_file = {0};
    str_set_path (&output_json_file, str_data(&cfg->target_path));
    path_cat (&output_json_file, "data.json");

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

            if (!is_empty_str(str_data(&rt->changes_log))) {
                printf("%s\n", str_data(&rt->changes_log));
            }

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

                generate_metadata(rt, cfg);
                generate_data_javascript(rt, str_data(&output_data_file), cli_home ? str_data(&cfg->home) : DEFAULT_HOME_DIR);
                generate_data_json(rt, str_data(&output_json_file));

                if (is_verbose) {
                    printf ("target: %s\n", str_data(&cfg->target_path));
                }

            } if (output_type == CLI_OUTPUT_TYPE_CUSTOM_SITE) {
                if (!has_js) {
                    printf (ECMA_YELLOW("warning: ") "generating static site without javascript engine\n");
                }

                string_t src_dir_path = {0};
                str_set_path (&src_dir_path, str_data(&cfg->home));
                str_cat_path (&src_dir_path, custom_sources_dir);
                str_cat_path (&src_dir_path, ""); // Ensure path ends in '/'
                if (!dir_exists(str_data(&src_dir_path))) {
                    printf (ECMA_RED("error: ") "no custom sources directory '%s'\n", custom_sources_dir);
                    exit(EXIT_FAILURE);
                }

                string_t out_dir_path = {0};
                str_set_path (&out_dir_path, str_data(&cfg->target_path));
                path_ensure_dir (str_data(&out_dir_path));
                str_cat_path (&out_dir_path, ""); // Ensure path ends in '/'

                copy_dir (str_data(&src_dir_path), str_data(&out_dir_path));

                // Clear the used_file_ids so we get only those used in the
                // content for blog posts, not all the notes.
                // :custom_site_file_output
                rt->used_file_ids_len = 0;

                // Model Creation
                struct splx_node_list_t *splx_posts = splx_get_node_by_type (&rt->sd,  "blog-post");

                // TODO: Filter posts that represent entities which are not
                // visible. Maybe the visibility concept should happen at the
                // SPLX data level not the note level?.

                // TODO: This mutates the original SPLX data for posts, we
                // don't use it for anything else so it's fine. Still, it would
                // be good to have a SPLX "context" mechanism where changes can
                // be performed and then rolled back. Like a database transaction.
                LINKED_LIST_FOR (struct splx_node_list_t *, curr_list_node, splx_posts) {
                    struct splx_node_t *entity = curr_list_node->node;
                    string_t buff = {0};


                    // Effective URL property
                    string_t *name = splx_node_get_name(entity);
                    str_cat_section_id (&buff, str_data(name), false);
                    str_cat_c (&buff, ".html");
                    splx_node_attribute_append_c_str(&rt->sd, entity, "url", str_data(&buff), SPLX_NODE_TYPE_STRING);


                    // Pretty formatting of date
                    struct splx_node_t *publish_date = splx_node_get_attribute(entity, "published-at");
                    if (publish_date) {
                        struct date_t date = {0};
                        if (date_read (str_data(splx_node_get_id(publish_date)), &date, &buff)) {
                            str_set_printf (&buff, "%s %i, %i", month_names[date.month-1], date.day, date.year);
                        } else {
                            printf(ECMA_RED("error: ") "%s", str_data(&buff));
                        }
                        splx_node_attribute_append_c_str(&rt->sd, entity, "published-at@pretty", str_data(&buff), SPLX_NODE_TYPE_STRING);
                    }


                    mem_pool_t pool_l = {0};
                    struct note_t *note = rt_get_note_by_id (str_data(splx_node_get_id(entity)));

                    // Excerpt
                    if (note != NULL) {
                        STACK_ALLOCATE (struct splx_data_t, empty_sd);
                        STACK_ALLOCATE (struct psx_parser_ctx_t, empty_ctx);
                        empty_ctx->sd = empty_sd;
                        struct psx_block_t *note_tree = parse_note_text (&pool_l, empty_ctx, str_data(&note->psplx));
                        splx_destroy (empty_sd);

                        struct psx_block_t *after_title = note_tree->block_content->next;
                        if (after_title != NULL && after_title->type == BLOCK_TYPE_PARAGRAPH) {

                            string_t excerpt = {0};
                            str_cat_inline_content_as_plain (str_data(&after_title->inline_content), &excerpt);
                            splx_node_attribute_append_c_str(&rt->sd, entity, "excerpt", str_data(&excerpt), SPLX_NODE_TYPE_STRING);
                            str_free (&excerpt);
                        }

                    }

                    // Content
                    if (!note->error) {
                        struct html_t *content_html = html_new (&pool_l, "div");
                        STACK_ALLOCATE (struct psx_parser_ctx_t, ctx);
                        ctx->vlt = &rt->vlt;
                        block_tree_to_html (ctx, content_html, note->tree, content_html->root, true);

                        str_set (&buff, "");
                        str_cat_html_element_siblings(&buff, content_html->root->children->next->next->next, 2);
                        splx_node_attribute_append_c_str(&rt->sd, entity, "content", str_data(&buff), SPLX_NODE_TYPE_STRING);
                    }

                    mem_pool_destroy (&pool_l);
                }

                splx_node_list_sort(&splx_posts, "published-at", true);


                cJSON *json_model = cJSON_CreateObject();
                cJSON *json_posts = cJSON_splx_create_array(splx_posts);
                cJSON_AddItemToObject(json_model, "posts", json_posts);
                //printf("%s\n", cJSON_Print(json_model)) ;


                // index.html
                {
                    size_t src_dir_path_len = str_len(&src_dir_path);
                    str_cat_path (&src_dir_path, "index.html");

                    size_t out_dir_path_len = str_len(&out_dir_path);
                    str_cat_path (&out_dir_path, "index.html");

                    uint64_t template_str_len;
                    char *template_str = full_file_read (NULL, str_data(&src_dir_path), &template_str_len);
                    size_t out_len;
                    char *out;

                    mustach_cJSON_mem(template_str, 0, json_model, 0, &out, &out_len);
                    full_file_write (out, out_len, str_data(&out_dir_path));

                    free(out);
                    str_put_c (&src_dir_path, src_dir_path_len, "\0");
                    str_put_c (&out_dir_path, out_dir_path_len, "\0");
                }


                // Posts
                {
                    size_t src_dir_path_len = str_len(&src_dir_path);
                    str_cat_path (&src_dir_path, "post.html");

                    size_t out_dir_path_len = str_len(&out_dir_path);

                    for (int i=0; i < cJSON_GetArraySize(json_posts); i++) {
                        cJSON *json_post = cJSON_GetArrayItem(json_posts, i);

                        str_cat_path (&out_dir_path, cJSON_GetStringValue(cJSON_GetObjectItem(json_post, "url")));

                        uint64_t template_str_len;
                        char *template_str = full_file_read (NULL, str_data(&src_dir_path), &template_str_len);
                        size_t out_len;
                        char *out;

                        mustach_cJSON_mem(template_str, 0, json_post, 0, &out, &out_len);
                        full_file_write (out, out_len, str_data(&out_dir_path));

                        str_put_c (&out_dir_path, out_dir_path_len, "\0");
                    }
                    str_put_c (&src_dir_path, src_dir_path_len, "\0");
                }

                // Files
                // :custom_site_file_output
                //
                // TODO: Maybe don't copy source files? Often we use the same
                // id for a file created with an editor like extension .drawio
                // and its export result with extension .png. Right now both
                // are copied, but maybe only the export result should be...
                {
                    str_set_path (&src_dir_path, rt->vlt.base_dir);
                    size_t src_dir_path_len = str_len(&src_dir_path);

                    str_cat_path (&out_dir_path, "files");
                    size_t out_dir_path_len = str_len(&out_dir_path);
                    path_ensure_dir (str_data(&out_dir_path));

                    for (int i=0; i<rt->used_file_ids_len; i++) {
                        struct vlt_file_t *files = file_id_lookup (&rt->vlt, rt->used_file_ids[i]);

                        LINKED_LIST_FOR (struct vlt_file_t *, file, files) {
                            str_cat_path (&src_dir_path, str_data(&file->path));
                            str_cat_path (&out_dir_path, str_data(&file->path));

                            file_copy (str_data(&src_dir_path), str_data(&out_dir_path));

                            str_put_c (&src_dir_path, src_dir_path_len, "\0");
                            str_put_c (&out_dir_path, out_dir_path_len, "\0");
                        }
                    }
                }

                cJSON_Delete(json_model);

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
