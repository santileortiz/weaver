#!/usr/bin/python3
from mkpy.utility import *
import common_note_graph as gn

import uuid
import json
from jinja2 import Environment, FileSystemLoader, select_autoescape

# This directory contains user data/configuration. It's essentially what needs
# to be backed up if you want to restore functionality after a OS reinstall.
base_dir = os.path.abspath(path_resolve('~/.weaver'))
ensure_dir(base_dir)

notes_dir = 'notes'
source_notes_dir = path_cat(base_dir, notes_dir)
ensure_dir (source_notes_dir)

files_dir = 'files'
source_files_dir = path_cat(base_dir, files_dir)
ensure_dir (source_files_dir)

# This directory contains data that is automatically generated from user's data
# in the base directory.
cache_dir = os.path.abspath(path_resolve('~/.cache/weaver/'))
out_dir = path_cat(cache_dir, 'build')
ensure_dir(out_dir)

# These directories are part of the code checkout and are used to generate the
# static website.
templates_dir = 'templates'
static_dir = 'static'

def default ():
    generate()

def open_browser ():
    # So... AJAX GET requests are forbidden to URLs using the file:// protocol,
    # I don't understand why... supposedly, Firefox did allow it a while back,
    # as long as the page was originally loaded as file://, I don't know why
    # they changed it, but it doesn't work anymore.
    #
    # TODO: What happens if we don't use --user-data-dir and there's a chrome
    # session already open?
    chrome_data_dir = path_cat (cache_dir, 'chrome_data')
    ensure_dir (chrome_data_dir)
    ex (f'google-chrome --user-data-dir={chrome_data_dir} --allow-file-access-from-files {path_cat(out_dir,"index.html")}&')

def new_note ():
    is_vim_mode = get_cli_bool_opt('--vim')
    ensure_dir(source_notes_dir)

    new_note_path = path_cat(source_notes_dir, str(uuid.uuid4()))

    if not is_vim_mode:
        ex ('touch ' + new_note_path)
        ex ('xdg-open ' + new_note_path)

    else:
        print (new_note_path)

def list_notes ():
    folder = os.fsencode(source_notes_dir)
    for fname_os in os.listdir(folder):
        fname = os.fsdecode(fname_os)

        note_f = open(path_cat(source_notes_dir, fname), 'r')
        note_display = fname + ' - ' + note_f.readline()[2:-1] # Remove starting "# " and ending \n
        print (note_display)
        note_f.close()

def search_notes ():
    is_vim_mode = get_cli_bool_opt('--vim')
    args = get_cli_no_opt()

    if args != None and len(args) > 0:
        # args[0] is the snip's name. Also make lowercase to make search case insensitive.
        arg = args[0].lower()

        folder = os.fsencode(source_notes_dir)
        for fname_os in os.listdir(folder):
            fname = os.fsdecode(fname_os)

            note_path = path_cat(source_notes_dir, fname)
            note_f = open(note_path, 'r')
            note_title = note_f.readline()[2:-1] # Remove starting "# " and ending \n

            if not is_vim_mode:
                note_data = note_f.read()
                if arg in note_data.lower() or arg in note_title.lower():
                    print (note_path + ' - ' + note_title)

            else:
                if arg in note_title.lower():
                    print (note_path + ':1:' + note_title)

                for i, line in enumerate(note_f, 1):
                        if arg in line.lower():
                            print (note_path + ':' + str(i) + ':' + note_title)

            note_f.close()

def add_title_note ():
    title_notes = store_get ('title_notes', [])
    title_notes.append(get_cli_no_opt()[0])
    store ('title_notes', title_notes)

def remove_title_note ():
    title_notes = store_get ('title_notes', [])
    note_id = get_cli_no_opt()[0]
    if note_id in title_notes:
        title_notes.remove(note_id)
        store ('title_notes', title_notes)
    else:
        print ("There's no title note with identifier " + note_id)

def sort_title_notes ():
    title_notes = store_get ('title_notes', [])

    for i, note_id in enumerate(title_notes, 1):
        print (str(i) + ' ' + note_id)
    print()

    order_str = input('Specify new title order (numbers separated by spaces): ')
    order = [int(v) for v in order_str.split()]

    title_notes_cpy = title_notes[:]
    new_title_notes = []
    for i in order:
        new_title_notes.append(title_notes_cpy[i-1])
        title_notes.remove(title_notes_cpy[i-1])

    new_title_notes += title_notes
    print ('\nNew title notes:\n' + str(new_title_notes))
    store ('title_notes', new_title_notes)

def generate ():
    title_notes = store_get ('title_notes', [])
    note_title_to_id, id_to_note_title = gn.get_note_maps(source_notes_dir)

    orphan_notes = gn.get_orphans(source_notes_dir, note_title_to_id.keys())
    orphan_notes = [n for n in orphan_notes if note_title_to_id[n] not in title_notes]
    if len (orphan_notes) > 0:
        print ("Orphan notes:")
        for orphan in orphan_notes:
            print (f"{path_cat(source_notes_dir, note_title_to_id[orphan])} - {orphan}")
        print()

    env = Environment(
        loader=FileSystemLoader(templates_dir),
        autoescape=select_autoescape(['html']),
        trim_blocks=True,
        lstrip_blocks=True
    )
    env.globals = globals()

    for tname in env.list_templates():
        template = env.get_template (tname)

        out_path = path_cat(out_dir, tname)
        ensure_dir (path_dirname(out_path))
        template.stream(locals()).dump(out_path)

    gn.copy_changed(static_dir, out_dir)
    gn.copy_changed(source_files_dir, path_cat(out_dir, files_dir))
    gn.copy_changed(source_notes_dir, path_cat(out_dir, notes_dir))

if __name__ == "__main__":
    # Everything above this line will be executed for each TAB press.
    # If --get_completions is set, handle_tab_complete() calls exit().
    handle_tab_complete ()

    pymk_default()

