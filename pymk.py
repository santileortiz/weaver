#!/usr/bin/python3
from mkpy.utility import *
import common_note_graph as gn
import file_utility as fu

from natsort import natsorted
import random
import psutil
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
out_dir = path_cat(cache_dir, 'www')
ensure_dir(out_dir)

# These directories are part of the code checkout and are used to generate the
# static website.
templates_dir = 'templates'
static_dir = 'static'

def default ():
    target = store_get ('last_snip', default='money_flow')
    call_user_function(target)

server_pid_pname = 'server_pid'
def server_start ():
    last_pid = store_get (server_pid_pname, default=None)
    if last_pid != None and psutil.pid_exists(last_pid):
        print (f'Server is already running, PID: {last_pid}')
    else:
        pid = ex_bg (f'./nocache_server.py', cwd=out_dir)
        print (f'Started server, PID: {pid}')
        store (server_pid_pname, pid)

def server_stop ():
    pid = store_get (server_pid_pname, default=None)
    if pid != None and psutil.pid_exists(int(pid)):
        ex (f'kill {pid}')
    else:
        print ('Server is not running.')

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

    _, id_to_note_title = gn.get_note_maps(source_notes_dir)
    for i, note_id in enumerate(title_notes, 1):
        print (str(i) + ' ' + id_to_note_title[note_id])
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

    root_notes, note_links, note_backlinks = gn.get_note_graph(source_notes_dir, note_title_to_id, ["note", "summary"])

    orphan_notes = [n for n in root_notes if n not in title_notes]
    if len (orphan_notes) > 0:
        print ("Orphan notes:")
        for orphan in orphan_notes:
            print (f"{path_cat(source_notes_dir, orphan)} - {id_to_note_title[orphan]}")
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

    success = True
    if c_needs_rebuild ('weaver.c', './bin/weaver') or ex (f'./bin/weaver --has-js', echo=False) == 0:
        print ('Building weaver...')
        if weaver_build (True) != 0:
            success = False
        else:
            print()

    # TODO: The HTML generator should only process source notes that changed.
    # Currently it generates all notes, all the time.
    if success:
        ex (f'./bin/weaver --generate-static --output-dir {path_cat(out_dir, notes_dir)}')


ensure_dir ("bin")
modes = {
        'debug': '-O0 -g -Wall',
        'profile_debug': '-O3 -g -pg -Wall',
        'release': '-O3 -g -DNDEBUG -Wall'
        }
mode = store('mode', get_cli_arg_opt('-M,--mode', modes.keys()), 'debug')
C_FLAGS = modes[mode]

# Building the Javascript engine increases build times from ~0.5s to ~3s which
# is quite bad, the flags --js-enable and --js-disable toggle this for
# testing/development. The NO_JS macro will be defined to switch into stub
# implementations of functions that call out to javascript.
def check_js_toggle():
    return get_cli_persistent_toggle ('use_js', '--js-enable', '--js-disable', True)

def common_build(c_sources, out_fname, use_js):
    global C_FLAGS

    if use_js:
        c_sources += " lib/duktape.c"
    else:
        C_FLAGS += " -DNO_JS"

    generate_automacros ('automacros.h')

    return ex (f'gcc {C_FLAGS} -o {out_fname} {c_sources} -lm -lrt')

def weaver_build (use_js):
    return common_build ("weaver.c", 'bin/weaver', use_js)

def weaver():
    weaver_build (check_js_toggle ())

def psplx_parser_tests():
    return common_build ("psplx_parser_tests.c", 'bin/psplx_parser_tests', False)

def tsplx_parser_tests():
    return common_build ("tsplx_parser_tests.c", 'bin/tsplx_parser_tests', False)

def cloc():
    ex ('cloc --exclude-list-file=.clocignore .')

def install_dependencies ():
    ex ("sudo apt-get install python3-jinja2 build-essential python3-psutil")

def new_file_id(length = 10):
    characters = ['2','3','4','5','6','7','8','9','C','F','G','H','J','M','P','Q','R','V','W','X']
    new_id = ""
    for i in range(length):
        new_id += random.choice(characters)

    return new_id

def rename_files ():
    prefix = get_cli_arg_opt ("--prefix")
    ordered = get_cli_bool_opt ("--ordered")
    dry_run = not get_cli_bool_opt ("--execute")

    rest_args = get_cli_no_opt ()

    if rest_args == None:
        print (f"usage: ./pymk.py {get_function_name()} [OPTIONS] DIRECTORY")
        return

    path = os.path.abspath(rest_args[0])

    fu.canonical_rename (path, prefix, ordered=ordered,
            dry_run=dry_run, verbose=True)

    if dry_run:
        print ('Dry run by default, to perform changes use --execute')

def move_files ():
    prefix = get_cli_arg_opt ("--prefix")
    ordered = get_cli_bool_opt ("--ordered")
    dry_run = not get_cli_bool_opt ("--execute")
    target = get_cli_arg_opt ("--target")

    position = get_cli_arg_opt ("--position")
    if position != None:
        position = int(position)

    rest_args = get_cli_no_opt()

    if rest_args == None:
        print (f"usage: ./pymk.py {get_function_name()} [OPTIONS] [--target TARGET_DIRECTORY] SOURCE_DIRECTORY")
        return

    source = os.path.abspath (rest_args[0])

    fu.canonical_move (source,
            target,
            prefix=prefix,
            ordered=ordered,
            position=position,
            dry_run=dry_run, verbose=True)

    if dry_run:
        print ('Dry run by default, to perform changes use --execute')

def test_file_utility():
    fu.tests()


builtin_completions = ['--get_build_deps']
if __name__ == "__main__":
    # Everything above this line will be executed for each TAB press.
    # If --get_completions is set, handle_tab_complete() calls exit().
    handle_tab_complete ()

    pymk_default()

