#!/usr/bin/python3
from mkpy.utility import *
import file_utility as fu
import tests_python

import traceback
import tempfile
from datetime import datetime
import curses, time
from natsort import natsorted
import random
import psutil
import json
from zipfile import ZipFile
from urllib.parse import urlparse, urlunparse, ParseResult

fu.weaver_files_init()

file_original_name_path = path_cat(fu.data_dir, 'original-name.tsplx')
# TODO: Store file hashes so we can avoid storing duplicates
#file_hash_path = path_cat(fu.data_dir, 'hash.tsplx')

# This directory contains data that is automatically generated from user's data
# in the base directory.
cache_dir = os.path.abspath(path_resolve('~/.cache/weaver/'))
out_dir = path_cat(cache_dir, 'www')
ensure_dir(out_dir)

public_out_dir = path_cat(cache_dir, 'public')
ensure_dir(public_out_dir)

example_output = os.path.abspath(path_resolve('./bin/example_static/'))
example_public_output = os.path.abspath(path_resolve('./bin/example_public_static/'))

# These directories are part of the code checkout and are used to generate the
# static website.
static_dir = 'static'

def default ():
    target = store_get ('last_snip', default='money_flow')
    call_user_function(target)

server_pid_pname = 'server_pid'
def start_static ():
    last_pid = store_get (server_pid_pname, default=None)
    if last_pid != None and psutil.pid_exists(last_pid):
        print (f'Server is already running, PID: {last_pid}')
    else:
        pid = ex_bg (f'./nocache_server.py', cwd=out_dir)
        print (f'Started server, PID: {pid}')
        print ('Open at: http://localhost:8000/')
        store (server_pid_pname, pid)

def stop_static ():
    pid = store_get (server_pid_pname, default=None)
    if pid != None and psutil.pid_exists(int(pid)):
        ex (f'kill {pid}')
    else:
        print ('Server is not running.')

def server_deploy ():
    server_home = './bin/.weaver_deploy'

    # TODO: This should be read from config.tsplx. Right now we're storing a
    # Python version of the expected result of parsing in Pymk's store.
    ssh_deployment = store_get("default_deployment")

    node_id = ssh_deployment["@id"]
    hostname = ssh_deployment["hostname"]
    username = ssh_deployment["username"]
    pem_file = ssh_deployment["authentication"]["path"]

    
    # TODO: This should be taken directly from ~/.weaver/config.tsplx, right
    # now we duplicate it here as JSON because there's no Python TSPLX parser
    # yet. Just uploading config.tsplx should work.
    config_json = [
        {
            "@id" : node_id,
            "a" : "link-node",
            "url" : store_get("public_url")
        },
        {
            "@id" : store_get("link_node_id"),
            "a" : "link-node",
            "priority" : [
                node_id
            ]
        }
    ]

    ex ('rm -r ./bin/dist')
    ex (f'rm -r {server_home}')
    ex ('bash ./package_venv.sh')
    ensure_dir (server_home)
    with open(f'{server_home}/config.json', 'w') as target_file:
        json.dump(config_json, target_file)

    # :autolink_map_location
    tests_python.data_to_autolink_map(f'{out_dir}/data.json', f'{server_home}/files/map/{node_id}.json')

    ex(f'scp -r -i {pem_file} ./bin/dist/app_dist*.whl {username}@{hostname}:~/')

    ex(f"ssh -i {pem_file} {username}@{hostname} 'rm -r ~/.weaver'")
    ex(f"ssh -i {pem_file} {username}@{hostname} 'mkdir ~/.weaver'")
    ex(f'scp -r -i {pem_file} {server_home}/* {username}@{hostname}:~/.weaver/')

    ex(f"ssh -i {pem_file} {username}@{hostname} 'pip3 install --upgrade --force-reinstall app_dist*.whl'")
    ex(f"ssh -i {pem_file} {username}@{hostname} 'kill -HUP `ps -C gunicorn fch -o pid | head -n 1`'")

    # To provision the target machine
    #
    # Port 80 redirect (https://serverfault.com/questions/112795/how-to-run-a-server-on-port-80-as-a-normal-user-on-linux)
    # (add) iptables -t nat -A PREROUTING -p tcp --dport 80 -j REDIRECT --to-port 8080
    # (remove) 
    #   iptables -t nat --line-numbers -n -L
    #   Look for rule number
    #   iptables -t nat -D PREROUTING <number>
    #
    # Wheel management
    # (install) pip3 install --upgrade --force-reinstall app_dist*.whl
    # (start) gunicorn server.main:app --bind 0.0.0.0:8080 --daemon
    # (restart) kill -HUP `ps -C gunicorn fch -o pid | head -n 1`

viewer_pid_pname = 'viewer_pid'
def view():
    import screeninfo

    if len(sys.argv) < 3:
        print (f"usage: ./pymk.py {get_function_name()} QUERY")
        return

    is_vim_mode = get_cli_bool_opt('--vim')
    query = get_cli_no_opt ()[0]

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

    # Find leftmost coordinate of primary monitor
    x_pos = next(filter(lambda m: m.is_primary, screeninfo.get_monitors())).x

    files = ex(f"./bin/weaver lookup --csv {query}", ret_stdout=True, echo=False).split('\n')
    pid = ex_bg(f'pqiv --sort --allow-empty-window --wait-for-images-to-appear --lazy-load --window-position=0,{x_pos} --scale-mode-screen-fraction=0.95 --hide-info-box ' + ' '.join([f"'{fname}'" for fname in files]))
    if is_vim_mode:
        vim_window_id = ex("wmctrl -l -p | awk -v pid=$PPID '$5~/splx/ {print $1}'", ret_stdout=True, echo=False)
        if vim_window_id != "":
            time.sleep(0.2)
            ex(f"wmctrl -i -a {vim_window_id}", echo=False)
    store (viewer_pid_pname, pid)

def new_note ():
    is_vim_mode = get_cli_bool_opt('--vim')
    ensure_dir(fu.source_notes_dir)

    new_note_path = fu.new_unique_canonical_path(fu.source_notes_dir)

    if not is_vim_mode:
        ex ('touch ' + new_note_path)
        ex ('xdg-open ' + new_note_path)

    else:
        print (new_note_path)

def new_note_example ():
    # TODO: There's no vim mode for this so :Nn won't work to create notes in
    # the example. This should be implemented by completely removing vim mode
    # in new_note() and instead calling a genering id generator from the vim
    # macro definition then creating a new file in the place where the current
    # open file is.

    new_note_path = fu.new_unique_canonical_path(os.path.abspath(path_resolve('./tests/example/notes')))
    ex ('touch ' + new_note_path)
    ex ('xdg-open ' + new_note_path)

def list_notes ():
    folder = os.fsencode(fu.source_notes_dir)
    for fname_os in os.listdir(folder):
        fname = os.fsdecode(fname_os)

        note_f = open(path_cat(fu.source_notes_dir, fname), 'r')
        note_display = fname + ' - ' + note_f.readline()[2:-1] # Remove starting "# " and ending \n
        print (note_display)
        note_f.close()

def search_notes ():
    is_vim_mode = get_cli_bool_opt('--vim')
    args = get_cli_no_opt()

    if args != None and len(args) > 0:
        # args[0] is the snip's name. Also make lowercase to make search case insensitive.
        arg = args[0].lower()

        folder = os.fsencode(fu.source_notes_dir)
        for fname_os in os.listdir(folder):
            fname = os.fsdecode(fname_os)

            note_path = path_cat(fu.source_notes_dir, fname)
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

def generate_common (source=fu.source_files_dir, target=out_dir):
    success = weaver_maybe_build()
    fu.copy_changed(static_dir, target)
    fu.copy_changed(source, path_cat(target, fu.files_dirname))

    return success

def generate ():
    if generate_common():
        ex (f'./bin/weaver generate --static --verbose')

def generate_public ():
    if generate_common():
        ex (f'./bin/weaver generate --static --public --verbose')

def publish ():
    if generate_common(target=public_out_dir):
        ex (f'./bin/weaver generate --static --public --verbose --output-dir {public_out_dir}')
        ex ('rclone sync --fast-list --checksum ~/.cache/weaver/public/ aws-s3:weaver.thrachyon.net/santileortiz/');


def generate_example ():
    example_source = os.path.abspath(path_resolve('./tests/example/'))
    ensure_dir(example_output)

    if generate_common(source=example_source, target=example_output):
        ex (f'./bin/weaver generate --static --verbose --home {example_source} --output-dir {example_output}')

def generate_example_public ():
    example_source = os.path.abspath(path_resolve('./tests/example/'))
    ensure_dir(example_public_output)

    if generate_common(source=example_source, target=example_public_output):
        ex (f'./bin/weaver generate --static --public --verbose --home {example_source} --output-dir {example_public_output}')

def start_static_example ():
    last_pid = store_get (server_pid_pname, default=None)
    if last_pid != None and psutil.pid_exists(last_pid):
        print (f'Server is already running, PID: {last_pid}')
    else:
        pid = ex_bg (f'./nocache_server.py', cwd=example_output)
        print (f'Started server, PID: {pid}')
        print ('Open at: http://localhost:8000/')
        store (server_pid_pname, pid)

def start_static_example_public ():
    last_pid = store_get (server_pid_pname, default=None)
    if last_pid != None and psutil.pid_exists(last_pid):
        print (f'Server is already running, PID: {last_pid}')
    else:
        pid = ex_bg (f'./nocache_server.py', cwd=example_public_output)
        print (f'Started server, PID: {pid}')
        print ('Open at: http://localhost:8000/')
        store (server_pid_pname, pid)


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

def common_build(c_sources, out_fname, use_js, subprocess_test=True):
    global C_FLAGS

    if use_js:
        c_sources += " lib/duktape.c"
    else:
        C_FLAGS += " -DNO_JS"

    if not subprocess_test:
        C_FLAGS += " -DTEST_NO_SUBPROCESS"

    generate_automacros ('automacros.h')

    return ex (f'gcc {C_FLAGS} -o {out_fname} {c_sources} -lm -lrt')

def weaver_build (use_js):
    return common_build ("weaver.c", 'bin/weaver', use_js)

# TODO: Can this be the default weaver snip?, or there are cases where our
# rebuild detection doesn't correctly work and we want to force it to rebuild?
def weaver_maybe_build():
    success = True
    if c_needs_rebuild ('weaver.c', './bin/weaver') or ex (f'./bin/weaver --has-js', echo=False) == 0:
        print ('Building weaver...')
        if weaver_build (True) != 0:
            success = False
        else:
            print()
    return success

def weaver():
    weaver_build (check_js_toggle ())

def psplx_parser_tests():
    subprocess_test = not get_cli_bool_opt ("--no-subprocess")
    
    return common_build ("psplx_parser_tests.c", 'bin/psplx_parser_tests', False, subprocess_test)

def tsplx_parser_tests():
    subprocess_test = not get_cli_bool_opt ("--no-subprocess")

    return common_build ("tsplx_parser_tests.c", 'bin/tsplx_parser_tests', False, subprocess_test)

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
    is_sequence = get_cli_bool_opt('--sequence')
    attach_target = get_cli_arg_opt('--attach')
    is_vim_mode = get_cli_bool_opt('--vim')
    args = get_cli_no_opt()

    if is_sequence and attach_target:
        print ("Only one allowed between: --sequence, --attach")
        return

    identifier = None
    if attach_target:
        cf = fu.canonical_parse(attach_target)
        if not cf.is_canonical:
            print ('Target is not a canonical file name.')
            return

        if fu.file_has_attachment(attach_target):
            print ('Target already has an attachment.')
            return

        identifier = cf.identifier

    if is_sequence:
        identifier = fu.new_identifier()

    files = []
    if args == None:
        separator = '|||'
        files_str = ex(f'zenity --file-selection --multiple --separator=\'{separator}\'', echo=False,ret_stdout=True)
        if len(files_str) > 0:
            files = files_str.split(separator)
    else:
        files = args

    location = []
    original_paths = []
    new_basenames = []
    for i,path in enumerate(files,1):
        if is_sequence and len(files) > 1:
            location = [i]

        error, new_path = fu.file_canonical_rename(path, fu.source_files_dir,
            None, None, identifier, location, None,
            False, False, keep_name=True, original_names=file_original_name_path)

        if error == None:
            original_paths.append(path)
            new_basenames.append(path_basename(new_path))
        else:
            print(error)

    if len(new_basenames) > 0:
        if is_vim_mode:
            result = set()
            for new_basename in new_basenames:
                canonical_name = fu.canonical_parse(new_basename)
                result.add(f'{canonical_name.identifier}')
            print(', '.join(result))

        else:
            result = []
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
    tests_python.create_files_from_map('bin/test', file_map)


def input_str(win):
    s = ''
    ch = None
    while True:
        ch = chr(win.getch())

        if ch == '\n':
            break

        s += ch
    return s

def input_char(win):
    while True:
        ch = win.getch()
        if ch in range(32, 127):
            break
        time.sleep(0.05)
    return chr(ch)

def get_scanner_names():
    s = ex("scanimage -f '%d'", echo=False, quiet=True, ret_stdout=True)

    if s:
        return s.split('\n')
    else:
        return []

def new_scan(path, resolution=300, device=None):
    device_arg = ''
    if device != None:
        device_arg = f"-d '{device}'"

    error = None
    cmd = f"scanimage {device_arg} --resolution {resolution} --format jpeg --output-file '{path}'"

    status = ex(cmd, echo=False, quiet=True)
    if status != 0:
        error = True

    return error, cmd

def get_type_config(target_type):
    # TODO: This should be part of a configuration for each type, most likely
    # coming from some .tsplx file. This configuration will describe details of
    # how types are handled.
    target_file_dir = None
    target_data_file = None

    # The TSPLX stub must not include the identifier assignment portion as it
    # may be filled later when resolving it.
    # :identifier_stub_resolution
    tsplx_stub = None

    if target_type == "print-book":
        target_file_dir = "book-photos"
        target_data_file = "book_library_santiago.tsplx"
        tsplx_stub = 'print-book["<++>", q("<++>")];\n'

    elif target_type == "transaction":
        target_file_dir = 'invoices'
        target_data_file = "digitized_invoices.tsplx"
        tsplx_stub = textwrap.dedent('''\
            transaction["<+date+>",<+value+>,"MXN",q("<+from+>"),q("<+to+>")]{
              file <+files+>;
            };
            ''')

    elif target_type == "exchange":
        target_file_dir = 'invoices'
        target_data_file = "digitized_invoices.tsplx"
        tsplx_stub = textwrap.dedent('''\
            exchange["<+date+>","<+exchanger+>",<+from-value+>,"<+from-currency+>",q("<+from+>"),<+to-value+>,"USD",q("<+to+>")]{
              "San Francisco 2022";
              file <+files+>;
            };
            ''')

    elif target_type == "bus-ticket":
        target_file_dir = 'ticket'
        target_data_file = "ticket.tsplx"
        tsplx_stub = textwrap.dedent('''\
            ticket {
              transport-means "bus";
              file <+files+>;
            };
            ''')

    elif target_type == "plane-ticket":
        target_file_dir = 'ticket'
        target_data_file = "ticket.tsplx"
        tsplx_stub = textwrap.dedent('''\
            ticket {
              from "<++>";
              to "<++>";
              transport-means "airplane";
              file <+files+>;
            };
            ''')

    elif target_type in ["fm3", "passport"]:
        target_file_dir = 'identifications'
        target_data_file = "identifications.tsplx"
        tsplx_stub = textwrap.dedent(f'''\
            {target_type} {{
              person <+person+>;
              file <+files+>;
              issuing-date "<+date+>";
              expiration-date "<+date+>";
            }};
            ''')

    elif target_type in ["person", "establishment"]:
        tsplx_stub = textwrap.dedent(f'''\
            {target_type}["<+name+>"]{{
              file <+files+>;
            }};
            ''')


    if target_data_file == None:
        target_data_file = f"{target_type}.tsplx"


    if tsplx_stub == None:
        tsplx_stub = textwrap.dedent(f"""\
            {target_type} {{
              file <+files+>;
            }};
            """)

    return target_file_dir, target_data_file, tsplx_stub

def resolve_stub(target_type, tsplx_stub=None, identifier=None, files=None):
    # TODO: Don't pass the stubs as a string template. Should better pass them
    # in a format that node_to_object_string() understands, and use that as
    # serialization mechanism.

    if tsplx_stub.find("<+files+>") > 0:
        tsplx_stub = tsplx_stub.replace("<+files+>", "; ".join(files))

    # I've been tempted to using a shorthand notation where apply the following
    # default
    #
    # if identifier==None and len(files) == 1:
    #     identifier = files[0]
    #     files = []
    #
    # But I'm still not 100% sold on it. I've been currently using it for the
    # "print-book" type but doing so binds the unique entity ID with a single
    # flie ID. Which is fine when there is one possible file for a thing, or
    # all files of the thing are always refered to as a group (maybe multiple
    # files, but all under the same ID (different extensions, names, prefixes
    # or locations).
    #
    # The problem comes when we use this notation, and then at a later point we
    # decide we really want to associate multiple files to the entity. At this
    # point this ID can be referenced from a note with the [[]] notation. There
    # are 2 resolution strategies:
    #
    # - Leave the entity's ID and all note references unchanged. Then rename
    #   the preexisting file (or file group) to a new ID then add this new ID
    #   together with the ID for the incoming file to the "files" property.
    #
    # - Leave all file names unchanged. Then create a new ID for the entity and
    #   replace all note references to point to the new entity ID. Add the old
    #   entity ID together with the ID for the incoming file to the "files"
    #   property.
    #
    # :identifier_stub_resolution
    if identifier != None and isinstance(identifier, str):
        tsplx_stub = f'{identifier} = {tsplx_stub}'

    return tsplx_stub

def append_instance(target_data_file, tsplx_data):
    ensure_dir(path_basename(target_data_file))

    file_existed = True if path_exists(target_data_file) else False
    with open(target_data_file, "+a") as data_file:
        if file_existed:
            data_file.write('\n')
        data_file.write(tsplx_data)

def scan():
    show_help = get_cli_bool_opt ("--help")
    target_file_dir = get_cli_arg_opt ("--directory")
    target_data_file = get_cli_arg_opt ("--data-file")
    resolution = int(get_cli_arg_opt ("--resolution", default="300"))
    rest_args = get_cli_no_opt()

    target_type = None
    if rest_args != None and len(rest_args) == 1:
        target_type = rest_args[0]

    if show_help or (target_type == None and target_file_dir == None):
        print ("usage:")
        print (f"  ./pymk.py {get_function_name()} TARGET_TYPE")
        print (f"  ./pymk.py {get_function_name()} --directory TARGET_DIRECTORY")
        return

    # If we don't pass a device to 'scanimage' and leave it to figure it out,
    # scanning takes significantly longer (10s per scan). To make things faster
    # we list all devices, choose one, then pass in all subsequent commands.
    scanner_name = None
    scanner_names = get_scanner_names()
    if len(scanner_names) == 0:
        print(ecma_red("error:") + " No scanners detected.")
        return
    elif len(scanner_names) == 1:
        scanner_name = scanner_names[0]

    if target_type != None:
        target_file_dir, target_data_file, tsplx_stub = get_type_config(target_type)
    target_file_dir, target_data_file = fu.get_target(target_type, target_file_dir, target_data_file)

    help_str = textwrap.dedent("""\
        Commands:
          n new scan (new instance)
          d new document (in same instance)
          p scan next page in document

          e end document section
          w end instance (optional, n and q imply this)

          h help
          q quit
        """)

    mfd = fu.MultiFileDocument(target_file_dir)
    ensure_dir(target_file_dir)

    win = curses.initscr()
    curses.start_color()
    win.scrollok(1)
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_RED, -1)

    win.addstr(f'Storing scans in: {target_file_dir}\n')
    win.addstr(f'Appending data stubs to: {target_data_file}\n')

    # There are multiple devices, prompt the user to choose one.
    if scanner_name == None:
        win.addstr(f'\nAvailable devices:\n')
        devices_list = [win.addstr(f'  {i}) {d}\n') for i,d in enumerate(scanner_names,1)]
        while scanner_name == None:
            win.addstr('> ')

            idx_str = input_str(win)
            if idx_str.isnumeric():
                idx = int(idx_str) - 1
                if idx >= 0 and idx < len(scanner_names):
                    scanner_name = scanner_names[idx]

            if scanner_name == None:
                win.addstr(f'Invalid device index.\n')

    win.addstr(f'Using device: {scanner_name}\n\n')
    win.addstr(help_str)

    if target_type != None:
        instance_files = None
        tsplx_data = None

    while True:
        win.addstr('> ')
        c = input_char(win)
        win.addstr('\n')

        if c.lower() == 'n':
            path_to_scan = fu.mfd_new(mfd)
            error, output = new_scan (path_to_scan, resolution, device=scanner_name)

            if not error and target_type != None:
                assert target_data_file != None

                # If there was a tsplx data to be written, write it out. This
                # writes out the OLD tsplx data, because we're going to be
                # createing a new one, we know this one is done.
                if tsplx_data != None:
                    append_instance(target_data_file, tsplx_data)

                # Initialize a the new tsplx data
                instance_files = [mfd.identifier]
                tsplx_data = resolve_stub(target_type,
                        tsplx_stub=tsplx_stub,
                        identifier=None,
                        files=instance_files)

                win.addstr(f'TSPLX:\n{tsplx_data}')

            win.addstr(f'{output}\n')
            win.refresh()

            if error == None:
                win.addstr(f'New: {path_basename(path_to_scan)}\n')
            else:
                win.addstr('error:', curses.color_pair(1))
                win.addstr(' could not scan page\n')

        elif c.lower() == 'd':
            if target_type != None:
                path_to_scan = fu.mfd_new(mfd)
                error, output = new_scan (path_to_scan, resolution, device=scanner_name)

                if not error:
                    instance_files.append(mfd.identifier)
                    tsplx_data = resolve_stub(target_type,
                            tsplx_stub=tsplx_stub,
                            identifier=None,
                            files=instance_files)

                    win.addstr(f'TSPLX:\n{tsplx_data}')

                win.addstr(f'{output}\n')
                win.refresh()

            else:
                win.addstr('error: new document in instance only makes sense when specifying a type, right now no stubs are being generated.\n')

        elif c.lower() == 'p':
            path_to_scan = fu.mfd_new_page (mfd)
            error, output = new_scan (path_to_scan, resolution, device=scanner_name)

            if target_type != None:
                tsplx_data = resolve_stub(target_type,
                        tsplx_stub=tsplx_stub,
                        identifier=None,
                        files=instance_files)

                win.addstr(f'TSPLX:\n')
                win.addstr(f'{tsplx_data}\n')

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

        elif c.lower() == 'w':
            if target_type != None:
                if tsplx_data != None:
                    append_instance(target_data_file, tsplx_data)

                    win.addstr(f'TSPLX:\n')
                    win.addstr(f'{tsplx_data}\n')
                    tsplx_data = None

                else:
                    win.addstr('error: no instance data to be written out, did nothing.\n')

            else:
                win.addstr('error: new document in instance only makes sense when specifying a type, right now no stubs are being generated.\n')

        elif c.lower() == 'h':
            win.addstr(help_str)

        elif c.lower() == 'q':
            if target_type != None and tsplx_data != None:
                append_instance(target_data_file, tsplx_data)
            break

    curses.endwin()

def files_sort():
    """
    The --ids option expects a comma separated list of identifiers, as a single
    argument.

    If no ids are passed, we open a temporary file with canonical names of the
    passed directory, one per line. The ordering of files should be edited in
    it, then after saving and closing, the new ordering will be applied.

    Numbering starts with 1. If a line is deleted from the file, the
    correspoding file will be assigned index 0. Which essentially marks it as
    unsortable and places at the beginning of the directory.
    """

    # TODO: This interface is not idempotent. Calling this on a directory then
    # closing without changing anything, may trigger changes. This is because
    # initially 0 index files are shown, but we require them to be deleted to
    # get 0 index again. I don't really like this...
    #
    # Probably a better behavior is to leave all 0-index files _at the
    # beginning_ be left unsorted. We can NOT have 0-index files always be
    # unsorted because the point of this is to fix the ordering of unsortable
    # files, which are 0-indexed. The simplest behavior is to cut a 0-index
    # file form the start, then paste it in it's position. Having to remove the
    # leading 0_ for it to work, is hard to teach, and hard to remember. Also,
    # not all editors have column editing/multi-cursors (or users don't know
    # about this) which may slow them down.

    dry_run = get_cli_bool_opt ("--dry-run")
    ids = get_cli_arg_opt ("--ids")
    rest_args = get_cli_no_opt()

    directory = None
    if rest_args != None and len(rest_args) == 1:
        directory = os.path.abspath(path_resolve(rest_args[0]))

    if directory == None:
        print ("usage:")
        print (f"  ./pymk.py {get_function_name()} [--ids 'ID1,ID2,...'] DIRECTORY")
        return

    if ids == None:
        file_list = fu.collect_canonical_fnames(directory)

        temp = tempfile.NamedTemporaryFile(mode='w', delete=False)
        name = temp.name
        temp.write('\n'.join([f.fname() for f in file_list]))
        temp.close()

        ex(f'gvim -f {name}')

        maybe_dup_ids = []
        new_order = open(name, 'r')
        for line in new_order:
            cn = fu.canonical_parse(line)
            maybe_dup_ids.append(cn.identifier)

        ids = []
        seen = set()
        for identifier in maybe_dup_ids:
            if identifier not in seen:
                ids.append(identifier)
                seen.add(identifier)
    else:
        ids = [i.strip() for i in ids.split(',')]

    fu.canonical_renumber(directory, ids, verbose=False, dry_run=dry_run);
    print (json.dumps(ids))

def parse_timestamp(timestamp, min_date):
    # NOTE: There are cases where I've seen exiftool report an
    # invalid timestamp of 0000:00:00 00:00:00
    try:
        result = datetime.fromisoformat(timestamp)
    except:
        result = None

    if result != None and min_date != None and min_date > result:
        result = None

    return result

def print_row(columns, column_widths=[26, 13], spacing=2):
    padded_columns = []
    for i, column in enumerate(columns):
        if i < len(column_widths):
            padded_columns.append(column.ljust(column_widths[i]))
        else:
            padded_columns.append(column)
    print((" "*spacing).join(padded_columns))
    print()

def files_date_sort():
    """
    Tries to figure out the chronological order of files, using the file's
    metadata to retrieve the actual creation date.

    This doesn't use the file system's file creation date, this looks _into_
    the file's metadata.
    """

    dry_run = get_cli_bool_opt ("--dry-run")
    default_utc_offset = get_cli_arg_opt ("--default-utc-offset")
    min_date = get_cli_arg_opt ("--minimum-date")
    rest_args = get_cli_no_opt()

    if min_date != None:
        min_date = datetime.fromisoformat(min_date)

    directory = None
    if rest_args != None and len(rest_args) == 1:
        directory = os.path.abspath(path_resolve(rest_args[0]))

    if directory == None:
        print ("usage:")
        print (f"  ./pymk.py {get_function_name()} [--dry-run] [--default-utc-offset +hh:mm] [--minimum-date yyyy-mm-ddThh:mm:ss+hh:mm] DIRECTORY")

        return

    unsortable = []
    assumed_utc = []
    assumed_default_utc_offset = []
    timestamped_paths = []
    for dirpath, dirnames, filenames in os.walk(directory):
        for fname in natsorted(filenames):
            path = path_cat(dirpath, fname)
            if path.lower().endswith(".jpg") or path.lower().endswith(".jpeg") or path.lower().endswith(".heic") or path.lower().endswith(".heif"):
                output = ex(f"exiftool -s -H -u -DateTimeOriginal -OffsetTimeOriginal -GPSDateStamp -GPSTimeStamp -Model '{path}'", ret_stdout=True, echo=False)

                if len(output) > 0:
                    lines = [x for x in output.split('\n')]
                    fields = {x[1]:x[3:] for x in map(lambda x: x.split(), lines)}

                    model = '-'
                    if 'Model' in fields.keys():
                        model = " ".join(fields['Model'])

                    timestamp = None
                    if 'OffsetTimeOriginal' in fields.keys() and 'DateTimeOriginal' in fields.keys():
                        datetime_value = fields['DateTimeOriginal']
                        offset = fields['OffsetTimeOriginal'][0]
                        timestamp = f"{datetime_value[0].replace(':','-')}T{datetime_value[1]}{offset}"

                    elif 'GPSTimeStamp' in fields.keys() and 'GPSDateStamp' in fields.keys():
                        date_value = fields['GPSDateStamp'][0]
                        time_value = fields['GPSTimeStamp'][0]
                        timestamp = f"{date_value.replace(':','-')}T{time_value}+00:00"

                    elif 'DateTimeOriginal' in fields.keys() and default_utc_offset != None:
                        datetime_value = fields['DateTimeOriginal']
                        timestamp = f"{datetime_value[0].replace(':','-')}T{datetime_value[1]}{default_utc_offset}"
                        assumed_default_utc_offset.append(path)

                    elif 'OffsetTimeOriginal' not in fields.keys() and 'DateTimeOriginal' in fields.keys():
                        datetime_value = fields['DateTimeOriginal']
                        print_row([f"{datetime_value[0].replace(':','-')}T{datetime_value[1]}+??:??", model, fname])
                        unsortable.append(path)

                    else:
                        unsortable.append(path)

                    if timestamp != None:
                        parsed_timestamp = parse_timestamp(timestamp, min_date)
                        if parsed_timestamp:
                            timestamped_paths.append((parsed_timestamp, path))
                            print_row([timestamp, model, fname])
                        else:
                            unsortable.append(path)

                else:
                    unsortable.append(path)

            elif path.lower().endswith(".mov"):
                output = ex(f"exiftool -s -H -u -CreateDate -api quicktimeutc '{path}'", ret_stdout=True, echo=False)

                if len(output) > 0:
                    lines = [x for x in output.split('\n')]
                    fields = {x[1]:x[3:] for x in map(lambda x: x.split(), lines)}
                    datetime_value = fields['CreateDate']
                    timestamp = f"{datetime_value[0].replace(':','-')}T{datetime_value[1]}"
                    print_row([timestamp, '-', fname])

                    parsed_timestamp = parse_timestamp(timestamp, min_date)
                    if parsed_timestamp:
                        timestamped_paths.append((parsed_timestamp,path))
                        assumed_utc.append(path)
                    else:
                        unsortable.append(path)

                else:
                    unsortable.append(path)

            elif path.lower().endswith(".mp4"):
                output = ex(f"exiftool -s -H -u -CreateDate -api quicktimeutc '{path}'", ret_stdout=True, echo=False)

                if len(output) > 0:
                    lines = [x for x in output.split('\n')]
                    fields = {x[1]:x[3:] for x in map(lambda x: x.split(), lines)}
                    datetime_value = fields['CreateDate']
                    timestamp = f"{datetime_value[0].replace(':','-')}T{datetime_value[1]}"
                    print_row([timestamp, '-', fname])

                    parsed_timestamp = parse_timestamp(timestamp, min_date)
                    if parsed_timestamp:
                        timestamped_paths.append((parsed_timestamp,path))
                        assumed_default_utc_offset.append(path)
                    else:
                        unsortable.append(path)

                else:
                    unsortable.append(path)

            else:
                unsortable.append(path)

            # At some point I thought getting the file system's creation date
            # could be better than nothing. I'm now almost convinced that's not
            # a good default. It mixes files we are very highly confident are
            # correctly sorted, together with files where we are hoping the
            # file creation date to have survived across moving of the file
            # from different places.
            #
            # The better solution is to mark them as unsortable, then if the
            # user wants, they can go ahead and fix it by hand using
            # files_sort().

    sidecar_paths = None
    if not dry_run:
        identifier_idx = {}
        for i, (dt, path) in enumerate(sorted(timestamped_paths, key=lambda x: x[0]), 1):
            cn = fu.canonical_parse(path_basename(path))
            identifier_idx[cn.identifier] = i
            rename_error, new_path = fu.file_canonical_rename (path,
                None,
                cn.prefix,
                i,
                cn.identifier,
                cn.location,
                cn.name,
                True, dry_run,
                error_if_target_exists=True)

        sidecar_paths = []
        for path in unsortable:
            cn = fu.canonical_parse(path_basename(path))

            # It's possible that there are sidecar files that are not sortable
            # but have the same identifier as a file that is sortable. For
            # example Apple's .AAE files, or JSON files containing image
            # annotations. In such case, we copy the same index that was
            # assigned to the file that is sortable.
            idx = -1 if cn.identifier not in identifier_idx.keys() else identifier_idx[cn.identifier]

            rename_error, new_path = fu.file_canonical_rename (path,
                None,
                cn.prefix,
                idx,
                cn.identifier,
                cn.location,
                cn.name,
                True, dry_run,
                error_if_target_exists=True)

            if idx != -1:
                sidecar_paths.append(path)

    if len(assumed_utc) > 0:
        print()
        print (ecma_yellow('warning:') + ' assuming dates are UTC for:')
        print ('  ' + '\n  '.join(map(lambda x: f"'{x}'", assumed_utc)))

    if len(assumed_default_utc_offset) > 0:
        print()
        print (ecma_yellow('warning:') + ' assume the following files have local dates, using passed UTC offset:')
        print ('  ' + '\n  '.join(map(lambda x: f"'{x}'", assumed_default_utc_offset)))

    if sidecar_paths != None:
        # Remove sidecar files that were sortable in the end so we don't warn about
        # those.
        unsortable = [p for p in unsortable if p not in sidecar_paths]

    if len(unsortable) > 0:
        print('', file=sys.stderr)
        print (ecma_red('error:') + ' could not sort:', file=sys.stderr)
        print ('  ' + '\n  '.join(map(lambda x: f"'{x}'", unsortable)), file=sys.stderr)


def dir_import():
    """
    Recursively iterates into a directory, takes any file with a non canonical
    name and renames it.
    """
    dry_run = get_cli_bool_opt ("--dry-run")
    rest_args = get_cli_no_opt()

    directory = None
    if rest_args != None and len(rest_args) == 1:
        directory = os.path.abspath(path_resolve(rest_args[0]))

    if directory == None:
        print ("usage:")
        print (f"  ./pymk.py {get_function_name()} DIRECTORY")
        return

    dir_len = len(directory)+1

    paths = []
    for dirpath, dirnames, filenames in os.walk(directory):
        for fname in filenames:
            path = path_cat(dirpath, fname)

            canonical_name = fu.canonical_parse(fname)
            if not canonical_name.is_canonical:
                paths.append(path)

    if len(paths)>0:
        errors = ''
        fu.append_empty_line (file_original_name_path)

        for path in paths:
            rename_error, new_path = fu.file_canonical_rename (path,
                    None,
                    None,
                    None,
                    None,
                    [],
                    None,
                    True, dry_run,
                    keep_name=True,
                    original_names=file_original_name_path, error_if_target_exists=True)

            if not rename_error:
                print(path[dir_len:], '->', new_path[dir_len:])
            else:
                errors += rename_error

        if len(errors) > 0:
            print(errors)

def dir_flatten():
    dry_run = get_cli_bool_opt ("--dry-run")
    properties = get_cli_arg_opt ("--properties")
    rest_args = get_cli_no_opt()

    directory = None
    if rest_args != None and len(rest_args) == 1:
        directory = os.path.abspath(path_resolve(rest_args[0]))

    if directory == None:
        print ("usage:")
        print (f"  ./pymk.py {get_function_name()} DIRECTORY")
        return

    dir_len = len(directory)+1

    # Collect files to be flattened
    paths = []
    for dirpath, dirnames, filenames in os.walk(directory):
        # Don't collect top level files as they are already flat.
        if dirpath == directory:
            continue

        for fname in filenames:
            path = path_cat(dirpath, fname)

            # Only collect files already in canonical name. This ensures we
            # have an ID to use when building the metadata.
            canonical_name = fu.canonical_parse(fname)
            if canonical_name.is_canonical:
                paths.append(path)

    property_names = [p.strip() for p in properties.split(',')]
    property_file_content = {p:[] for p in property_names}
    if len(paths)>0:
        for path in paths:
            fname = path_basename(path)
            canonical_name = fu.canonical_parse(fname)
            values = path_dirname(path)[dir_len:].split(os.sep)

            for i,v in enumerate(values):
                if v != '_':
                    property_file_content[property_names[i]].append(f'{canonical_name.identifier} "{v}";')

            if not dry_run:
                os.rename(path, path_cat(directory,fname))
            else:
                print(f'{canonical_name.identifier} = {{')
                for i,v in enumerate(values):
                    if v != '_':
                        print(f'  {property_names[i]} "{v}";')
                print(f'  file-path "{path_cat(directory,fname)}";')
                print('};\n')

    if not dry_run:
        for key,content in property_file_content.items():
            property_file_path = path_cat(fu.data_dir, key + '.tsplx')

            file_existed = True if path_exists(property_file_path) else False
            with open(property_file_path, "+a") as data_file:
                if file_existed:
                    data_file.write('\n')
                data_file.write('\n'.join(content))
                data_file.write('\n')

def tests():
    # TODO: Only build if necessary
    #tsplx_parser_tests()
    #psplx_parser_tests()

    weaver_maybe_build()

    tests_python.tests()

    #ex (f'cat {log}')

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


telegram_api_id_pname = 'telegram_api_id'
telegram_api_hash_pname = 'telegram_api_hash'
def telegram_setup():
    if len(sys.argv) != 4:
        print (f"usage: ./pymk.py {get_function_name()} API_ID API_HASH")
        return

    store (telegram_api_id_pname, int(sys.argv[2]))
    store (telegram_api_hash_pname, sys.argv[3])

# How to use:
#  - Send a sequence of photo albums to the saved messages chat.
#  - Reply to the start message of the sequence with START
#  - Download all pictures with a command like
#     ./pymk.py telegram_download_photos print-book
def telegram_download_photos():
    """
    This downloads all media groups assuming they are all photos. It assigns a
    single identifier to each photo album and sequentialy number pictures in
    it.

    To specify where to stop, it looks for a message with START as content,
    which should be a reply to the oldest message that should be included in
    the full download.
    """

    prefix = get_cli_arg_opt ("--prefix")
    group_by_album = get_cli_bool_opt ("--group-by-album")

    target_file_dir = get_cli_arg_opt ("--directory")
    target_data_path = get_cli_arg_opt ("--data-file")
    rest_args = get_cli_no_opt()
    if rest_args != None and len(rest_args) == 1:
        target_type = rest_args[0]

    if target_type == None:
        print (f"usage: ./pymk.py {get_function_name()} [--directory DIRECTORY] [--data-file PATH] TARGET_TYPE")

    tsplx_stub = None

    if target_type != None:
        if target_type == "print-book":
            target_file_dir = "book-photos"
            target_data_path = "book_library_santiago.tsplx"
            tsplx_stub = 'print-book["<++>", q("<++>")];\n'

        elif target_type == "transaction":
            group_by_album = True
            target_file_dir = 'invoices'
            target_data_path = "digitized_invoices.tsplx"
            prefix = "photo"
            tsplx_stub = textwrap.dedent('''\
                transaction["<++>",<++>,"MXN",q("<++>"),q("<++>")]{
                  file <+ids+>;
                };\n
                ''')

        elif target_type == "menu":
            group_by_album = True
            target_data_path = "establishments.tsplx"
            tsplx_stub = textwrap.dedent('''\
                menu{
                  file <+ids+>;
                };\n
                ''')

        if target_data_path == None and target_type != None:
            target_data_path = target_type + ".tsplx"

        if target_file_dir == None:
            target_file_dir = target_type

        if not target_file_dir.startswith(os.sep):
            target_file_dir = path_cat(fu.source_files_dir, target_file_dir)

        if not target_data_path.startswith(os.sep):
            target_data_path = path_cat(fu.data_dir, target_data_path)

    if target_file_dir == None:
        target_file_dir = fu.data_dir

    print (f'Storing photos in: {target_file_dir}')
    if target_data_path != None:
        print (f'Appending data to: {target_data_path}')
    print()


    # Uses Pyrogram (https://docs.pyrogram.org/) as the Telegram library
    from pyrogram import Client
    import piexif

    api_id = store_get(telegram_api_id_pname)
    api_hash = store_get(telegram_api_hash_pname)

    if api_id == None or api_hash == None:
        print(ecma_red("error:") + " Missing telegram setup use: ./pymk.py telegram_setup")
        return


    final_message_id = None

    tsplx_data = ''
    media_group_id_to_messages = {}
    parent_message_id_to_media_group_ids = {}
    with Client("my_telegram_account", api_id, api_hash) as app:
        for message in app.get_chat_history("me"):
            if message.media != None:
                if message.media_group_id == None:
                    media_group_id_to_messages["msg" + str(message.id)] = [message]

                else:
                    if message.media_group_id not in media_group_id_to_messages.keys():
                        media_group_id_to_messages[message.media_group_id] = []
                    media_group_id_to_messages[message.media_group_id].append(message)

                # TODO: Whenever there's a picture that's a reply to another
                # one, assume it's in the same media group. This is useful for
                # cases where the user forgets a picture, before submitting the
                # album. Users can still "add into the album", even though
                # Telegram doesn't allow this.
                next_reply = message
                next_reply_id = message.reply_to_message_id
                while next_reply_id != None:
                    next_reply = app.get_messages("me", next_reply_id)
                    next_reply_id = next_reply.reply_to_message_id

                if next_reply.id not in parent_message_id_to_media_group_ids.keys():
                    parent_message_id_to_media_group_ids[next_reply.id] = []

                custom_id = message.media_group_id
                if message.media_group_id == None:
                    custom_id = "msg" + str(message.id)
                parent_message_id_to_media_group_ids[next_reply.id].append(custom_id)

            elif message.text == "START":
                final_message_id = message.reply_to_message_id;

            if message.id == final_message_id:
                break

        # All messages repreenting elements inside an album are parents in that
        # they are not a reply to other message, but they are already included
        # in the media group so we remove them from the dictionary of parent
        # messages. In Telegram, all replies to messages inside a media group
        # actually reply to the first message in the media group.
        for _, messages in media_group_id_to_messages.items():
            minimum_id = None
            for message in reversed(messages):
                if message.id in parent_message_id_to_media_group_ids.keys():
                    minimum_id = message.id
                    break

            for message in messages:
                if message.id != minimum_id:
                    parent_message_id_to_media_group_ids.pop(message.id, None)

        #print (media_group_id_to_messages.keys())
        #print (parent_message_id_to_media_group_ids)

        # If we don't group by album, this takes all images in a reply tree and
        # merges them together into the same message group.
        message_groups = []
        for _, custom_ids in parent_message_id_to_media_group_ids.items():
            instance_file_groups = []
            for custom_id in custom_ids:
                if group_by_album:
                    instance_file_groups.append(media_group_id_to_messages[custom_id])
                else:
                    instance_file_groups += media_group_id_to_messages[custom_id]

            if group_by_album:
                message_groups.append(instance_file_groups)
            else:
                message_groups.append([instance_file_groups])

        for message_group in message_groups:
            thing_identifiers = []
            if not group_by_album:
                identifier = fu.new_identifier()
                thing_identifiers.append(identifier)

            for messages in message_group:
                if group_by_album:
                    identifier = fu.new_identifier()
                    thing_identifiers.append(identifier)

                for i, message in enumerate(reversed(messages), 1):

                    location = [i] if len(messages) > 1 else []

                    _, new_fname = fu.new_canonical_name(identifier=identifier, location=location, extension="jpg", prefix=prefix)
                    fpath = path_cat(target_file_dir, new_fname)
                    app.download_media(message, fpath)
                    print(fpath)

                    # Images returned by Telegram don't have any Exif data. We
                    # embed the message's timestamp into it.

                    # Use the message's datetime to set the EXIF timestamp which is
                    # usually the local time. Even though Telegram stores message
                    # timestamps as in UNIX epoch format, pyrogram translates that
                    # using the system's timezone but returns a timezone unaware
                    # object, we make it timezone aware ourselves.
                    exif_dict = piexif.load(fpath)

                    timezone_aware_date = message.date.replace(tzinfo=datetime.utcnow().astimezone().tzinfo)
                    exif_dict["0th"][piexif.ImageIFD.DateTime] = timezone_aware_date.strftime("%Y:%m:%d %H:%M:%S")
                    no_colon = timezone_aware_date.strftime("%z")
                    exif_dict["Exif"][piexif.ExifIFD.OffsetTime] = no_colon[:3] + ":" + no_colon[3:]

                    exif_bytes = piexif.dump(exif_dict)
                    piexif.insert(exif_bytes, fpath)

            if tsplx_stub.find("<+ids+>") >= 0:
                tsplx_data += tsplx_stub.replace("<+ids+>", "; ".join(thing_identifiers))
            elif not group_by_album:
                tsplx_data += identifier + " = " + tsplx_stub
            else:
                # TODO: Here the default should be to add it to the "file"
                # property. We don't do it right now because we're using string
                # replacement instead of a node representation.
                print(ecma_red("error:") + " Default multiple id setting is not implemented.")

    with open(target_data_path, "+a") as data_file:
        data_file.write('\n')
        data_file.write(tsplx_data)

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

    with open(path_cat(fu.data_dir, 'person.tsplx'), 'w+') as target:
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

    with open(path_cat(fu.data_dir, 'secure.tsplx'), 'w+') as target:
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

    with open(path_cat(fu.data_dir, 'my-playlists.tsplx'), 'w+') as target:
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

    with open(path_cat(fu.data_dir, 'santileortiz-fibery.tsplx'), 'w+') as target:
        target.write(res)

builtin_completions = ['--get_build_deps']
if __name__ == "__main__":
    # Everything above this line will be executed for each TAB press.
    # If --get_completions is set, handle_tab_complete() calls exit().
    handle_tab_complete ()

    pymk_default()
