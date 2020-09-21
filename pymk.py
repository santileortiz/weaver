#!/usr/bin/python3
from mkpy.utility import *

import uuid

notes_dir = 'notes'

def default ():
    target = store_get ('last_snip', default='new_note')
    call_user_function(target)

def new_note ():
    is_vim_mode = get_cli_bool_opt('--vim')
    ensure_dir(notes_dir)

    new_note_path = path_cat(notes_dir, str(uuid.uuid4()))

    if not is_vim_mode:
        ex ('touch ' + new_note_path)
        ex ('xdg-open ' + new_note_path)

    else:
        print (new_note_path)

def list_notes ():
    folder = os.fsencode(notes_dir)
    for fname_os in os.listdir(folder):
        fname = os.fsdecode(fname_os)

        note_f = open(path_cat(notes_dir, fname), 'r')
        note_display = fname + ' - ' + note_f.readline()[2:-1] # Remove starting "# " and ending \n
        print (note_display)
        note_f.close()

def search_notes ():
    is_vim_mode = get_cli_bool_opt('--vim')
    args = get_cli_no_opt()

    if args != None and len(args) > 0:
        # args[0] is the snip's name. Also make lowercase to search case insensitive.
        arg = args[0].lower()

        folder = os.fsencode(notes_dir)
        for fname_os in os.listdir(folder):
            fname = os.fsdecode(fname_os)

            note_path = path_cat(notes_dir, fname)
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

if __name__ == "__main__":
    # Everything above this line will be executed for each TAB press.
    # If --get_completions is set, handle_tab_complete() calls exit().
    handle_tab_complete ()

    pymk_default()

