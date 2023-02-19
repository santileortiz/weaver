from mkpy.utility import *

from natsort import natsorted
import random
import errno
import glob

def replace_subpath(path, old_path, new_path):
    return path.replace(old_path.rstrip(os.sep), new_path.rstrip(os.sep), 1)

def copy_changed(src, dst):
    paths = glob.glob(path_cat(src, '/**'), recursive=True)
    file_paths = []

    for path in paths:
        if path_isdir (path):
            ensure_dir (replace_subpath(path, src, dst))
        else:
            file_paths.append(path)

    file_dict = {}
    for fname in file_paths:
        file_dict[fname] = replace_subpath(fname, src, dst)

    install_files(file_dict, '/')

canonical_fname_r = '^(?:(?P<idx>[0-9]*)_)?(?:(?P<prefix>.*?)_)?(?P<id>[23456789CFGHJMPQRVWX]{8,})(?P<location>(?:\.[0-9]+)+)?(?: (?P<name>.+))?\.(?P<extension>.*)$'


# TODO: Implement something like an improved C struct but in Python.
#  - Mutable, unlike namedtuple.
#  - Only allows accessing members by name (forbids access by index like namedtuple).
#  - Simple attribute definition. Support for default values and no repetition of attribute names like the usual __init__ implementation (this can be similar to namedtuple, but maybe we can do better?)
#  - Automatic __eq__ implementation that compares by value.
#  - Automatic __repr__ implementation.
class CanonicalName():
    def __init__(self, is_canonical=False, idx=None, prefix=None, identifier=None, location=[], name=None, extension=None):
        self.is_canonical = is_canonical
        self.prefix = prefix
        self.idx = idx
        self.identifier = identifier
        self.location = location
        self.name = name
        self.extension = extension

    def __eq__(self, other):
        return other != None and isinstance(other, CanonicalName) and self.is_canonical == other.is_canonical and self.idx == other.idx and self.prefix == other.prefix and self.identifier == other.identifier and self.location == other.location and self.name == other.name and self.extension == other.extension

    def __repr__(self):
        return f'({repr(self.is_canonical)}, {repr(self.idx)}, {repr(self.prefix)}, {repr(self.identifier)}, {repr(self.location)}, {repr(self.name)}, {repr(self.extension)})'

    def fname(self):
        _, fname = new_canonical_name(prefix=self.prefix,
                idx=self.idx,
                identifier=self.identifier,
                location=self.location,
                name=self.name,
                extension=self.extension)
        return fname


@automatic_test_function
def canonical_parse (path):
    fname = path_basename (path)

    result = re.search(canonical_fname_r, fname)

    is_canonical = False
    prefix = None
    idx = None
    identifier = None
    location_str = None
    name = None
    extension = None

    if result != None:
        groups = result.groupdict()

        is_canonical = True
        prefix = groups['prefix']

        idx_str = groups['idx']
        idx = int(idx_str) if idx_str != None else None

        identifier = groups['id']
        location_str = groups['location']
        name = groups['name']
        extension = groups['extension']

    location = []
    if location_str != None:
        location = [int(i) for i in location_str.split('.')[1:]]

    return CanonicalName(is_canonical, idx, prefix, identifier, location, name, extension)

def is_canonical (path):
    return canonical_parse.is_canonical

def collect_canonical_fnames (path):
    has_non_canonical = False

    non_canonical = []
    canonical_files = []
    for dirpath, dirnames, filenames in os.walk(path):
        for fname in filenames:
            canonical_fname = canonical_parse(fname)
            if not canonical_fname.is_canonical:
                has_non_canonical = True
                non_canonical.append(fname)
            else:
                canonical_files.append (canonical_fname)
    result = natsorted (canonical_files, key = lambda f: (f.idx, '.'.join([str(l) for l in f.location]), f.name))

    if has_non_canonical:
        print (f"Some file names are not in canonical form, they will be ignored:")
        for fname in non_canonical:
            print (fname)

    return result

def new_canonical_name (prefix=None,
        idx=None,
        identifier=None,
        location=[],
        name=None,
        extension=None,
        path=None):
    error = None

    idx_str = ''
    if idx != None and idx < 0:
        idx_str = f'0_'
    elif idx != None:
        idx_str = f'{idx}_'

    prefix_str = ''
    if prefix != None:
        prefix_str = f'{prefix}_'

    if identifier == None:
        identifier = new_identifier()

    location_str = ''
    if len(location) > 0:
        location_str = '.' + '.'.join([str(i) for i in location])

    name_str = ''
    if name != None:
        name_str = f' {name}'

    extension_str = ''
    if extension != None:
        if not extension.startswith('.'):
            extension_str = f'.{extension}'
        else:
            extension_str = extension

    new_fname = f"{idx_str}{prefix_str}{identifier}{location_str}{name_str}{extension_str}"

    if path != None:
        tgt_path = f'{path_cat(path, new_fname)}'
        if path_exists (tgt_path):
            error = errno.EEXIST

        # TODO: Implement a flag parameter that executes a "soft duplicate"
        # test, meaning, it checks if a file with the same identifier exists in
        # the target directory, but not exactly the same file name (diiferent
        # index, prefix etc.). Note that files with different extension should
        # not be considered soft duplicates, these are "related files".

    return (error, new_fname)

def new_unique_canonical_name (path, idx=None, prefix=None, location=[], name=None, extension=None, id_length=10):
    while True:
        new_fname_error, new_fname = new_canonical_name(prefix=prefix,
                idx=idx,
                identifier=new_identifier(length=id_length),
                location=location,
                name=name,
                extension=extension,
                path=path)

        if new_fname_error == None:
            break

    return new_fname

def new_unique_canonical_path (*args, **kwargs):
    return path_cat(args[0], new_unique_canonical_name(*args, **kwargs))

def file_canonical_rename (path, target,
        prefix, idx, identifier, location, name,
        verbose, dry_run, keep_name=None, force_rename=False, original_names=None, error_if_target_exists=False):
    error = None

    original_dirname, original_basename, extension = path_parse(path)
    canonical_name = canonical_parse(original_basename)
    
    if canonical_name.is_canonical and not force_rename and identifier != None and identifier != canonical_name.identifier:
        error = ecma_red('error:') + f' refusing to change id of file already in canonical form: {path}'
        return (error, None)

    """
    keep_name   name       is_canonical   new_name
    ------------------------------------+---------------------------
    None        None       False        | None
    None        None       True         | canonical_name.name
    None        "MyName"   X            | "MyName"
    False       X          X            | name
    True        None       False        | original_basename
    True        None       True         | canonical_name.name
    True        "MyName"   False        | original_basename (warn if name!=new_name)
    True        "MyName"   True         | canonical_name.name (warn if name!=new_name)
    """
    if keep_name == None:
        if name != None:
            new_name = name
        elif canonical_name.is_canonical:
            new_name = canonical_name.name
        else:
            new_name = None
    elif keep_name == False:
        new_name = name
    elif keep_name == True:
        if canonical_name.is_canonical:
            new_name = canonical_name.name
        else:
            new_name = original_basename
    if keep_name == True and name != None and name != new_name:
        print (ecma_yellow('warn:') + f' attempting to change file\'s user name with keep_name==True, ignored {name} used: {new_name}')

    if extension == None:
        if original_extension == None:
            _, extension = path_split(original_basename)
        else:
            extension = original_extension

    extension_str = extension
    if extension in ['.JPG', '.JPEG', '.MOV']:
        extension_str = extension.lower()

    if target == None:
        target = path_dirname(path)

    new_fname_error, new_fname = new_canonical_name(prefix=prefix,
            idx=idx,
            identifier=identifier,
            location=location,
            name=new_name,
            extension=extension_str,
            path=target)

    if identifier == None:
        identifier = canonical_parse(new_fname).identifier

    tgt_path = f'{path_cat(target, new_fname)}'

    if not dry_run:
        if new_fname_error == errno.EEXIST and error_if_target_exists:
            error = ecma_red('error:') + f' target already exists: {tgt_path}'

        else:
            try:
                # TODO: This fails if run from an SD memory to the HDD with the
                # following error:
                #   OSError: [Errno 18] Invalid cross-device link: ...
                os.rename (f'{path}', tgt_path)

            except:
                error = ecma_red('error:') +  f' exception while moving: {path} -> {tgt_path}'

            if original_names != None:
                try:
                    original_names_file = open(original_names, 'a+')
                    original_name = '"' + path_basename(path).encode("unicode_escape").decode().replace('"', '\\"') + '"'
                    original_names_file.write(f'{identifier} {original_name};\n')
                    original_names_file.close()
                except:
                    error = ecma_red('error:') +  f' exception appending to: {original_names}'

    return (error, tgt_path)

def collect(path_or_file_list):
    """
    Resolves a parameter that may be either a file list or a path. It returns a
    list of absolute paths to files.
    """

    file_list = []
    if type(path_or_file_list) == type(''):
        for dirpath, dirnames, filenames in os.walk(path_or_file_list):
            for fname in filenames:
                file_list.append (path_cat(dirpath, fname))
        file_list = natsorted(file_list, key=lambda path: path_basename(path))

    else:
        file_list = path_or_file_list

    return file_list

def canonical_renumber (path_or_file_list, new_id_order, verbose=False, dry_run=False):
    error = None

    result = []
    non_canonical = []
    file_list = collect (path_or_file_list)

    canonical_files = {}
    for path in file_list:
        basename = path_basename (path)
        canonical_fname = canonical_parse(basename)
        if not canonical_fname.is_canonical:
            non_canonical.append(path)
        else:
            if canonical_fname.identifier not in canonical_files.keys():
                canonical_files[canonical_fname.identifier] = []
            canonical_files[canonical_fname.identifier].append((path, canonical_fname))

    files_left = canonical_files.copy()

    for i, identifier in enumerate(new_id_order, 1):
        del files_left[identifier]

        for path,cn in canonical_files[identifier]:
            rename_error, new_path = file_canonical_rename (path, None,
                    cn.prefix, i, identifier, cn.location, cn.name,
                    verbose, dry_run)

            if rename_error != None:
                error = '' if error == None else error
                error += '{rename_error}\n'
            else:
                result.append ([path, new_path])
                if verbose:
                    print (f'{path} -> {new_path}')

    # Non specified files are assigned index 0.
    # TODO: Another behavior could be just to sort them by name somewhere,
    # could be at the end, at the start, or inside an "unsortable" directory.
    for identifier,file_info in files_left.items():
        for path,cn in file_info:
            rename_error, new_path = file_canonical_rename (path, None,
                    cn.prefix, -1, identifier, cn.location, cn.name,
                    verbose, dry_run)

            if rename_error != None:
                error = '' if error == None else error
                error += '{rename_error}\n'
            else:
                result.append ([path, new_path])
                if verbose:
                    print (f'{path} -> {new_path}')

    if len(non_canonical):
        error = '' if error == None else error
        error += f"Some file names are not in canonical form, they will be ignored:\n"
        [error.append('  {p}\n') for p in non_canonical]

    return (error, result)

def append_empty_line(fname):
    original_names_file = open(fname, 'a+')
    original_names_file.write(f'\n')
    original_names_file.close()

def canonical_move (path_or_file_list, target, prefix=None, ordered=False, position=None,
        keep_name=False, dry_run=False, original_names=None, verbose=False):
    error = None
    result = []

    file_list = collect (path_or_file_list)

    original_target_files = []
    if path_exists(target):
        for dirpath, dirnames, filenames in os.walk(target):
            for fname in filenames:
                original_target_files.append (path_cat(dirpath, fname))

    ensure_dir (target)

    source_ids = []
    if position != None:
        target_ids = [canonical_fname.identifier for canonical_fname in collect_canonical_fnames(target)]

    is_ordered = True if ordered or position != None else False

    if original_names != None and not dry_run:
        append_empty_line (original_names)

    for i, path in enumerate(file_list, 1):
        if not path_exists(path):
            error = '' if error == None else error
            error += ecma_red('error:') + f' no such file: {path}\n'
            continue

        file_id = new_identifier()
        source_ids.append (file_id)

        rename_error, new_path = file_canonical_rename (path,
                target,
                prefix,
                i if is_ordered else None,
                file_id,
                [],
                None,
                verbose, dry_run,
                keep_name=keep_name,
                original_names=original_names, error_if_target_exists=True)

        if rename_error != None:
            error = '' if error == None else error
            error += f'{rename_error}\n'
        else:
            result.append ([path, new_path])
            if verbose and position==None:
                print (f'{path} -> {new_path}')

    if position != None:
        tmp_result = result[:]
        move_source_files = [r[0] for r in tmp_result]
        move_target_files = [r[1] for r in tmp_result] + original_target_files
        if position < 0:
            pivot = position+1
            if pivot == 0:
                ordered_ids = target_ids[:] + source_ids
            else:
                ordered_ids = target_ids[:pivot] + source_ids + target_ids[pivot:]
        else:
            ordered_ids = target_ids[:position-1] + source_ids + target_ids[position-1:]

        renumber_error, renumber_result = canonical_renumber (move_target_files,
                ordered_ids, verbose=False, dry_run=dry_run)
        renumber_result_map = {r[0]:r[1] for r in renumber_result}

        result = [[r[0], renumber_result_map[r[1]]] for r in tmp_result]

        if verbose:
            [print (f'{r[0]} -> {r[1]}') for r in result]

    return (error, result)

def canonical_rename (path_or_file_list, prefix=None, ordered=False,
        dry_run=False, original_names=None, verbose=False):

    if type(path_or_file_list) == type(''):
        target = path_or_file_list if path_isdir(path_or_file_list) else path_dirname(path_or_file_list)
        return canonical_move (path_or_file_list, target, prefix=prefix, ordered=ordered,
            dry_run=dry_run, original_names=original_names, verbose=verbose)

    else:
        error = None
        result = []

        files_by_folder = {}
        for path in path_or_file_list:
            dirname = path_dirname(path)
            if dirname not in files_by_folder.keys():
                files_by_folder[dirname] = []
            files_by_folder[dirname].append(path)

        for target in files_by_folder:
            sub_error, sub_result = canonical_move (files_by_folder[target], target, prefix=prefix, ordered=ordered,
                dry_run=dry_run, original_names=original_names, verbose=verbose)

            if sub_error != None:
                if error == None:
                    error = ''
                error += sub_error
            result += sub_result

    return (error, result)

def build_file_tree (file_tree):
    if type(file_tree) == type(set()):
        pass

def new_identifier(length = 10):
    characters = ['2','3','4','5','6','7','8','9','C','F','G','H','J','M','P','Q','R','V','W','X']
    new_id = ""
    for i in range(length):
        new_id += random.choice(characters)

    return new_id


#########################
# Multi File Document API
#########################

class MfdActions(Enum):
    NEW = 1
    NEW_PAGE = 2
    END_SECTION = 3

class MultiFileDocument():
    def __init__(self, target_path, idx=None, prefix='scn', name=None, extension='jpg'):
        self.target_path = target_path
        self.idx = idx
        self.prefix = prefix
        self.name = name
        self.extension = extension

        self.identifier = ''

        self.document_files = []
        self.curr_location = []
        self.curr_section_idx = -1

        self.error = False
        self.last_action = None

def mfd_new(self):
    if self.error:
        return

    self.document_files = []
    self.curr_location = []
    self.curr_section_idx = -1

    new_fname = new_unique_canonical_name(self.target_path,
            prefix=self.prefix,
            name=self.name,
            extension=self.extension)
    self.identifier = canonical_parse(new_fname).identifier

    self.document_files.append(new_fname)

    self.last_action = MfdActions.NEW
    return path_cat(self.target_path, new_fname)

def mfd_add_section(self):
    self.curr_location.insert(0, 1)

    new_document_files = []
    for fname in self.document_files:
        new_location = [1] + canonical_parse(fname).location

        error, new_path = file_canonical_rename (path_cat(self.target_path, fname), None,
                self.prefix, self.idx, self.identifier, new_location, None,
                False, False)
        new_document_files.append (path_basename (new_path))

    self.document_files = new_document_files
    self.curr_section_idx += 1

def mfd_new_page(self):
    if self.error:
        return

    if self.curr_section_idx == -1:
        mfd_add_section(self)

    if self.last_action == MfdActions.END_SECTION:
        location = self.curr_location
        self.curr_location[self.curr_section_idx] += 1
        for i in range(self.curr_section_idx+1, len(self.curr_location)):
            self.curr_location[i] = 1
        self.curr_section_idx = len(self.curr_location) - 1

    else:
        canonical_fname = canonical_parse(self.document_files[-1])
        location = location=canonical_fname.location
        location[-1] += 1

    error, new_fname = new_canonical_name(path=self.target_path,
            prefix=self.prefix,
            identifier=self.identifier,
            location=location,
            name=self.name,
            extension=self.extension)
    self.document_files.append(new_fname)

    self.last_action = MfdActions.NEW_PAGE
    return path_cat(self.target_path, new_fname)

def mfd_end_section(self):
    if self.error:
        return

    if self.curr_section_idx == -1:
        self.error = True

    elif self.curr_section_idx == 0:
        mfd_add_section(self)

    self.curr_section_idx -= 1

    self.last_action = MfdActions.END_SECTION
