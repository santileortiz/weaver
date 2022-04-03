import sys, subprocess, os, ast, shutil, platform, re, json, pickle, zipfile, shlex

import textwrap
import importlib.util, inspect, pathlib, filecmp
from itertools import permutations
from collections import namedtuple

try:
    # Used by pretty_dict()
    from natsort import natsorted
except:
    natsorted = sorted

from enum import Enum

"""
!!!!!!IMPOTRANT!!!!!
The idea of this library is to make easy to call python functions located in
the file pymk.py through the command line. Every function created in that file
is called a 'snip' (short for code snippet). Snips get the following
functionality automatically:

    - Calling './pymk.py my_snip' calls my_snip
    - Automatic TAB completion.
    - For snips that call gcc through ex(), we can automatically guess the
      package names that provide libraries used in the compiler flags. This
      works for deb and rpm packages.

To be able to provide these we need to call snip functions in a dry_run mode that
won't have side effects besides updating several global variables in this
module. For this reason when editing/creating a snip function the user has to
have in mind it may be called at unexpected times but ONLY in dry_run
mode.

If a snip calls functions with side effects besides the ones in this module,
the user has to determine if the call is happening in dry run mode by checking
the g_dry_run global variable. If the call is happening in dry run mode,
external calls with side effect must be skipped. It would look something like:

    def my_snip():
        global g_dry_run
        if not g_dry_run:
            <code with side effects>

Functions with side effects internal to this module do this by default. A
function that doesn't must be reported as a bug.
"""

# TODO: Implement the functionality mentioned above statically so we don't need
# a dry run mode and the user can write scripts with more confidence that
# nothing strange will happen. This would also simplify the internal logic of
# the code. At least TAB complete should be implementable this way, the
# dependency computation, I'm not sure.

def get_functions():
    """
    Returns a list of functions defined in the __main__ module, it's a list of
    tuples like [(f_name, f), ...].

    NOTE: Functions imported like 'from <module> import <function>' are
    included too.
    """
    # Why does this return a list of tuples and not a map? I guess function
    # names can be repeated? under which circumstances?
    return inspect.getmembers(sys.modules['__main__'], inspect.isfunction).copy()

def get_user_functions():
    """
    Like get_functions() removing the ones defined in this file.
    """
    keys = globals().copy().keys()
    return [(m,v) for m,v in get_functions() if m not in keys]

def get_function_name(nested_frames=1):
    return inspect.getouterframes(inspect.currentframe())[nested_frames].frame.f_code.co_name

def user_function_exists(name):
    fun = None
    for f_name, f in get_user_functions():
        if f_name == name:
            fun = f
            break

    return fun != None

def call_user_function(name, dry_run=False):
    fun = None
    for f_name, f in get_user_functions():
        if f_name == name:
            fun = f
            break

    global g_dry_run
    if dry_run:
        g_dry_run = True

    fun() if fun else print ('No function: '+name)

    # Why do we do this? it seems highly suspicious...
    if dry_run:
        g_dry_run = False
    return

def is_macos():
    return platform.system() == 'Darwin'

def is_windows():
    return platform.system() == 'Windows'

def is_linux():
    return platform.system() == 'Linux'

def get_completions_path():
    completions_path = ''

    if is_macos():
        if ex('which brew', echo=False, no_stdout=True) == 0:
            prefix = ex('brew --prefix', ret_stdout=True, echo=False)
            completions_path = path_cat(prefix, '/etc/bash_completion.d/pymk.py')
    elif is_linux():
        completions_path = '/usr/share/bash-completion/completions/pymk.py'

    return completions_path

def completions_exist ():
    completions_path = get_completions_path()
    if completions_path == '' or not path_exists(completions_path):
        return False
    else:
        return True

# Options can have several syntaxes, like ls has -a and --all. These are stored
# as a comma separated list in the key of cli_completions, here we choose just
# one to recommend when using tab completions.
def recommended_opt (s):
    opt_lst = s.split(',')
    res = None
    for s in opt_lst: 
        if s.startswith('--'):
            res = s
    if res == None:
        res = opt_lst[0]
    return res

def is_interactive():
    return os.isatty(sys.stdin.fileno())

builtin_completions = []
cli_completions = {}
cli_bool_options = set()
cli_arg_options = set()
def handle_tab_complete ():
    """
    Handles command line options used by tab completion.
    The option --get_completions exits the script after printing completions.
    """
    global cli_completions, builtin_completions

    # Check that the tab completion script is installed
    if is_interactive():
        if get_cli_bool_opt('--install_completions'):
            print ('Installing tab completions...')
            ex (f'cp mkpy/pymk.py {get_completions_path()}')
            exit ()

        elif not completions_exist ():
            if is_macos():
                warn('Tab completions not installed:')
                print(' 1) Install brew (https://brew.sh/)')
                print(' 2) Install bash-completion with "brew install bash-completion")')
                print(' 3) Run "sudo ./pymk.py --install_completions" to install Pymk completions.\n')
            elif is_linux():
                warn('Tab completions not installed:')
                print(' Use "sudo ./pymk.py --install_completions" to install them\n')
            return

    # Add the builtin tab completions the user wants
    if len(builtin_completions) > 0:
        [get_cli_bool_opt (c) for c in builtin_completions]

    data_str = get_cli_arg_opt('--get_completions', unique_option=True)
    if data_str != None:
        data_lst = data_str.split(' ')
        curs_pos = int(data_lst[0])
        line = data_lst[1:]
        if len(line) == 1: line.append('')

        match_found = False
        for opt,vals in cli_completions.items():
            if line[-2] in opt.split(',') and vals != None:
                match_found = True
                print (' '.join(vals))

        if not match_found:
            f_names = [s for s,f in get_user_functions()]
            print (' '.join(f_names))

            if line[-1] == '-':
                def_opts = [recommended_opt(s) for s in cli_completions.keys()]
                print (' '.join(def_opts))
            elif line[-1] != '':
                def_opts = []
                for s in cli_completions.keys(): def_opts += s.split(',')
                print (' '.join(def_opts))
        exit ()

def get_cli_arg_opt (opts, values=None, unique_option=False, default=None):
    """
    Parses sys.argv looking for option _opt_ and expects an argument after it.

    If _opt_ is found in argv and it has an argument after it, it returns the
    value of the argument as a string.
    If a list of strings is passed in _values_ , we check that the argument is
    present in the list, if it isn't None is returned as error.
    If _opt_ is not found in argv the return value depends on the value of
    _default_, if it's different to None _default_ is casted to string and
    returned, otherwise None is returned.

    When unique_option is True then _opt_ must be the only option used.

    NOTE: This always return a string. Casts must be done by the user.
    """
    global cli_completions

    global cli_arg_options
    cli_arg_options.add(opts)

    res = None
    i = 1
    if values != None: has_argument = True
    cli_completions[opts] = values

    # TODO: Right now a missing argument is only detected if _opt_ is the last
    # value of argv. Something like ./pymk.py --opt1 --opt2 will return
    # '--opt2' as argument of --opt1. How bad is this?
    while i<len(sys.argv):
        if sys.argv[i] in opts.split(','):
            if i+1 >= len(sys.argv):
                print ('Missing argument for option '+opt+'.');
                if values != None:
                    print ('Possible values are [ '+' | '.join(values)+' ].')
                return
            else:
                res = sys.argv[i+1]
                break
        i = i+1

    # TODO: I'm thinking unique_option is not that useful. Probably remove it.
    if unique_option and res != None:
        if len(sys.argv) != 3:
            print ('Option '+opt+' receives no other option.')
            return

    if res == None and default != None:
        res = str (default)

    if values != None and res != None:
        if res not in values:
            print ('Argument '+res+' is not valid for option '+opt+',')
            if values != None:
                print ('Possible values are: [ '+' | '.join(values)+' ].')
            return
    return res

def get_cli_bool_opt(opts, has_argument=False, unique_option=False):
    """
    Parses sys.argv looking for option _opt_.

    If _opt_ is found, returns True, otherwise it returns False.

    When unique_option is True then _opt_ must be the only option used.
    """
    global cli_completions

    global cli_bool_options
    cli_bool_options.add(opts)

    res = None
    i = 1
    while i<len(sys.argv):
        if sys.argv[i] in opts.split(','):
            res = True
        i = i+1

    if unique_option and res != None:
        if len(sys.argv) != 2:
            print ('Option '+opt+' receives no other option.')
            return

    return res

def get_cli_persistent_toggle(name, enable, disable, default):
    if get_cli_bool_opt(disable):
        store(name, False)

    if get_cli_bool_opt(enable):
        store(name, True)

    return store_get(name, default)

# Deprecated
# This implementation includes the function's name when there is no option
# starting with '-', but when there is, then the function name is not returned.
# This is broken because now callers have to use diferent indices in the array
# depending on the presence of - options. Instead assume this will always be
# called from within a snip and trim the first value by default. That's what
# the new get_cli_no_opt() does.
#
#def get_cli_rest ():
#    """
#    Returns an array of all argv values that are after options starting with '-'.
#    """
#    i = 1;
#    while i<len(sys.argv):
#        if sys.argv[i].startswith ('-'):
#            if len(sys.argv) > i+1 and not sys.argv[i+1].startswith('-'):
#                i = i+1
#        else:
#            return sys.argv[i:]
#        i = i+1
#    return None

def get_cli_no_opt ():
    """
    Returns an array of all argv values that are after options processed by
    get_cli_arg_opt() or get_cli_bool_opt().

    This assumes the calling command line is always something like

    ./pymk.py <snip-name> [OPTS] [REST]

    The returned array will contain REST as a list. Don't use this if there is
    any chance the calling command can be different (for example a missing snip
    name). I have yet to come accross a use case where this could be an issue.

    CAUTION: Always make the call to this function be the last one in a
    sequence of calls to get_cli_* functions.
    """
    global cli_arg_options
    global cli_bool_options

    i = 2;
    while i<len(sys.argv):
        if sys.argv[i].startswith ('-'):
            if sys.argv[i] in cli_arg_options and len(sys.argv) > i+1:
                i += 2
            else:
                if sys.argv[i] in cli_arg_options and len(sys.argv) <= i+1:
                    # TODO: This doesn't detect all the cases where someone may
                    # forget to pass the argument.
                    print (f'Missing argument for CLI parameter: {sys.argv[i]}')

                elif sys.argv[i] not in cli_bool_options:
                    print (f'Unknown CLI parameter: {sys.argv[i]}')

                i += 1
        else:
            return sys.argv[i:]
    return None

def err (string, **kwargs):
    print ('\033[1m\033[91m{}\033[0m'.format(string), **kwargs)

def ok (string, **kwargs):
    print ('\033[1m\033[92m{}\033[0m'.format(string), **kwargs)

def pkg_config_libs (packages):
    return ex ('pkg-config --libs ' + ' '.join(packages), ret_stdout=True)

def pkg_config_includes (packages):
    return ex ('pkg-config --cflags ' + ' '.join(packages), ret_stdout=True)

ex_cmds = []
g_dry_run = False
g_echo_mode = False

# TODO: Is this useful? maybe just ask the user to set the global variable
# :global_variable_setters
def set_dry_run():
    global g_dry_run
    g_dry_run = True

# :global_variable_setters
def set_echo_mode():
    global g_echo_mode
    g_echo_mode = True

# TODO: ex_escape() seems very useless. I never remember it exists, it's
# tedious to wrap the content of an ex command with it and if we have variables
# we actually want to replace this will escape them too. The use case for this
# is that if the command uses { and } a lot we don't want to have to replace
# each occurence with {{ and }}, as this makes it complicated to take the
# string and run it in it's normal context. A better approach is to have ex
# receive an argument that defines how to identify variables maybe something
# like
#
#   ex ("""awk 'BEGIN {print "A %{REPLACE_VAR}%"} {print $0} file""", escs='%{', esce='}%')
#
# or
#
#   ex ("""awk 'BEGIN {print "A %{REPLACE_VAR}%"} {print $0} file""", esc='%')
#
# This would escape all occurences of { and }, and replace %{ and }% for { and
# } respectiveley. I'm leaning towards the 2nd option because it's more concise
# and easy to remember. Note that I use %{ }% not %{ %} like the \ character
# works in C strings, the reason for this is that we can still grep for
# {REPLACE_VAR} and find where the variable is being used.
#
# Also, the new echo mode allows getting the resolved commands without running
# them. Maybe this makes it not that bad to replace all {{ and }}?. The thing
# is I normally develop the awk script in the console, then copy paste it to a
# pymk script, I expect to add variables not have to replace each { and }. This
# also applies for C code that may be present in a command. Let's wait and see
# if I see cases where I need it.
def ex_escape (s):
    return s.replace ('\n', '').replace ('{', '{{').replace ('}','}}')

def ex_bg (cmd, echo=True, cwd=None, log=None):
    """
    This does not invoke a full wrapping shell, this means shell syntax doesn't
    work here. IO redirection using >, >>, <, << or | will not work. Chaining
    multiple commands using ; or using & to run a command in the background
    will fail too.

    Rationale:
    ---------
    When running some command from a script in the background, we most likely
    want the PID of the executed process so we can interact with it (kill it,
    query if it's running, or if it terminated, detect if startup failed etc.).
    For this, we can't wrap the process in a shell, if we did, it would be
    impossible to detect any of these failure states and we would end with a
    zombie shell process.

    Even though this adds some limitations in the types of commands that can be
    passed, it forces users to shape the commands in such a way that will be
    more convenient for automation in the long term. Avoiding hard to debug
    issues caused by the hidden layer of the wrapper shell.

    I don't think it makes sense to have a function that runs a command in the
    background and does use a wrapping shell. This would be equivalent to using
    ex() and appending & to the command string. The process is run in the
    background and we lose most of the ability to monitor and control it.
    """
    global g_dry_run
    global g_echo_mode

    ex_cmds.append(cmd)
    if g_dry_run:
        return

    if echo or g_echo_mode: print (cmd)

    if g_echo_mode:
        return

    if log == None:
        redirect = open(os.devnull, 'wb')
    else:
        redirect = open(log, 'wb')

    # NOTE: Passing cmd as a string does not work when shell=False, and we want
    # shell=False to disable the wrapping shell. We use shlex's split to
    # correctly compute the parameters, even when there are spaces inside
    # quoted strings or escaped space characters.
    process = subprocess.Popen(shlex.split(cmd), shell=False, stdout=redirect, stderr=redirect, cwd=cwd)

    redirect.close()
    return process.pid

# TODO: Is there a way to do this in python without calling a shell?
def ex_bg_kill (pid):
    if is_windows():
        ex(f'taskkill /F /PID {pid}')
    else:
        ex(f'kill {pid}')

def ex (cmd, no_stdout=False, no_stderr=False, quiet=False, ret_stdout=False, echo=True, cwd=None):
    global g_dry_run
    global g_echo_mode

    ex_cmds.append(cmd)
    if g_dry_run:
        return

    if echo or g_echo_mode: print (cmd)

    if g_echo_mode:
        return

    # Somewhere I needed to also silence stderr when returning stdout so I was
    # passing stderr=open(os.devnull, 'wb') to subprocess.check_output. The
    # following is done just to be backward compatible with that, but really I
    # should pass no_stderr=True wherever this was necessary, and not assume we
    # want to silence stderr when returning stdout.
    # TODO: Check all usages of ex() where I pass ret_stdout=True. What I said
    # above, does it really make sense?, is it really a better API?. Maybe a
    # better idea is to remove the ret_stdout parameter and instead create a
    # new function that can be called like this:
    #   return_code, stdout_str, stderr_str = ex_quiet(...)
    if ret_stdout:
        quiet = True

    stdout_redirect = open(os.devnull, 'wb') if no_stdout or quiet else None
    stderr_redirect = open(os.devnull, 'wb') if no_stderr or quiet else None

    if not ret_stdout:
        return subprocess.call(cmd, shell=True, stdout=stdout_redirect, stderr=stderr_redirect, cwd=cwd)
    else:
        result = ""
        try:
            result = subprocess.check_output(cmd, shell=True, stderr=stderr_redirect, cwd=cwd).decode().strip ()
        except subprocess.CalledProcessError as e:
            pass
        return result

# TODO: Rename this because it has the same name as one of the default logging
# functions in python.
def info (s):
    # The following code can be used to see available colors
    #for i in range (8):
    #    print ("\033[0;3" + str(i) + "m\033[K" + "HELLO" + "\033[m\033[K", end=' ')
    #    print ("\033[0;9" + str(i) + "m\033[K" + "HELLO" + "\033[m\033[K", end=' ')
    #    print ("\033[1;9" + str(i) + "m\033[K" + "HELLO" + "\033[m\033[K", end=' ')
    #    print ("\033[1;3" + str(i) + "m\033[K" + "HELLO" + "\033[m\033[K")

    #for i in range (16, 232):
    #    print (str(i) + ": \033[1;38;5;" + str(i) + "m\033[K" + "HELLO" + "\033[m\033[K")

    color = '\033[1;38;5;177m\033[K'
    default_color = '\033[m\033[K'
    print (color+s+default_color)

def warn (s):
    color = '\033[1;33m\033[K'
    default_color = '\033[m\033[K'
    print (color+s+default_color)

def ecma_red(s):
 return f"\033[1;31m\033[K{s}\033[m\033[K"

def ecma_green(s):
 return f"\033[1;32m\033[K{s}\033[m\033[K"

def ecma_yellow(s):
 return f"\033[1;33m\033[K{s}\033[m\033[K"

def ecma_blue(s):
 return f"\033[1;34m\033[K{s}\033[m\033[K"

def ecma_magenta(s):
 return f"\033[1;35m\033[K{s}\033[m\033[K"

def ecma_cyan(s):
 return f"\033[1;36m\033[K{s}\033[m\033[K"

def ecma_white(s):
 return f"\033[1;37m\033[K{s}\033[m\033[K"

def ecma_bold(s):
 return f"\033[1m\033[K{s}\033[m\033[K"

def ecma_code(s, code):
 """Code is an integer between 16 and 232"""
 return f"\033[1;38;5;{code}m\033[K{s}\033[m\033[K"

def ecma_code_f(code):
    def ecma_f(s):
        return ecma_code(s, code)
    return ecma_f

def pickle_load(fname):
    with open (fname, 'rb') as f:
        return pickle.load(f)

def pickle_dump(obj, fname):
    with open (fname, 'wb') as f:
        pickle.dump(obj, f)

def py_literal_load(fname):
    with open (fname, 'r') as f:
        return ast.literal_eval(f.read())

def py_literal_dump(obj, fname):
    with open (fname, 'w') as f:
        f.write(str(obj))

def json_load(fname):
    with open (fname, 'r') as f:
        return json.load(f)

def json_dump(obj, fname):
    with open (fname, 'w') as f:
        return json.dump(obj, f)

def pretty_dict(d, title=None):
    s = ''

    if title != None:
        s += title + '\n'

    s += '{\n'
    for key, value in natsorted(d.items(), key=lambda x: x[0]):
        s += f"  {repr(key)} : {repr(value)}\n"
    s += '}\n'
    return s

def pretty_list(lst, title=None):
    s = ''

    if title != None:
        s += title + '\n'

    for i in lst:
        if title != None:
            s += '  '
        s += repr(i) + '\n'
    return s


def get_snip ():
    if len(sys.argv) == 1:
        return 'default'
    else:
        if sys.argv[1].startswith ('-'):
            return 'default'
        else:
            return sys.argv[1]

def get_cache_dict ():
    cache_dict = {}
    if os.path.exists('mkpy/cache'):
        cache = open ('mkpy/cache', 'r')
        cache_dict = ast.literal_eval (cache.readline())
        cache.close ()
    return cache_dict

def set_cache_dict (cache_dict):
    if not path_exists('mkpy'):
        return

    cache = open ('mkpy/cache', 'w')
    cache.write (str(cache_dict)+'\n')
    cache.close ()

def store (name, value, default=None):
    """
    Stores _value_ as an element of a dictionary in mkpy/cache with _name_ as
    key. Returns the value of _name_ in the cache. Used to reuse values across
    runs of the script.

    If _default_ is set, _value_==None and _name_ is not in the cache, then
    _default_ will be the value stored. This is useful to make this function
    always return a value, even if the cache hasn't been created yet.
    """
    global g_dry_run

    cache_dict = get_cache_dict ()

    if value == None:
        if name in cache_dict.keys ():
            return cache_dict[name]
        else:
            if default != None:
                cache_dict[name] = default
            else:
                print ('Key \''+name+'\' is not in mkpy/cache.')
                return
    else:
        cache_dict[name] = value

    # In dry run mode just don't update the cache file
    if not g_dry_run:
        set_cache_dict (cache_dict)
    return cache_dict.get (name)

def store_get (name, default=None):
    """
    Returns the stored value of _name_ in the dictionary at mkpy/cache.

    If _default_!=None and _name_ is not in the cache, the _name_ dictionary
    item will be initialized to _default_.
    """

    # TODO: Eventually remove this warning?
    if name == 'last_target':
        warn('Las called snip cache variable was renamed. Please change \'last_target\' to \'last_snip\'')

    cache_dict = get_cache_dict ()

    if name not in cache_dict.keys ():
        if default != None:
            cache_dict[name] = default
        else:
            # TODO: store_get(<name>) won't silently return None, it will print
            # this warning. If users want to set None as default return value,
            # store_get(<name>, default=None) has to be used. I like it because
            # it shows intent explicitly, but I'm not sure if it's intuitive
            # enough.
            print ('Key \''+name+'\' is not in pymk/cache.')

    # Even though we could avoid creating this function and require the user to
    # call store() with value==None, I found this is counter intuitive and hard
    # to remember, because it mixes a function that stores with one that gets
    # so the argument order is not straightforward, when using it to store the
    # 'natural' order is (name, value, default), but when using it to get a
    # value the common order would be (name, default, value) as value isn't
    # really useful here and needs to be set to None. Also, it rewrites the
    # cache file on reads, which is unexpected.
    return cache_dict.get (name)

def store_init (name, value):
    """
    Initializes the _name_ entry in the dictionary at mkpy/cache to _value_.
    This will only happen if _name_ is not yet in the cache.
    """

    # As with store_get() we could avoid this function, but it also
    # semantically overloads the store() function and makes it harder to know
    # the intent when reading a script that uses it. This makes the intent
    # clear as it also won't return anything.
    if value == None:
        print ('Initializing store entry \''+name+'\' to None is not allowed.')
    else:
        store (name, None, value)

# DEPRECATED
# I didn't like how I overloaded this function with at least 3 different
# semantics. I splitted it into the store* functions that make these semantics
# explicit.
#
#def pers (name, default=None, value=None):
#    """
#    Makes persistent some value across runs of the script, storing it as an
#    element of a dictionary in mkpy/cache. Stores _name_:_value_ pair in cache
#    unless value==None. Returns the value of _name_ in the cache.
#
#    If default is used, when _value_==None and _name_ is not in the cache the
#    pair _name_:_default is stored.
#    """
#    global g_dry_run
#
#    cache_dict = get_cache_dict ()
#
#    if value == None:
#        if name in cache_dict.keys ():
#            return cache_dict[name]
#        else:
#            if default != None:
#                cache_dict[name] = default
#            else:
#                print ('Key '+name+' is not in cache.')
#                return
#    else:
#        cache_dict[name] = value
#
#    # In dry run mode just don't update the cache file
#    if not g_dry_run:
#        set_cache_dict (cache_dict)
#    return cache_dict.get (name)

def pers_func_f (name, func, args, kwargs={}):
    """
    This function calls _func_(*args, **kwargs), stores whatever it returns in
    mkpy/cache with _name_ as key. If this function is called again, and _args_
    and _kwargs_ don't change, the previous result is returned without calling
    _func_ agaín.

    We assume the return value of _func_ only depends on its arguments (it's
    purely functional). If it depends on external state, there is no way we can
    detect _func_ needs to be called again.
    """
    # I don't think there is a valud usecase where we would receive no
    # arguments. If there are no arguments then the return value of the
    # function is always the same and there is no need to use this to cache the
    # result. It can be cached once with store().
    if (args == None or len(args) == 0) and len(kwargs) == 0:
        print ("pers_func_f expects at least one argument for func.")
        return

    # TODO: What should we do in dry run mode?.

    call_func = False
    cache_dict = get_cache_dict ()

    # If args changed from last call update them in cache and trigger
    # execution of func
    last_args = None
    if name+'_args' in cache_dict.keys ():
        last_args = cache_dict[name+'_args']

    if last_args != args:
        cache_dict[name+'_args'] = args
        call_func = True

    # If kwargs changed from last call update them in cache and trigger
    # execution of func
    last_kwargs = {}
    if kwargs != {}:
        if name+'_kwargs' in cache_dict.keys ():
            last_kwargs = cache_dict[name+'_kwargs']

        if last_kwargs != kwargs:
            cache_dict[name+'_kwargs'] = kwargs
            call_func = True

    # If some argument changed or func has never been called, call func and
    # store result in cache. Else return cached value.
    if last_args == None or call_func:
        res = str(func(*args, **kwargs))
        cache_dict[name] = res
        set_cache_dict (cache_dict)
        return res
    else:
        return cache_dict[name]

def pers_func (name, func, arg):
    """
    Simpler version of pers_func_f for the case when _func_ has a single
    argument.
    """
    # TODO: I haven't used pers_func_f enough to know if this is more common in
    # general. If it isn't then we should remove this and call pers_func_f
    # always (rename it to pers_func).
    #
    #     pers_func(name, func, [arg]) vs pers_func(name, func, arg)
    #
    # What I'm worried is that users will find it counter intuitive to have to
    # wrap arg into a list.
    return pers_func_f (name, func, [arg])

def path_isfile(path):
    return os.path.isfile(path)

def path_isdir (path_s):
    return os.path.isdir(path_s)

def path_resolve (path_s):
    return os.path.expanduser(path_s)

def path_exists (path_s):
    """
    Convenience function that checks the existance of a path, either as a file
    or a directory.
    """
    return pathlib.Path(path_resolve(path_s)).exists()

def path_parse (path):
    dirname, basename = os.path.split(path)
    fname, extension = os.path.splitext(basename)
    return (dirname, fname, extension)

def path_dirname (path_s):
    return os.path.dirname(path_s)

def path_basename (path_s):
    return os.path.basename(path_s)

def path_split (path):
    return os.path.splitext(path)

def path_cat (path, *paths):
    # I don't like how os.path.join() resets to root if it finds os.sep as the
    # start of an element in *paths. This ignores those leading separators.

    stripped_paths = []
    for p in paths:
        if p.startswith(os.sep):
            stripped_paths.append(p[1:])
        else:
            stripped_paths.append(p)

    return os.path.join(path, *stripped_paths)

# This could also be accomplished by:
#   ex ('mkdir -p {path_s}')
# Maybe remove this function and use that instead, although it won't work on Windows
def ensure_dir (path_s):
    global g_dry_run
    if g_dry_run:
        return

    resolved_path = path_resolve(path_s)
    if not path_exists(resolved_path):
        os.makedirs (resolved_path)

def unpack_zip(fname, curr_path='', extensions=['.zip']):
    """
    Extract nested zip files recursively. Compressed files are left untouched,
    the extracted result will be available in a directory with the same name
    (and extension) but with the .dir suffix.

    Useful for exploring the content of JAR or WAR files. Just pass
    ['.jar','.war'] to the extensions parameter.
    """
    filepath = curr_path + fname
    print (f'{filepath}')

    target_path = filepath +'.dir/'
    zf = zipfile.ZipFile(filepath)
    zf.extractall(target_path)
    extracted_list = zf.namelist()
    zf.close()

    # TODO: How does this handle a zip with the same file multiple times inside?
    for p in extracted_list:
        try:
            _, ext = path_split(p)
            if ext in extensions:
                unpack_zip(p, curr_path=target_path)
        except:
            print(ecma_red('error:') + f' could not extract {p}')
            pass

def needs_target (recipe):
    """
    This function takes a tuple of 2 strings representing paths, the first one
    we call source and the second one target. Each tuple represents a
    dependency between the source and target files. This function returns true
    whenever the target file needs to be regenerated, either because it does
    not exist or because source is more recent than target.

    The aim of this function is to implement similar functionality than the one
    provided by Make's recipes.
    """
    # TODO: Support target using common file transfer protocols like rsync, ftp
    # and SSH. How would we support authentication?.

    # TODO: Do we want to allow multiple source files?, Makefiles allow
    # multiple 'prerequisites'. My thoughts now are that this could lead users
    # in a slippery slope to have overly complicated build scripts. Not
    # supporting this is a way to make them rethink their architecture, maybe
    # use unity builds?. Anyway, it may be necessary in some cases so it's
    # useful to keep in mind, but it won't be implemented for now.
    #
    # If we were to implement it, the way to go would be to have all leading
    # elements od the tuple be the prerequisites and make the last one be the
    # target. This way it's easy to add more prerequisites and the user doesn't
    # need to change much.

    src = recipe[0]
    tgt = recipe[1]

    if not path_exists (tgt):
        return True
    else:
        if (file_time(src) > file_time(tgt)):
            return True
        else:
            return False

def needs_targets (recipes):
    """
    This function takes a list of tuples and calls needs_target() on them.
    Returns True if any of these calls returns True and False otherwise (all
    calls returned False).
    """

    for recipe in recipes:
        if needs_target (recipe):
            return True
    return False

def c_needs_rebuild (c_sources, target):
    """
    Calls gcc with the passed string as source files to compute the non system
    files included from it. It then compares its last edit time to the passed
    target file to determine if the binary needs to be rebuilt.
    """

    mm_out = ex (f'gcc -MM {c_sources}', ret_stdout=True, echo=False)
    arr = mm_out.split ()[1:]
    recipes = {(source, target) for source in arr if source != '\\'}
    return needs_targets (recipes)

def file_time(fname):
    res = 0
    tgt_path = pathlib.Path(fname)
    if tgt_path.is_file():
        res = os.stat(fname).st_mtime
    return res


def install_files (info_dict, prefix=None, only_new=True):
    global g_dry_run

    # TODO: I've come to realize this is a HORRIBLE default...
    if prefix == None:
        prefix = '/'

    prnt = []
    for f in info_dict.keys():
        tgt = info_dict[f]
        if tgt == None:
            tgt = f

        dst = path_cat(prefix, tgt)

        # Compute the absolute dest path including the file, even if just a
        # dest directory was specified
        dst_dir, fname = os.path.split (dst)
        if fname == '':
            _, fname = os.path.split (f)
        dest_file = path_cat(dst_dir, fname)

        install_file = True
        if only_new:
            # If the file already exists check we have a newer version. If we
            # don't, skip it.
            dest_file_path = pathlib.Path(dest_file)
            if dest_file_path.exists():
                src_time = file_time (f)
                dst_time = file_time (dest_file)
                if (src_time < dst_time):
                    install_file = False

        if install_file:
            if not g_dry_run:
                dst_path = pathlib.Path(dst_dir)
                if not dst_path.exists():
                    os.makedirs (dst_dir)

                shutil.copy (f, dest_file)
                prnt.append (dest_file)
            else:
                prnt.append ('Install: ' + f + ' -> ' + dest_file)

    prnt.sort()
    [print (s) for s in prnt]

    return prnt

# TODO: dnf is written in python, maybe we can call dnf's module instead of
# using ex().
# @use_dnf_python
def rpm_find_providers (file_list):
    f_str = " ".join (file_list)
    cmd = ex_escape (r"rpm -qf " + f_str + " --queryformat '%{NAME}\n' | sort | uniq")
    prov_str = ex (cmd, ret_stdout=True, echo=False)
    return prov_str.split ('\n')

# @use_dnf_python
def rpm_find_deps (pkg_name):
    # FIXME: The --installed option is required because there is a bug in dnf
    # that makes the query impossibly slow. Check if this gets fixed.
    cmd = ex_escape(r"dnf repoquery --qf '%{NAME}' --requires --resolve --installed --recursive " + pkg_name)
    deps_str = ex (cmd, ret_stdout=True, echo=False)
    return deps_str.split ('\n')

def deb_find_providers (file_list):
    f_str = " ".join (file_list)
    cmd = ex_escape ("dpkg -S " + f_str + " | awk -F: '{print $1}' | sort | uniq")
    prov_str = ex (cmd, ret_stdout=True, echo=False)
    return prov_str.split ('\n')

def deb_find_deps (pkg_name):
    flags = '--recurse --no-recommends --no-suggests --no-conflicts --no-breaks --no-replaces --no-enhances'
    apt_cmd = 'apt-cache depends {} {}'.format(flags, pkg_name)
    cmd = ex_escape (apt_cmd + " | grep Depends | awk '{print $2}'")
    deps_str = ex (cmd, ret_stdout=True, echo=False)
    return deps_str.split ('\n')

def get_pkg_manager_type ():
    if path_exists ('/etc/os-release'):
        os_release = open ('/etc/os-release', "r")
        os_id = ''
        os_id_like = ''
        for l in os_release:
            if l.startswith ('ID='):
                os_id = l.replace ('ID=', '').strip()
            elif l.startswith ('ID_LIKE='):
                os_id_like = l.replace ('ID_LIKE=', '').strip()
        os_release.close()

        os_id = os_id.replace ("'",'').replace ('"', '')
        os_id_like = os_id_like.replace ("'",'').replace ('"', '')

        def match_os_id (wanted_ids, os_id, os_id_like):
            for i in wanted_ids:
                if i == os_id:
                    return True
                elif i == os_id_like:
                    return True

            return False

        deb_oses = ['elementary', 'ubuntu', 'debian']
        rpm_oses = ['fedora']

        if match_os_id (deb_oses, os_id, os_id_like):
            return 'deb'
        elif match_os_id (rpm_oses, os_id, os_id_like):
            return 'rpm'
        else:
            return ''

    else:
        return ''

pkg_manager_type = get_pkg_manager_type ()

# Probably put this in polymorphic classes?
# TODO: Test this in several distributions. Arch is still not implemented.
find_deps = None
find_providers = None
if pkg_manager_type == 'deb':
    find_deps = deb_find_deps
    find_providers = deb_find_providers
elif pkg_manager_type == 'rpm':
    find_deps = rpm_find_deps
    find_providers = rpm_find_providers

def prune_pkg_list (pkg_list):
    a = set()
    b = set(pkg_list)

    while b and find_deps != None:
        dep = b.pop ()
        curr_deps = find_deps (dep)
        #print ('Pruning {}: {}\n'.format(dep, " ".join(curr_deps)))
        print ('Pruning {}... '.format(dep), end='', flush=True)
        removed = []
        for d in curr_deps:
            if d in a:
                removed.append(d)
                a.remove (d)
            elif d in b:
                removed.append(d)
                b.remove (d)
        a.add (dep)

        if len(removed) > 0:
            print ('Removes: ' + ' '.join(removed))
        else:
            print ()

    if find_deps == None:
        print (f'No find_deps() function, trying to use package manager type: {pkg_manager_type}')
    print ()

    return list (a)

def gcc_used_system_includes (cmd):
    tmpfname = ex ('mktemp', ret_stdout=True, echo=False)
    arr = cmd.split()
    arr.insert (1, '-M -MF {}'.format(tmpfname))

    # FIXME: If we call gcc -M -MF <file> -o <output_file> then gcc for some
    # reason clears <output_file> and becomes empty. Strip the -o option to
    # avoid this.
    for i, tk in enumerate(arr):
        if tk == '-o':
            # Remove -o
            arr.pop(i)
            # Remove <output_file>
            arr.pop(i)
            break

    ex (" ".join (arr), echo=False)

    # NOTE: I think gcc outputs one Makefile rule per source file present in
    # the command, right now we just take all system include files and merge
    # them.
    awk_prg = r"""
    BEGIN {
        RS="\\";
        OFS="\n"
    }
    {
        for(i=1;i<=NF;i++)
            if ($i ~ /^\//) {print $i}
    }
    """
    awk_prg = ex_escape(awk_prg)
    res = ex ("awk '{}' {} | sort | uniq".format(awk_prg, tmpfname), echo=False, ret_stdout=True)
    return res.split ('\n')

def gcc_get_out (cmd):
    """
    Returns a.out or the file after -o option.
    """
    out_f = 'a.out'
    cmd_arr = cmd.split ()
    for i, tk in enumerate(cmd_arr):
        if tk == '-o':
            out_f = cmd_arr[i+1]
            break
    return out_f

def file_is_elf (fname):
    return not ex ('file '+ fname + ' | grep ELF', echo=False, no_stdout=True)

def file_exists (fname):
    return pathlib.Path(fname).exists()

g_skip_snip_cache = []
def no_cache(f):
    """"
    Decorator that makes the decorated snip function not be stored as the last
    called one. Only useful when using a default snip that reads the
    'last_snip' key in the cache.
    """

    # TODO: I like this solution better than the argument being passed to
    # pymk_default() which is an alternative implementation I used before.
    # The main reason I like this more, is that we don't repeat the function's
    # name in multiple places, so changing the function's name doesn't break
    # the behavior, on the other hand passing the array to pymk_default() would
    # break if the function name changes and it wasn't updated in the passed
    # array.
    global g_skip_snip_cache
    g_skip_snip_cache.append (f.__name__)
    return f

def pymk_default (skip_snip_cache=[]):
    global ex_cmds
    t = get_snip()

    global g_skip_snip_cache
    skip_snip_cache += g_skip_snip_cache

    if '--get_run_deps' in builtin_completions and get_cli_bool_opt ('--get_run_deps'):
        # Look for all gcc commands that generate an executable and get the
        # packages that provide the shared libraries it uses.
        #
        # Note that shared libraries is not the only way a project can have a
        # runtime dependency. These are some ways an executable may have a
        # build dependency that won't be discovered by this code:
        #   * Shared libraries that are conditionally loaded by the application
        #     using dlopen().
        #   * Any file opened by the application that is provided by other
        #     project. For example configuration files like /etc/os-release
        #     which is provided by systemd, or icon themes.
        #
        # First we try to get the executable calling a dry run of the snip, if
        # we can't find the output file then run the snip and try again. I'm
        # still not sure this is a good default because if a snip generates
        # something using gcc and then deletes it, then this function will
        # always call the snip. Still, knowing if a snip deletes gcc's output
        # seems intractable.
        if pkg_manager_type == '':
            exit ()

        use_dry_run = True
        for attempt in range (2):
            call_user_function (t, dry_run=use_dry_run)
            tmp_ex_cmds = ex_cmds[:]
            for cmd in tmp_ex_cmds:
                if cmd.startswith('gcc'):
                    out_file = gcc_get_out (cmd)
                    if not file_exists (out_file):
                        if attempt == 0:
                            # Output does not exist, reset ex_cmd and try
                            # again but not as dry_run.
                            ex_cmds = []
                            use_dry_run = False
                            break
                        else:
                            # Output does not exist but we already ran the
                            # snip. Probably the snip itself deletes this
                            # file. Skip it.
                            continue
                    elif file_is_elf (out_file):
                        info ('Runtime dependencies for: ' + out_file)
                        tmp = ex_escape("ldd " + out_file + " | awk '!/vdso/{print $(NF-1)}'")
                        required_shdeps = ex (tmp, ret_stdout=True, echo=False).split ('\n')
                        deps = find_providers (required_shdeps)
                        print (' '.join (deps))
                        print ()

                        deps = prune_pkg_list (deps)
                        deps.sort()
                        info ('Minimal runtime dependencies for: ' + out_file)
                        print (' '.join (deps))
                        print ()
            if use_dry_run == True:
                # Calling the snip wasn't necessary, we are done.
                break
        exit ()

    if '--get_build_deps' in builtin_completions and get_cli_bool_opt ('--get_build_deps'):
        # Call the snip in dry run mode and for each gcc command find the
        # packages that provide the include files it needs.
        #
        # Note that included files are not the only way a project gets build
        # dependencies. For instance using this script adds a dependency on
        # python3 (this one we add by default). Other ways in which you may
        # have build dependencies that won't be discovered by this code are:
        #
        #   * Build toolchain (gcc, llvm, ld, etc.).
        #   * Any non default python module used in pymk.py.
        #   * Any non standard command called by the ex() function.
        #   * Tools used to package the application (rpmbuild, debuild).
        #   * Dependencies of statically linked libraries.

        if pkg_manager_type == '':
            exit ()

        call_user_function (t, dry_run=True)
        deps = set()
        for cmd in ex_cmds[:]:
            if cmd.startswith('gcc'):
                include_files = gcc_used_system_includes (cmd)
                curr_deps = set (find_providers (include_files))
                deps = deps.union (curr_deps)

        # Sort and print
        deps_list = list(deps)
        deps_list.append ('python3')
        deps_list.sort()
        info ('Build dependencies for snip: ' + t)
        print (' '.join(deps_list), end='\n\n')

        deps_list = prune_pkg_list (deps_list)
        deps_list.sort()
        info ('Minimal build dependencies for snip: ' + t)
        print (" ".join (deps_list), end='\n\n')
        exit ()

    if not user_function_exists(t) and t == 'default':
        print ('No default snip. Doing nothing.')

    else:
        call_user_function (t)
        old_t = store_get ('last_snip', default=None)
        if t != 'default' and t != 'install' and t != old_t and t not in skip_snip_cache:
            store ('last_snip', value=t)

##########################
# Custom status logger API
#
# This is a simpler logging API than Python's default logger. It allows to
# easily process its content after it's been populated. Processing is done with
# normal code that reads the event array, without requiring knowledge of
# concepts like filters, handlers, formatters etc.
_level_enum = {
    'ERROR': 1,
    'WARNING': 2,
    'INFO': 3,
    'DEBUG': 4
}
_level_to_name = {}

# Create variables for the level enum above and a dictionary to get the names
_g = globals()
for varname, value in _level_enum.items():
    _g[varname] = value
    _g['_level_to_name'][value] = varname

class StatusEvent():
    def __init__(self, message, level=INFO):
        self.message = message
        self.level = level

class Status():
    def __init__(self, level=WARNING):
        self.events = []
        self.level = level

    def __str__(self):
        events_str_arr = [f'{_level_to_name[e.level]}: {e.message}' for e in self.events if e.level <= self.level]
        return '\n'.join(events_str_arr)

    def print(self):
        """
        Utility method that avoids printing an empty line if the event array is empty.
        """
        print(self, end='')

def log_clsr(level_value):
    def log_generic(status, message, echo=False):
        if echo:
            print (message)

        if status != None and status.level >= level_value:
            status.events.append (StatusEvent(message, level=level_value))
    return log_generic

for level_name, level_value in _level_enum.items():
    _g[f'log_{level_name.lower()}'] = log_clsr(level_value)

###########
# Testing
#
def get_outer_frame(nested_frames=3):
    return inspect.getouterframes(inspect.currentframe())[nested_frames]

def get_calling_test_source(nested_frames=3):
    frame = get_outer_frame(nested_frames)

    calling_source = None
    with open(frame.filename) as f:
        for i, line in enumerate(f, 1):
            if i == frame.lineno:
                calling_source = line.strip()
                break

    name = None

    # Parse the line of code where this function was called from, use the
    # source code that evaluated to the result argument as the name.
    try:
        source_ast = ast.parse(calling_source, mode='eval')
        for node in ast.walk (source_ast):
            if type(node) == ast.Call and node.func.id == get_function_name(2):
                result_code = node.args[0]

                if result_code.lineno == result_code.end_lineno:
                    name = calling_source[result_code.col_offset:result_code.end_col_offset]
                else:
                    name = calling_source[result_code.col_offset:] + '...'

            elif type(node) == ast.Call and node.func.id.endswith('_test'):
                if node.lineno == node.end_lineno:
                    name = calling_source[node.col_offset:node.end_col_offset]
                else:
                    name = calling_source[node.col_offset:] + '...'
    except:
        pass

    return name

def automatic_test_function(*args, **kwargs):
    if len(args) == 1 and callable(args[0]) and len(kwargs) == 0:
        func = args[0]
        def test_function(*args, **kwargs):
            function_args = args[:-1]
            expected_result = args[-1]

            source = get_calling_test_source()
            result = func(*function_args, **kwargs)

            test (result, expected_result, name=f'{source} -> {result}')

        func.__globals__[f'{func.__name__}_test'] = test_function

        return func

    else:
        def __wrapper(func):
            permute_args_l = kwargs.get('permute_args')
            def test_function(*args, **kwargs):
                function_args = args[:-1]
                expected_result = args[-1]

                source = get_calling_test_source()

                if type(permute_args_l) == type(True):
                    # TODO: If there are repeated parameters, the number of
                    # permutations can be reduced.
                    for i, args_perm in enumerate(permutations(function_args)):
                        result = func(*args_perm, **kwargs)
                        # TODO: Show the actual permutation in the source code
                        # name, instead of just the index in the arrow.
                        test (result, expected_result, name=f'{source} -({i})-> {result}')
                elif type(permute_args_l) == type(1):
                    function_args_list = list(function_args)
                    to_be_permuted = function_args[permute_args_l][:]
                    for i, args_perm in enumerate(permutations(to_be_permuted)):
                        function_args_list[permute_args_l] = args_perm
                        result = func(*function_args_list, **kwargs)
                        test (result, expected_result, name=f'{source} -({i})-> {result}')
                else:
                    result = func(*function_args, **kwargs)
                    test (result, expected_result, name=f'{source} -> {result}')

            func.__globals__[f'{func.__name__}_test'] = test_function
            return func

        return __wrapper

class Test():
    def __init__(self, name):
        self.children_status = None
        self.name = name
        self.output = ''

class TestContext():
    def __init__(self):
        self.width = 40
        self.indent = 4

        self.tests = []
        self.force_output = False

    def set_status(self, status):
        if len(self.tests) > 0 and status != None:
            if self.tests[-1].children_status == None:
                self.tests[-1].children_status = status
            else:
                self.tests[-1].children_status &= status

    def out(self, s):
        if len(self.tests) == 0:
            print (s)
        else:
            self.tests[-1].output += s + '\n'

__g_test_context = TestContext()

def test_force_output(value=True, test_context=None):
    global __g_test_context
    if test_context == None:
        test_context = __g_test_context
    test_context.force_output = value

def test_push(name, test_context=None):
    global __g_test_context
    if test_context == None:
        test_context = __g_test_context

    test_context.tests.append(Test(name))

def test_result_string(s, result, test_context=None):
    global __g_test_context
    if test_context == None:
        test_context = __g_test_context

    unescaped = re.sub(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])', '', s)

    return s + '.'*max(3, test_context.width-len(unescaped)) + ' ' + result

def test_result_string_default(name, result, test_context=None):
    global __g_test_context
    if test_context == None:
        test_context = __g_test_context

    if result:
        return test_result_string (f'{name}', ecma_green("OK"), test_context=test_context)
    else:
        return test_result_string (f'{name}', ecma_red("FAIL"), test_context=test_context)

def test_pop(result=None, test_context=None):
    global __g_test_context
    if test_context == None:
        test_context = __g_test_context

    t = test_context.tests.pop()

    if result == None:
        result = t.children_status

    print (test_result_string_default (t.name, result))

    if result == False or t.children_status == False or test_context.force_output:
        indented = textwrap.indent (t.output, ' '*(test_context.indent)*(len(test_context.tests)+1))
        print (indented, end='')

def test_error(message, test_context=None):
    global __g_test_context
    if test_context == None:
        test_context = __g_test_context

    indented = textwrap.indent (message, ' '*(test_context.indent)*(len(test_context.tests)))
    test_context.out(indented)

def test(result, expected_result=None, name=None, test_context=None):
    global __g_test_context
    if test_context == None:
        test_context = __g_test_context

    if name == None:
        source = get_calling_test_source()
        name = f'{source} -> {result}'

    success = None
    if expected_result != None:
        if result == expected_result:
            success = True
        else:
            success = False

    if expected_result != None:
        test_context.out(test_result_string_default(name, result == expected_result))
    else:
        test_context.out(f'{name} -> {result} ... {ecma_yellow("?")} ')

    test_context.set_status(success)

    return success

##############
# Regex tester
#
def get_color_by_idx(idx):
    if idx == 0:
        return ecma_green
    elif idx == 1:
        return ecma_cyan
    elif idx == 2:
        return ecma_yellow
    elif idx == 3:
        return ecma_magenta
    elif idx == 4:
        return ecma_blue
    elif idx == 5:
        return ecma_code_f(52)
    elif idx == 6:
        return ecma_code_f(183)

def regex_match_print (string, match_object):
    res = ''
    if match_object != None:
        markers = {}
        for i in range(len(match_object.groups())+1):
            start, end = match_object.span(i)
            if start != -1:
                if start != end:
                    marker = markers.get(start)
                    if marker == None:
                        markers[start] = []
                    markers[start].append (('<', get_color_by_idx(i)))

                    marker = markers.get(end)
                    if marker == None:
                        markers[end] = []
                    markers[end].insert (0, ('>', get_color_by_idx(i)))

                else:
                    marker = markers.get(end)
                    if marker == None:
                        markers[end] = []
                    markers[end].insert (0, ('<', get_color_by_idx(i)))
                    markers[end].append (('>', get_color_by_idx(i)))


        last_pos = 0
        sorted_markers = [(key, markers[key]) for key in sorted(markers.keys())]
        for pos, markers_at_pos in sorted_markers:
            res += string[last_pos:pos]
            for marker in markers_at_pos:
                res += marker[1](marker[0])
            last_pos = pos
        res += string[last_pos:]

    else:
        return string

    return res

def regex_test(regex, test_str, should_match, test_context=None):
    global __g_test_context
    if test_context == None:
        test_context = __g_test_context

    out_str = ''

    result = re.search(regex, test_str)

    if (should_match and result != None) or (not should_match and result == None):
        if result != None:
            out_str += ecma_green('OK MATCH')
        else:
            out_str += ecma_green('OK NO MATCH')

        out_str += ' ' + regex_match_print (test_str, result)

        test_context.set_status(True)

    else:
        if result != None:
            out_str += f"{ecma_red('FAIL MATCH')} {regex_match_print(test_str, result)}\n"
            out_str += str(result) + '\n'

        else:
            out_str += ecma_red('FAIL NO MATCH') + ' ' + regex_match_print (test_str, result)

        test_context.set_status(False)

    test_context.out(out_str)


############################################
# Implementation of control flow reuse for C
#

def generate_automacros (out_fname, files=None):
    class TokenType(Enum):
        OPERATOR = 1
        KEYWORD = 2
        COMMENT = 3
        SPACE = 4
        OTHER = 5

    class Scanner():
        def __init__(self, data, pos):
            self.str = data
            self.pos = pos

    class Token():
        def __init__(self, token_type, value):
            self.type = token_type
            self.value = value

    def token_next (scnr):
        new_token = None

        if scnr.str.startswith ('//', scnr.pos):
            scnr.pos += 2
            start = scnr.pos
            while scnr.str[scnr.pos] != '\n' and scnr.pos < len(scnr.str):
                scnr.pos += 1
            new_token = Token(TokenType.COMMENT,  scnr.str[start:scnr.pos])

        if new_token == None and scnr.str.startswith ('/*', scnr.pos):
            scnr.pos += 2
            start = scnr.pos
            comment_end = scnr.str.find('*/')
            if comment_end != -1:
                comment = scnr.str[start:comment_end]
            else:
                comment = scnr.str[start:]

            new_token = Token(TokenType.COMMENT, comment)

        if new_token == None:
            operators = [';','{', '}', '(', ')', ',']
            for op in operators:
                if scnr.str.startswith (op, scnr.pos):
                    scnr.pos += len(op)
                    new_token = Token(TokenType.OPERATOR, op)
                    break

        if new_token == None:
            keywords = ['if', 'else', 'do', 'while', 'for']
            for kwd in keywords:
                if scnr.str.startswith (kwd, scnr.pos):
                    scnr.pos += len(kwd)
                    new_token = Token(TokenType.KEYWORD, kwd)

        if new_token == None:
            start = scnr.pos
            spaces = ['\n', ' ', '\t']
            if scnr.str[scnr.pos] in spaces:
                while scnr.str[scnr.pos] in spaces and scnr.pos < len(scnr.str):
                    scnr.pos += 1
                new_token = Token(TokenType.SPACE, scnr.str[start:scnr.pos])

        if new_token == None:
            start = scnr.pos
            while scnr.str[scnr.pos] not in operators and scnr.pos < len(scnr.str):
                scnr.pos += 1

            new_token = Token(TokenType.OTHER, scnr.str[start:scnr.pos])

        # For all non-comment tokens, append the spacing to its value. We do this
        # to preserve formatting in the generated code. When matching token values
        # we strip spaces.
        if new_token != None and new_token.type != TokenType.COMMENT:
            space_start = scnr.pos
            spaces = ['\n', ' ', '\t']
            while scnr.str[scnr.pos] in spaces and scnr.pos < len(scnr.str):
                scnr.pos += 1
            new_token.value += scnr.str[space_start:scnr.pos]

        return new_token

    def token_match_error (token, token_type, token_value=None):
        if token.type != token_type:
            if token_value == None:
                print (f"error: Expected token type {token_type.name}, got '{token.value}' of type {token.type.name}.")
            else:
                print (f"error: Expected token '{token_value}' of type {token_type.name}, got '{token.value}' of type {token.type.name}.")

        elif token_value != None and token.value != token_value:
            print (f"error: Expected '{token_value}', got '{token.value}'.")

    def token_match (token, token_type, token_value=None):
        match = False
        if token.type == token_type:
            if token_value == None or token.value.strip() == token_value:
                match = True

        return match

    def token_expect (scnr, token_type, token_value=None):
        token = token_next (scnr)
        if not token_match (token, token_type, token_value):
            token_match_error (token, token_type, token_value)

    ## START OF IMPLEMENTATION ##

    if files == None:
        files = []
        source_code_dir = os.path.abspath(path_resolve('.'))
        for dirpath, dirnames, filenames in os.walk(source_code_dir):
            for fname in filenames:
                abs_path = path_cat(dirpath, fname)
                if abs_path.endswith(".c") or abs_path.endswith(".h"):
                    files.append(abs_path)


    result = ''
    curr_macro_name = ''
    curr_macro = ''
    macros = {}
    for abs_path in files:
        f = open(abs_path)
        file_data = f.read()
        f.close()

        auto_macro_start = file_data.find ('@AUTO_MACRO_PREFIX')
        while auto_macro_start != -1:
            prefix = ''
            stub = ''
            macro_dict = {}

            scnr = Scanner(file_data, auto_macro_start)

            token = token_next(scnr)
            token_expect(scnr, TokenType.OPERATOR, '(')
            prefix = token_next(scnr).value
            token_expect(scnr, TokenType.OPERATOR, ')')

            token = token_next(scnr)
            while not token_match(token, TokenType.OPERATOR, '{'):
                stub += token.value
                token = token_next(scnr)
            stub += token.value

            brace_count = 1
            while brace_count > 0:
                token = token_next (scnr)

                if token_match(token, TokenType.OPERATOR, '{'):
                    brace_count += 1
                elif token_match(token, TokenType.OPERATOR, '}'):
                    brace_count -= 1

                if token_match (token, TokenType.COMMENT):
                    marker_start = token.value.find ('@AUTO_MACRO(BEGIN)')
                    if marker_start != -1:
                        macro_name = token.value[:marker_start].strip()
                        curr_macro_name = prefix + macro_name.upper().replace(' ', '_')

                    else:
                        marker_end = token.value.find ('@AUTO_MACRO(END)')
                        if marker_end != -1:
                            macro_dict[curr_macro_name] = curr_macro
                            stub += curr_macro_name
                            curr_macro_name = ''
                            curr_macro = ''

                else:
                    # TODO: Keep comments in macros, we need to avoid adding
                    # '\' at line end. Right now we ignore them.
                    if curr_macro_name != '':
                        added_continuation = token.value.replace('\n', ' \\\n')
                        curr_macro += added_continuation

                    elif not (stub.strip(" ").endswith('\n') and token.value.strip() == ''):
                        # Add line to stub, except if it would create consecutive empty lines.
                        stub += token.value

                #print (f'({token.type.name}, "{token.value}")')

            if prefix not in macros.keys():
                macros[prefix] = ((stub, macro_dict))
            else:
                print (f"error: There is already an automacro with prefix '{prefix}'")

            auto_macro_start = file_data.find ('@AUTO_MACRO_PREFIX', scnr.pos)

    for prefix, (stub, macro_dict) in macros.items():
        result += f"/* Stub for '{prefix}'\n" + stub.strip() + '\n*/\n\n'
        for macro_name, definition in macro_dict.items():
            result += f'#define {macro_name}'
            result += definition + '\n'


    out_f = open (out_fname, 'w')
    out_f.write (result)
    out_f.close()
