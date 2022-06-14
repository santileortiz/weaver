#!/usr/bin/python3
from mkpy.utility import *
import common_note_graph as gn
import file_utility as fu
import file_utility_tests

import curses, time
from natsort import natsorted
import random
import psutil
import uuid
import json
from zipfile import ZipFile
from urllib.parse import urlparse, urlunparse, ParseResult
from jinja2 import Environment, FileSystemLoader, select_autoescape

# This directory contains user data/configuration. It's essentially what needs
# to be backed up if you want to restore functionality after a OS reinstall.
base_dir = os.path.abspath(path_resolve('~/.weaver'))
ensure_dir(base_dir)

notes_dirname = 'notes'
source_notes_dir = path_cat(base_dir, notes_dirname)
ensure_dir (source_notes_dir)

files_dirname = 'files'
source_files_dir = path_cat(base_dir, files_dirname)
ensure_dir (source_files_dir)

data_dirname = 'data'
data_dir = path_cat(base_dir, data_dirname)
ensure_dir (data_dir)

file_original_name_path = path_cat(data_dir, 'original-name.tsplx')
# TODO: Store file hashes so we can avoid storing duplicates
#file_hash_path = path_cat(data_dir, 'hash.tsplx')

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
        print ('Open at: http://localhost:8000/')
        store (server_pid_pname, pid)

def server_stop ():
    pid = store_get (server_pid_pname, default=None)
    if pid != None and psutil.pid_exists(int(pid)):
        ex (f'kill {pid}')
    else:
        print ('Server is not running.')

viewer_pid_pname = 'viewer_pid'
def view():
    if len(sys.argv) < 3:
        print (f"usage: ./pymk.py {get_function_name()} QUERY")
        return

    last_pid = store_get (viewer_pid_pname, default=None)
    if last_pid != None and psutil.pid_exists(int(last_pid)):
        ex (f'kill {last_pid}')

    # Wasted quite a bit of time attempting these alternative implementations:
    #
    # 1. Tried using the following command to watch a file-view directory, then
    #    used python to clear it and populate it with symbolic links to the
    #    files we wanted to see.
    #
    #    - Requires an additional directory that needs to be managed.
    #    - Was quite slow, when invoking it from vim reloading felt like it
    #      took about 1 second. This seems to be on the pqiv side because
    #      calling it from Python was also slow.
    #    - I was scared of invoking a remove command that would follow links
    #      and remove the original files. Wasted time adding checks to ensure I
    #      was only deleting symbolic links.
    #
    # 2. Tried using pqiv's commands by sending them through a pipe to stdin as
    #    described here https://github.com/phillipberndt/pqiv/issues/183
    #
    #    - Requires a special fifo resource that needs to be managed.
    #    - Commands are quite limited. There is no "remove_all()" so we need to
    #      remove all files one by one.
    #    - Execution order of commands doesn't seem to be guaranteed, probably
    #      piqv tries to be clever and uses multithreading?.
    #    - Some commands seemed to get lost?.
    #    - I ended up in broken states, should have removed all old images
    #      loaded new ones, but I ended up with a mix.
    #    - The fifo got into a broken state at least once, I had to remove it
    #      and recreate it.
    #    - A single error stopped further command execution.
    #    - Even though I never set /home/santiago/weaver as input directory
    #      (but it was the WD), got lots of errors like
    #           Failed to load image /home/santiago/.weaver/files/book-photos/2QGW64VPQ9.1.jpg: Error opening file /home/santiago/weaver: Is a directory
    #
    # All of this to realize that just closing and reopening pqiv is the
    # simplest, fastest and most reliable mechanism. Don't know how I went all
    # other routes even after knowing of the window positioning and sizing
    # actions from the very beginning.

    files = ex(f"./bin/weaver lookup --csv {sys.argv[2]}", ret_stdout=True, echo=False).split('\n')
    pid = ex_bg('pqiv --sort --allow-empty-window --wait-for-images-to-appear --lazy-load --window-position=0,0 --scale-mode-screen-fraction=1 --hide-info-box ' + ' '.join(files))
    store (viewer_pid_pname, pid)

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
                    print (f'http://localhost:8000/?n={fname} - {note_title}')

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
    gn.copy_changed(source_files_dir, path_cat(out_dir, files_dirname))

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
        ex (f'./bin/weaver generate --static --output-dir {path_cat(out_dir, notes_dirname)}')


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

def resolve_source_list(path_list):
    source_list = []
    for path in path_list:
        if path_isdir(path):
            source_list += fu.collect(path)
        else:
            source_list.append(path)

    return source_list

def file_rename ():
    prefix = get_cli_arg_opt ("--prefix")
    ordered = get_cli_bool_opt ("--ordered")
    dry_run = not get_cli_bool_opt ("--execute")
    save_original_name = get_cli_bool_opt ("--save-original-name")

    rest_args = get_cli_no_opt ()

    if rest_args == None:
        print (f"usage: ./pymk.py {get_function_name()} [OPTIONS] SOURCE...")
        return

    source_list = resolve_source_list(rest_args)
    if source_list==None:
        return

    error, _ = fu.canonical_rename (source_list, prefix, ordered=ordered,
            dry_run=dry_run, original_names=file_original_name_path, verbose=True)

    if error != None:
        print (error)

    if dry_run:
        print ('Dry run by default, to perform changes use --execute')

def file_move ():
    prefix = get_cli_arg_opt ("--prefix")
    ordered = get_cli_bool_opt ("--ordered")
    dry_run = not get_cli_bool_opt ("--execute")
    target = get_cli_arg_opt ("--target-directory,-t")

    position = get_cli_arg_opt ("--position")
    if position != None:
        position = int(position)

    rest_args = get_cli_no_opt()

    if rest_args == None:
        print (f"usage:")
        print (f"  ./pymk.py {get_function_name()} [OPTIONS] [-t DIRECTORY] SOURCE...")
        print (f"  ./pymk.py {get_function_name()} [OPTIONS] SOURCE... DEST")
        return

    if target==None:
        target = os.path.abspath(rest_args[-1])
        path_list = [os.path.abspath(path) for path in rest_args[:-1]]

        if not path_isdir(target):
            print(ecma_red("error:") + f" DEST is not a directory")
            return
    else:
        path_list = [os.path.abspath(path) for path in rest_args]

    source_list = resolve_source_list(path_list)
    if source_list==None or len(source_list)==0:
        print(ecma_yellow("warning:") + f" no source files, nothing to do")
        return

    fu.canonical_move (source_list,
            target,
            prefix=prefix,
            ordered=ordered,
            position=position,
            dry_run=dry_run, original_names=file_original_name_path, verbose=True)

    if dry_run:
        print ('Dry run by default, to perform changes use --execute')

def file_store():
    is_vim_mode = get_cli_bool_opt('--vim')
    args = get_cli_no_opt()

    files = []
    if args == None:
        separator = '|||'
        files_str = ex(f'zenity --file-selection --multiple --separator=\'{separator}\'', echo=False,ret_stdout=True)
        if len(files_str) > 0:
            files = files_str.split(separator)
    else:
        files = args

    original_paths = []
    new_basenames = []
    for path in files:
        error, new_path = fu.file_canonical_rename(path, source_files_dir,
            None, None, None, [], None,
            False, False, keep_name=True, original_names=file_original_name_path)

        if error == None:
            original_paths.append(path)
            new_basenames.append(path_basename(new_path))
        else:
            print(error)

    if len(new_basenames) > 0:
        result = []
        if is_vim_mode:
            for new_basename in new_basenames:
                canonical_name = fu.canonical_parse(new_basename)
                result.append(f'{canonical_name.identifier}')
            print(', '.join(result))

        else:
            for i, new_basename in enumerate(new_basenames):
                canonical_name = fu.canonical_parse(new_basename)
                result.append(f'{original_paths[i]} -> {new_basename}')
            print('\n'.join(result))

def create_test_dir():
    file_map = {
        'DSC00004.JPG': None,
        'DSC00005.JPG': None,
        'DSC00006.JPG': None,
        'DSC00076.JPG': None,
        'DSC00106.JPG': None,
        'DSC00334.JPG': None,
        'IMG00001.JPG': None,
        'IMG00002.JPG': None,
        'IMG00003.JPG': None,
        'IMG00004.JPG': None,
        'IMG00005.JPG': None,
        'IMG00006.JPG': None,
        'IMG00013.JPG': None,
        'IMG00023.JPG': None,
        'IMG00043.JPG': None,
        'IMG00073.JPG': None,
        'IMG00103.JPG': None
    }
    file_utility_tests.create_files_from_map('bin/test', file_map)


def input_char(win):
    while True:
        ch = win.getch()
        if ch in range(32, 127):
            break
        time.sleep(0.05)
    return chr(ch)

def new_scan(path):
    error = None
    cmd = f"scanimage --resolution 300 --format jpeg --output-file '{path}'"

    status = ex(cmd, echo=False, quiet=True)
    if status != 0:
        error = True

    return error, cmd

def interactive_scan():
    rest_args = get_cli_no_opt()

    if rest_args == None:
        print (f"usage: ./pymk.py {get_function_name()} TARGET_DIRECTORY")
        return

    target = os.path.abspath (rest_args[0])

    help_str = textwrap.dedent("""
        Commands:
          n new scan
          p scan next page
          e end section

          h help
          q quit
        """).strip() + '\n'

    mfd = fu.MultiFileDocument(target)

    win = curses.initscr()
    curses.start_color()
    win.scrollok(1)
    win.addstr(help_str)

    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_RED, -1)

    while True:
        win.addstr('> ')
        c = input_char(win)
        win.addstr('\n')

        if c.lower() == 'n':
            path_to_scan = fu.mfd_new(mfd)
            error, output = new_scan (path_to_scan)

            win.addstr(f'{output}\n')
            win.refresh()

            if error == None:
                win.addstr(f'New: {path_basename(path_to_scan)}\n')
            else:
                win.addstr('error:', curses.color_pair(1))
                win.addstr(' could not scan page\n')

        elif c.lower() == 'p':
            path_to_scan = fu.mfd_new_page (mfd)
            error, output = new_scan (path_to_scan)

            win.addstr(f'{output}\n')
            win.refresh()

            if error == None:
                win.addstr(f'New: {path_basename(path_to_scan)}\n')
            else:
                win.addstr('error:', curses.color_pair(1))
                win.addstr(' could not scan page\n')

            win.addstr (f'Document files:\n')
            for p in mfd.document_files:
                win.addstr (f'  {p}\n')

        elif c.lower() == 'e':
            fu.mfd_end_section (mfd)
            win.addstr (f'Document files:\n')
            for p in mfd.document_files:
                win.addstr (f'  {p}\n')

        elif c.lower() == 'h':
            win.addstr(help_str)

        elif c.lower() == 'q':
            break

    curses.endwin()

def test_file_utility():
    file_utility_tests.tests()

def id():
    args = get_cli_no_opt()
    
    cnt = 1
    if args != None:
        cnt = int(args[0])

    ids = []
    for i in range(cnt):
        idx = None
        if get_cli_bool_opt('--ordered'):
            idx = i + 1

        _, name = fu.new_canonical_name(idx=idx)
        ids.append(name)
    print (" ".join(ids))

name_predicate = 'name'
type_predicate = 'a'
default_indent = 2

class Query():
    def __init__(self, s):
        self.s = s

class Identifier():
    def __init__(self, s):
        self.s = s

def default_serializer(v):
    if isinstance(v, Identifier) or v == None:
        return v

    elif isinstance(v, int):
        return str(v)

    elif isinstance(v, list):
        return '[' + ','.join([default_serializer(x) for x in v]) + ']'

    elif isinstance(v, set):
        return ' ; '.join([default_serializer(x) for x in v])

    elif isinstance(v, Query):
        return 'q(' + as_string_literal(v.s) + ')'

    elif isinstance(v, ParseResult):
        return '<' + urlunparse(v) + '>'

    else:
        return as_string_literal(v)

def apply_value_serializers(properties, value_serializers):
    result = {}
    if value_serializers == None:
        for p, v in properties.items():
            result[p] = default_serializer(v)
    else:
        pass
        # TODO: Actually apply custom serializers

    return result;

def write_constructor_parameters(constructors, properties, properties_left,
        type_name=None, value_serializers=None):

    constructor_parameters = ''
    first_parameter = True

    properties_serialized_values = apply_value_serializers(properties, value_serializers)
    
    if type_name == None:
        type_name = properties[type_predicate]

    if constructors != None and type_name in constructors.keys():
        constructor_parameters = '['

        for target_prop in constructors[type_name]:
            if not first_parameter:
                constructor_parameters += ','
            first_parameter=False

            if target_prop not in properties.keys() and target_prop in constructors.keys():
                constructor_parameters += write_constructor_parameters(constructors, properties, properties_left, target_prop, value_serializers)

            else:
                if target_prop in properties.keys():
                    constructor_parameters += properties_serialized_values[target_prop]
                    properties_left.remove(target_prop)

        constructor_parameters += ']'

    elif name_predicate in properties.keys() and '\n' not in properties[name_predicate]:
        if isinstance(properties[name_predicate], str) or isinstance(properties[name_predicate], set) or isinstance(properties[name_predicate], list):
            constructor_parameters = f"[{properties_serialized_values[name_predicate]}]"
            properties_left.remove(name_predicate)

    return constructor_parameters

def ensure_single_empty_line(s):
    if s[-2] == '\n' and s[-1] == '\n':
        return ''
    elif s[-1] == '\n':
        return '\n'
    else:
        return '\n\n'

def as_string_literal(s):
    return '"' + s.replace('"', '\\"') + '"'

def node_to_object_string(floating, properties, constructors=None, value_serializers=None, indent=0):
    properties_serialized_values = apply_value_serializers(properties, value_serializers)

    # Serialize the constructor, if one should be added
    res = ''
    type_name = None
    properties_left = set(properties.keys())
    if type_predicate in properties.keys():
        type_name = properties[type_predicate]
        properties_left.remove(type_predicate)

        constructor_parameters = write_constructor_parameters(constructors, properties, properties_left)

        res = ' '*indent + type_name + constructor_parameters

    # Serialize the content of the node
    if len(properties_left) > 0 or len(floating) > 0:
        res += ' '*indent + '{\n'
        for node in floating:
            res += node_to_object_string([], node, constructors, value_serializers, indent=indent+default_indent)

        for prop in properties_left:
            res += ' '*(indent+default_indent) + f'{prop} ' + properties_serialized_values[prop] + ' ;\n'
        res += ' '*indent + '}\n\n'
    else:
        res += ';\n'

    return res

def process_json_data_dump_2():
    from io import StringIO
    import vobject

    path = ex("./bin/weaver lookup --csv XRXC6R5F4W", ret_stdout=True, echo=False)

    property_map = [
        ("fn", "name"),
        ("tel", "telephone"),
        ("email", "email")
    ]

    def org_values(vcard):
        # This feels like a bug in vobject, why do ORG properties have nested
        # lists?, I expected them to just be a string with the company name.
        return {v.value[0] for v in vcard.contents["org"]}

    res = ''
    with ZipFile(path) as zip_file:
        res = 'q(this-file statements ?s) @ {from santileortiz-google-contacts};\n\n'

        f = StringIO(zip_file.read("Takeout/Contacts/My Contacts/My Contacts.vcf").decode('utf-8'))
        for vcard in vobject.readComponents(f):
            if ("kind" in vcard.contents.keys() and "org" in vcard.contents["kind"]) or ("org" in vcard.contents.keys() and "fn" not in vcard.contents.keys()):
                vcard_properties = {
                    "a": "company",
                    "name": org_values(vcard),
                }
            else:
                vcard_properties = {"a": "person"}
                if "org" in vcard.contents.keys():
                    vcard_properties["works-for"] = org_values(vcard)

            for source_prop, target_prop in property_map:
                if source_prop in vcard.contents.keys():
                    values = {v.value for v in vcard.contents[source_prop]}

                    if target_prop in vcard_properties.keys():
                        vcard_properties[target_prop] |= values
                    else:
                        vcard_properties[target_prop] = values

            if len(vcard_properties) > 1:
                res += node_to_object_string([], vcard_properties)

    with open(path_cat(data_dir, 'person.tsplx'), 'w+') as target:
        target.write(res)

def process_json_data_dump_1():
    import io
    import json

    path = ex("./bin/weaver lookup --csv 365FV4GMW8", ret_stdout=True, echo=False)

    type_map = {
        1: "account",
        2: "note",
        3: "card",
    }

    mapping = {
        "account" : [
            ("id", "foreign-identifier"),
            (["login", "username"], "username"),
            (["login", "password"], "password"),
            ("name", "name")
        ],

        "note" : [
            ("id", "foreign-identifier"),
            ("notes", "content"),
            ("name", "name")
        ],

        "card" : [
            ("id", "foreign-identifier"),
            ("name", "name"),
            (["card", "cardholderName"], "card-holder"),
            (["card", "number"], "number"),
            (["card", "expMonth"], "expiration-month"),
            (["card", "expYear"], "expiration-year"),
            (["card", "code"], "code"),
        ],
    }

    res = ''
    with open(path, 'r') as f:
        json_data = json.loads(f.read())
        for item in json_data['items']:
            source_type = item['type']
            if source_type in type_map.keys() and type_map[source_type] in mapping.keys():
                target_type = type_map[source_type]
                item_properties = {
                    'a': target_type,
                }

                for source_prop, target_prop in mapping[target_type]:
                    if isinstance(source_prop, list):
                        value = item
                        for p in source_prop:
                            value = value[p]
                    else:
                        value = item[source_prop]

                    if value != None:
                        item_properties[target_prop] = value

                if 'login' in item.keys() and 'uris' in item['login'].keys():
                    for uri in item['login']['uris']:
                        if 'url' not in item_properties.keys():
                            item_properties['url'] = set()
                        item_properties['url'].add(urlparse(uri['uri']))

                res += node_to_object_string([], item_properties)

    with open(path_cat(data_dir, 'secure.tsplx'), 'w+') as target:
        target.write(res)

def process_json_data_dump():
    import io
    import json

    # Right now this is based on a dump of all my playlists, done with Soundiiz
    path = ex("./bin/weaver lookup --csv H6XH3JGJFW ", ret_stdout=True, echo=False)

    track_constructor = ["name", "artist", "foreign-identifier"]
    constructors = {
        # Useful test, uncomment the playlist constructor. Nothing should
        # break, data output should be equivalent.
        #"playlist" : ["name", "dump-date"],

        "ytmusic-track" : track_constructor,
        "spotify-track" : track_constructor,
    }

    # FIXME: This way of apending strings is O(n^2). I'm sacrificing
    # performance for clarity and readability on the code. Improve this by
    # porting the code to C.
    res = ''
    with ZipFile(path) as zip_file:
        res = 'q(this-file statements ?s) @ {from santileortiz-soundiiz};\n\n'

        for path in zip_file.namelist():
            dirname, fname, extension = path_parse(path)
            if extension == '.json':
                m = re.search('(.*) \((.*)\)', fname)
                dump_date = m.group(2)
                playlist_properties = {
                    'a': 'playlist',
                    'name': m.group(1),
                    'dump-date': m.group(2),
                }

                playlist_floating = []
                playlist_json = json.loads(zip_file.read(path))
                for track in playlist_json:
                    track_properties = {
                        'a': f"{track['platform']}-track",
                        'name': track['title'],
                        'foreign-identifier': track['id'],
                        'artist': Query(track['artist']),
                    }
                    playlist_floating.append(track_properties)

                res += node_to_object_string(playlist_floating, playlist_properties, constructors)

    with open(path_cat(data_dir, 'my-playlists.tsplx'), 'w+') as target:
        target.write(res)

def process_csv_data_dump():
    import numpy as np
    import pandas as pd
    import io

    # Currently this is an example of a data dump from my Fibery workspace
    path = ex("./bin/weaver lookup --csv G5RJR34J49", ret_stdout=True, echo=False)

    type_map = {
        "Musical Artist": "musician",
        "Concert": "concert",
    }

    mapping = {
        "musician" : [
            ("Id", "foreign-identifier"),
            ("Creation Date", "instantiation-date"),
            ("Modification Date", "edit-date"),

            ("Name", "name")
        ],
        "concert" : [
            ("Id", "foreign-identifier"),
            ("Creation Date", "instantiation-date"),
            ("Modification Date", "edit-date"),

            ("Festival", "name"),

            # Fibery has a limitation here, because they serialize relationship
            # columns by using the name in the foreign table, then they use |
            # as separator while at the same time allowing the | character in
            # an entry's name. This means data structure will always be broken
            # if there are row names containing |, and there's nothing we can
            # do to fix it.
            #
            # TODO: I think there's an RFC for CSV. Do they have some handling
            # for columns with array-like type?.
            # TODO: These are really relationships to instances of musician
            # type. I'm thinking of these alternatives:
            #  1. Resolve these to IDs. Which means assigning IDs to everything,
            #     consuming some of the finite amount of IDs available. And also
            #     making the end serialization less readable as plain text
            #     (although, editor features like inline hints could help with
            #     this).
            #  2. Leave them as strings representing names and add a policy
            #     where reusing a string always means a relationship
            #  3. Add a special "query notation" where these name-based
            #     relationships are expressed like q("Dream Theater").
            # I'm leaning towards making 1 and 3 work, then allowing users to
            # decide which one to use when doing the data mapping. Option 2
            # sounds like a policy with very far reaching consequences
            # (homonyms are very common, and much more across languages take
            # for instance "pan" is it a prefix?, in spanish?, english?.
            ("Lineup", "lineup", lambda x: [Query(v) for v in x.split('|')] if x!=None else None),

            # I think we have a better approach to handling dates, where by
            # default it's always an interval, a lot of dates in this data
            # would be better represented as single dates with day precision,
            # but becaue some fastivals span multiple days I needed to enable
            # this start and end format.
            #
            # TODO: Transform this split dates into compressed versions that
            # don't use 2 start/end dates and instead use lower precision.
            (["Fecha Start", "Fecha End"], "date", lambda start,end: f'{start} {end}' if start!=None and end!=None else None),
            ("Precio", "ticket-price", lambda x: f'{x} MXN' if x!= None else x),
            ("URL", "url", lambda x: urlparse(x) if x!= None else x)
        ],
    }

    constructors = {
        "fibery-metadata": ["foreign-identifier", "instantiation-date", "edit-date"],
        "concert" : ["date", "ticket-price", "url", "fibery-metadata"],
        "musician" : ["name", "fibery-metadata"]
    }

    res = ''
    with ZipFile(path) as zip_file:
        res += 'q(this-file statements ?s) @ {from santileortiz-fibery};\n\n'

        for path in zip_file.namelist():

            dirname, fname, extension = path_parse(path)
            if fname in type_map.keys():
                res += ensure_single_empty_line(res)

                target_type = type_map[fname]

                # FIXME: This breaks if there is a column where we expect to
                # actually have a NaN value.
                df = pd.read_csv(io.BytesIO(zip_file.read(path))).replace([np.nan], [None])

                res += f'// {fname}\n'
                for index, row in df.iterrows():
                    target_properties = {}
                    target_properties[type_predicate] = target_type
                    for source_prop, target_prop, *rest in mapping[target_type]:
                        if not isinstance(source_prop, list):
                            source_prop = [source_prop]

                        t = lambda x: str(x) if x!= None else x
                        if len(rest) != 0:
                            t = rest[0]
                        source_values = row[source_prop]
                        value = t(*source_values)

                        if value != None:
                            target_properties[target_prop] = value

                    res += node_to_object_string([], target_properties, constructors)

    with open(path_cat(data_dir, 'santileortiz-fibery.tsplx'), 'w+') as target:
        target.write(res)

builtin_completions = ['--get_build_deps']
if __name__ == "__main__":
    # Everything above this line will be executed for each TAB press.
    # If --get_completions is set, handle_tab_complete() calls exit().
    handle_tab_complete ()

    pymk_default()
