from mkpy.utility import *

from natsort import natsorted
import random

canonical_fname_r = '^(?:(.*?)_)??(?:([0-9]*)_)?([23456789CFGHJMPQRVWX]{8,})\.(.*)$'

@automatic_test_function
def canonical_parse (path):
    fname = path_basename (path)
    f_base, f_extension = path_split (fname)

    result = re.search(canonical_fname_r, fname)

    is_canonical = False
    prefix = None
    idx = None
    identifier = None

    if result != None:
        is_canonical = True
        prefix = result.group(1)

        idx_str = result.group(2)
        idx = int(idx_str) if idx_str != None else None
        identifier = result.group(3)

    return (is_canonical, prefix, idx, identifier)

def is_canonical (path):
    return canonical_parse[0]

def collect_canonical_fnames (path):
    has_non_canonical = False

    non_canonical = []
    canonical_files = []
    for dirpath, dirnames, filenames in os.walk(path):
        for fname in filenames:
            canonical_fname = canonical_parse(fname)
            if not canonical_fname[0]:
                has_non_canonical = True
                non_canonical.append(fname)
            else:
                canonical_files.append (canonical_fname)
    result = sorted (canonical_files, key = lambda f: f[2])

    if has_non_canonical:
        print (f"Some file names are not in canonical form, they will be ignored:")
        for fname in non_canonical:
            print (fname)

    return result

def file_canonical_rename (path, target,
        prefix, idx, identifier, extension,
        verbose, dry_run, error_if_target_exists=False):
    error = None

    prefix_str = ''
    if prefix != None:
        prefix_str = f'{prefix}_'

    idx_str = ''
    if idx != None:
        idx_str = f'{idx}_'

    if extension == None:
        _, extension = path_split(path)
    extension_str = extension
    if extension in ['.JPG', '.JPEG', '.MOV']:
        extension_str = extension.lower()

    new_fname = f"{prefix_str}{idx_str}{identifier}{extension_str}"

    if target == None:
        target = path_dirname(path)

    tgt_path = f'{path_cat(target, new_fname)}'

    if not dry_run:
        if not path_exists (tgt_path):
            try:
                # TODO: This fails if run from a memory to the HDD with the following error:
                #   OSError: [Errno 18] Invalid cross-device link: ...
                os.rename (f'{path}', tgt_path)
            except:
                error = ecma_red('error:') +  f'exception while moving: {path} -> {tgt_path}'
        elif error_if_target_exists:
            error = ecma_red('error:') + f' target already exists: {tgt_path}'

    return (error, tgt_path)

def canonical_renumber (path_or_file_list, new_id_order, verbose=False, dry_run=False):
    error = None

    result = []
    file_list = []
    if type(path_or_file_list) == type(''):
        for dirpath, dirnames, filenames in os.walk(path_or_file_list):
            for fname in filenames:
                file_list.append (path_cat(dirpath, fname))
    else:
        file_list = path_or_file_list

    non_canonical = []

    canonical_files = {}
    canonical_idx = {}
    canonical_full_path = {}
    for path in file_list:
        basename = path_basename (path)
        canonical_fname = canonical_parse(basename)
        if not canonical_fname[0]:
            non_canonical.append(path)
        else:
            canonical_files[canonical_fname[3]] = canonical_fname
            canonical_idx[canonical_fname[3]] = 0
            canonical_full_path[canonical_fname[3]] = path

    for i, identifier in enumerate(new_id_order, 1):
        path = canonical_full_path[identifier]
        rename_error, new_path = file_canonical_rename (path,
                None,
                canonical_fname[1],
                i,
                identifier,
                None,
                verbose,
                dry_run)

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

def canonical_move (source, target, prefix=None, ordered=False, position=None,
        dry_run=False, verbose=False):
    error = None
    result = []

    original_target_files = []
    if path_exists(target):
        for dirpath, dirnames, filenames in os.walk(target):
            for fname in filenames:
                original_target_files.append (path_cat(dirpath, fname))

    ensure_dir (target)

    source_ids = []
    if position != None:
        target_ids = [canonical_fname[3] for canonical_fname in collect_canonical_fnames(target)]

    is_ordered = True if ordered or position != None else False

    tmp_result = []
    for dirpath, dirnames, filenames in os.walk(source):
        i = 1 if is_ordered else None

        filenames = natsorted (filenames)

        for fname in filenames:
            f_base, f_extension = path_split (fname)

            file_id = new_file_id()
            source_ids.append (file_id)

            path = path_cat(dirpath, fname)
            rename_error, new_path = file_canonical_rename (path,
                    target,
                    prefix, i, file_id, f_extension,
                    verbose, dry_run, True)

            if rename_error != None:
                error = '' if error == None else error
                error += '{rename_error}\n'
            elif position==None:
                result.append ([path, new_path])
                if verbose:
                    print (f'{path} -> {new_path}')
            else:
                tmp_result.append ([path, new_path])

            if is_ordered:
                i += 1

    if position != None:
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

def canonical_rename (path, prefix=None, ordered=False,
        dry_run=False, verbose=False):
    canonical_move (path, path, prefix=prefix, ordered=ordered,
            dry_run=dry_run, verbose=verbose)

def build_file_tree (file_tree):
    if type(file_tree) == type(set()):
        pass

def new_file_id(length = 10):
    characters = ['2','3','4','5','6','7','8','9','C','F','G','H','J','M','P','Q','R','V','W','X']
    new_id = ""
    for i in range(length):
        new_id += random.choice(characters)

    return new_id

def path_to_dict (path):
    result = {}
    for dirpath, dirnames, filenames in os.walk(path):
        for fname in filenames:
            path = path_cat(dirpath, fname)
            if path_exists (path):
                f = open(path)
                result[path] = f.read()
                f.close()

    return result

test_base = 'bin/test_tree/'

def reset_id_generator():
    """
    Reset the RNG to a fixed seed so that generated IDs are always the same.
    """
    random.seed (0)

def canonical_move_test(file_map, name, ordered=False):
    for k in file_map:
        test_file_path = path_cat(test_base, k)

        ensure_dir (path_dirname(test_file_path))
        test_file = open (test_file_path, 'w+')
        test_file.write (test_file_path + '\n')
        test_file.close()
    
    result_dict = {}
    for k in file_map:
        tgt = path_cat(test_base, file_map[k])
        result_dict[tgt] = path_cat(test_base, k)

    # Ensure file content always ends in newline
    for k in result_dict:
        if result_dict[k][-1] != '\n':
            result_dict[k] += '\n'

    src_path = path_cat(test_base, 'source')
    tgt_path = path_cat(test_base, 'target')

    test_push(name)

    original_file_tree = path_to_dict(src_path)

    reset_id_generator()
    canonical_move (src_path, tgt_path, ordered=ordered, dry_run=True)

    test_push('Dry run mode')
    success = test (path_to_dict(src_path), original_file_tree, 'Source directory remains the same')
    success = test (path_to_dict(tgt_path), {}, 'Target directory is empty')
    test_pop()

    reset_id_generator()
    canonical_move (src_path, tgt_path, ordered=ordered)

    success = test (path_to_dict(src_path), {}, 'Source tree is empty')
    success = test (path_to_dict(tgt_path), result_dict, 'Target tree is correct')

    if success == False:
        # TODO: Get better debugging output for errors
        test_error (str(result_dict))
        test_error (str(path_to_dict(tgt_path)))

    test_pop()
    shutil.rmtree (test_base)

def canonical_rename_test(file_map, name, ordered=False):
    for k in file_map:
        test_file_path = path_cat(test_base, k)

        ensure_dir (path_dirname(test_file_path))
        test_file = open (test_file_path, 'w+')
        test_file.write (test_file_path + '\n')
        test_file.close()

    result_dict = {}
    for k in file_map:
        tgt = path_cat(test_base, file_map[k])
        result_dict[tgt] = path_cat(test_base, k)

    # Ensure file content always ends in newline
    for k in result_dict:
        if result_dict[k][-1] != '\n':
            result_dict[k] += '\n'

    path = path_cat(test_base, 'dir')

    test_push(name)

    original_file_tree = path_to_dict(path)

    reset_id_generator()
    canonical_rename (path, ordered=ordered, dry_run=True)
    success = test (path_to_dict(path), original_file_tree, 'Dry run mode')

    reset_id_generator()
    canonical_rename (path, ordered=ordered)
    success = test (path_to_dict(path), result_dict, 'Resulting file tree is correct')
    if success == False:
        # TODO: Get better debugging output for errors
        test_error (str(result_dict))
        test_error (str(path_to_dict(path)))

    test_pop()

    shutil.rmtree (test_base)

def create_files_from_map (base_path, file_map):
    for k in file_map:
        test_file_path = path_cat(base_path, k)

        ensure_dir (path_dirname(test_file_path))
        test_file = open (test_file_path, 'w+')
        test_file.write (test_file_path + '\n')
        test_file.close()

def create_result_map(base_path, file_map):
    result_dict = {}
    for k in file_map:
        tgt = path_cat(base_path, file_map[k])
        result_dict[tgt] = path_cat(base_path, k)

    # Ensure file content always ends in newline
    for k in result_dict:
        if result_dict[k][-1] != '\n':
            result_dict[k] += '\n'

    return result_dict

def canonical_move_at_position_test(file_map, initial_copy_amount, position):
    sorted_files = natsorted(file_map.keys())
    initial_files = sorted_files[:initial_copy_amount]
    other_files = sorted_files[initial_copy_amount:]

    initial_files_map = {}
    for k in initial_files:
        initial_files_map[k] = file_map[k]

    other_files_map = {}
    for k in other_files:
        other_files_map[k] = file_map[k]

    create_files_from_map(test_base, initial_files_map)
    result_dict = create_result_map(test_base, file_map)

    src_path = path_cat(test_base, 'source')
    tgt_path = path_cat(test_base, 'target')

    test_push(f'move at position {position} canonical_move()')

    reset_id_generator()
    canonical_move (src_path, tgt_path, ordered=True)

    create_files_from_map(test_base, other_files_map)

    canonical_move (src_path, tgt_path, position=position)

    success = test (path_to_dict(src_path), {}, 'Source tree is empty')
    success = test (path_to_dict(tgt_path), result_dict, 'Resulting file tree is correct')
    if success == False:
        # TODO: Get better debugging output for errors
        test_error (str(result_dict))
        test_error (str(path_to_dict(tgt_path)))

    test_pop()

    shutil.rmtree (test_base)

def tests():
    test_push ('canonical filename regex')
    regex_test (canonical_fname_r, "fscn_10_8JRPV4P32J.jpg", True)
    regex_test (canonical_fname_r, "nsdklfjlsdjflnjndsk_10_8JRPV4P32J.jpg", True)
    regex_test (canonical_fname_r, "nsdklfjlsdjflnjndsk_8JRPV4P32J.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J_8JRPV4P32J.jpg", True)
    regex_test (canonical_fname_r, "24_9F663P3R7W.mov", True)

    regex_test (canonical_fname_r, "lkndsfkf.jpg", False)
    regex_test (canonical_fname_r, "IMG00003.JPG", False)
    regex_test (canonical_fname_r, "IMG55555.JPG", False)
    regex_test (canonical_fname_r, "IMG_20150325_144057.jpg", False)
    regex_test (canonical_fname_r, "DSC00076.JPG", False)
    regex_test (canonical_fname_r, "Fotos 004.jpg", False)
    regex_test (canonical_fname_r, "Fotos 54234.jpg", False)
    regex_test (canonical_fname_r, "Picture 5453.jpg", False)
    regex_test (canonical_fname_r, "SAM_2485.JPG", False)
    regex_test (canonical_fname_r, "IMG_4970.HEIC", False)
    regex_test (canonical_fname_r, "VID_20210922_133707.mp4", False)
    regex_test (canonical_fname_r, "IMG_20210926_131750_1.jpg", False)
    regex_test (canonical_fname_r, "1E13D662-8108-4B43-8C66-2E06A8AD4C4B.MOV", False)
    regex_test (canonical_fname_r, "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX.MOV", False)
    test_pop()

    test_push ('canonical_parse()')
    canonical_parse_test ("SAM_2345.JPG", (False, None, None, None))
    canonical_parse_test ("XC9RJ594PR.jpg", (True, None, None, "XC9RJ594PR"))
    canonical_parse_test ("picture_5PPMQ734QQ.jpg", (True, "picture", None, "5PPMQ734QQ"))
    canonical_parse_test ("page_10_8JRPV4P32J.jpg", (True, "page", 10, "8JRPV4P32J"))
    canonical_parse_test ("24_9F663P3R7W.mov", (True, None, 24, "9F663P3R7W"))
    test_pop()

    rename_test = {
        'dir/DSC00004.JPG': 'dir/JM3CRQJFQH.jpg',
        'dir/DSC00005.JPG': 'dir/W8R6F65XCV.jpg',
        'dir/DSC00006.JPG': 'dir/X6F54GQV5H.jpg',
        'dir/DSC00076.JPG': 'dir/MGX8VQPRC3.jpg',
        'dir/DSC00106.JPG': 'dir/V24J2XQG9G.jpg',
        'dir/DSC00334.JPG': 'dir/48W996VP44.jpg',
        'dir/IMG00001.JPG': 'dir/GRQ5FVF5VG.jpg',
        'dir/IMG00002.JPG': 'dir/V8XVWFP4XJ.jpg',
        'dir/IMG00003.JPG': 'dir/GW9F7873XC.jpg',
        'dir/IMG00004.JPG': 'dir/Q446634VJR.jpg',
        'dir/IMG00005.JPG': 'dir/CR98WMWCPQ.jpg',
        'dir/IMG00006.JPG': 'dir/H4GX5QWG89.jpg',
        'dir/IMG00013.JPG': 'dir/2C59H7GM35.jpg',
        'dir/IMG00023.JPG': 'dir/693WVX4258.jpg',
        'dir/IMG00043.JPG': 'dir/XW5J4H53X2.jpg',
        'dir/IMG00073.JPG': 'dir/875Q832VMX.jpg',
        'dir/IMG00103.JPG': 'dir/5C494FHM73.jpg'
    }
    canonical_rename_test (rename_test, 'canonical_rename()')

    rename_test = {
        'dir/DSC00004.JPG':  'dir/1_JM3CRQJFQH.jpg',
        'dir/DSC00005.JPG':  'dir/2_W8R6F65XCV.jpg',
        'dir/DSC00006.JPG':  'dir/3_X6F54GQV5H.jpg',
        'dir/DSC00076.JPG':  'dir/4_MGX8VQPRC3.jpg',
        'dir/DSC00106.JPG':  'dir/5_V24J2XQG9G.jpg',
        'dir/DSC00334.JPG':  'dir/6_48W996VP44.jpg',
        'dir/IMG00001.JPG':  'dir/7_GRQ5FVF5VG.jpg',
        'dir/IMG00002.JPG':  'dir/8_V8XVWFP4XJ.jpg',
        'dir/IMG00003.JPG':  'dir/9_GW9F7873XC.jpg',
        'dir/IMG00004.JPG': 'dir/10_Q446634VJR.jpg',
        'dir/IMG00005.JPG': 'dir/11_CR98WMWCPQ.jpg',
        'dir/IMG00006.JPG': 'dir/12_H4GX5QWG89.jpg',
        'dir/IMG00013.JPG': 'dir/13_2C59H7GM35.jpg',
        'dir/IMG00023.JPG': 'dir/14_693WVX4258.jpg',
        'dir/IMG00043.JPG': 'dir/15_XW5J4H53X2.jpg',
        'dir/IMG00073.JPG': 'dir/16_875Q832VMX.jpg',
        'dir/IMG00103.JPG': 'dir/17_5C494FHM73.jpg'
    }
    canonical_rename_test (rename_test, 'ordered canonical_rename()', ordered=True)

    move_test = {
        'source/DSC00004.JPG': 'target/JM3CRQJFQH.jpg',
        'source/DSC00005.JPG': 'target/W8R6F65XCV.jpg',
        'source/DSC00006.JPG': 'target/X6F54GQV5H.jpg',
        'source/DSC00076.JPG': 'target/MGX8VQPRC3.jpg',
        'source/DSC00106.JPG': 'target/V24J2XQG9G.jpg',
        'source/DSC00334.JPG': 'target/48W996VP44.jpg',
        'source/IMG00001.JPG': 'target/GRQ5FVF5VG.jpg',
        'source/IMG00002.JPG': 'target/V8XVWFP4XJ.jpg',
        'source/IMG00003.JPG': 'target/GW9F7873XC.jpg',
        'source/IMG00004.JPG': 'target/Q446634VJR.jpg',
        'source/IMG00005.JPG': 'target/CR98WMWCPQ.jpg',
        'source/IMG00006.JPG': 'target/H4GX5QWG89.jpg',
        'source/IMG00013.JPG': 'target/2C59H7GM35.jpg',
        'source/IMG00023.JPG': 'target/693WVX4258.jpg',
        'source/IMG00043.JPG': 'target/XW5J4H53X2.jpg',
        'source/IMG00073.JPG': 'target/875Q832VMX.jpg',
        'source/IMG00103.JPG': 'target/5C494FHM73.jpg'
    }
    canonical_move_test (move_test, 'canonical_move()')

    move_test = {
        'source/DSC00004.JPG':  'target/1_JM3CRQJFQH.jpg',
        'source/DSC00005.JPG':  'target/2_W8R6F65XCV.jpg',
        'source/DSC00006.JPG':  'target/3_X6F54GQV5H.jpg',
        'source/DSC00076.JPG':  'target/4_MGX8VQPRC3.jpg',
        'source/DSC00106.JPG':  'target/5_V24J2XQG9G.jpg',
        'source/DSC00334.JPG':  'target/6_48W996VP44.jpg',
        'source/IMG00001.JPG':  'target/7_GRQ5FVF5VG.jpg',
        'source/IMG00002.JPG':  'target/8_V8XVWFP4XJ.jpg',
        'source/IMG00003.JPG':  'target/9_GW9F7873XC.jpg',
        'source/IMG00004.JPG': 'target/10_Q446634VJR.jpg',
        'source/IMG00005.JPG': 'target/11_CR98WMWCPQ.jpg',
        'source/IMG00006.JPG': 'target/12_H4GX5QWG89.jpg',
        'source/IMG00013.JPG': 'target/13_2C59H7GM35.jpg',
        'source/IMG00023.JPG': 'target/14_693WVX4258.jpg',
        'source/IMG00043.JPG': 'target/15_XW5J4H53X2.jpg',
        'source/IMG00073.JPG': 'target/16_875Q832VMX.jpg',
        'source/IMG00103.JPG': 'target/17_5C494FHM73.jpg'
    }
    canonical_move_test (move_test, 'ordered canonical_move()', ordered=True)

    move_test = {
        'source/IMG00002.JPG':  'target/1_V8XVWFP4XJ.jpg',
        'source/IMG00003.JPG':  'target/2_GW9F7873XC.jpg',
        'source/IMG00004.JPG':  'target/3_Q446634VJR.jpg',
        'source/IMG00005.JPG':  'target/4_CR98WMWCPQ.jpg',
        'source/IMG00006.JPG':  'target/5_H4GX5QWG89.jpg',
        'source/IMG00013.JPG':  'target/6_2C59H7GM35.jpg',
        'source/IMG00023.JPG':  'target/7_693WVX4258.jpg',
        'source/IMG00043.JPG':  'target/8_XW5J4H53X2.jpg',
        'source/IMG00073.JPG':  'target/9_875Q832VMX.jpg',
        'source/IMG00103.JPG': 'target/10_5C494FHM73.jpg',

        'source/DSC00004.JPG': 'target/11_JM3CRQJFQH.jpg',
        'source/DSC00005.JPG': 'target/12_W8R6F65XCV.jpg',
        'source/DSC00006.JPG': 'target/13_X6F54GQV5H.jpg',
        'source/DSC00076.JPG': 'target/14_MGX8VQPRC3.jpg',
        'source/DSC00106.JPG': 'target/15_V24J2XQG9G.jpg',
        'source/DSC00334.JPG': 'target/16_48W996VP44.jpg',
        'source/IMG00001.JPG': 'target/17_GRQ5FVF5VG.jpg'
    }
    canonical_move_at_position_test (move_test, 7, 1)

    move_test = {
        'source/DSC00004.JPG':  'target/1_JM3CRQJFQH.jpg',

        'source/IMG00002.JPG':  'target/2_V8XVWFP4XJ.jpg',
        'source/IMG00003.JPG':  'target/3_GW9F7873XC.jpg',
        'source/IMG00004.JPG':  'target/4_Q446634VJR.jpg',
        'source/IMG00005.JPG':  'target/5_CR98WMWCPQ.jpg',
        'source/IMG00006.JPG':  'target/6_H4GX5QWG89.jpg',
        'source/IMG00013.JPG':  'target/7_2C59H7GM35.jpg',
        'source/IMG00023.JPG':  'target/8_693WVX4258.jpg',
        'source/IMG00043.JPG':  'target/9_XW5J4H53X2.jpg',
        'source/IMG00073.JPG': 'target/10_875Q832VMX.jpg',
        'source/IMG00103.JPG': 'target/11_5C494FHM73.jpg',

        'source/DSC00005.JPG': 'target/12_W8R6F65XCV.jpg',
        'source/DSC00006.JPG': 'target/13_X6F54GQV5H.jpg',
        'source/DSC00076.JPG': 'target/14_MGX8VQPRC3.jpg',
        'source/DSC00106.JPG': 'target/15_V24J2XQG9G.jpg',
        'source/DSC00334.JPG': 'target/16_48W996VP44.jpg',
        'source/IMG00001.JPG': 'target/17_GRQ5FVF5VG.jpg'
    }
    canonical_move_at_position_test (move_test, 7, 2)

    move_test = {
        'source/DSC00004.JPG':  'target/1_JM3CRQJFQH.jpg',
        'source/DSC00005.JPG':  'target/2_W8R6F65XCV.jpg',
        'source/DSC00006.JPG':  'target/3_X6F54GQV5H.jpg',
        'source/DSC00076.JPG':  'target/4_MGX8VQPRC3.jpg',
        'source/DSC00106.JPG':  'target/5_V24J2XQG9G.jpg',
        'source/DSC00334.JPG':  'target/6_48W996VP44.jpg',
        'source/IMG00001.JPG':  'target/7_GRQ5FVF5VG.jpg',

        'source/IMG00002.JPG':  'target/8_V8XVWFP4XJ.jpg',
        'source/IMG00003.JPG':  'target/9_GW9F7873XC.jpg',
        'source/IMG00004.JPG': 'target/10_Q446634VJR.jpg',
        'source/IMG00005.JPG': 'target/11_CR98WMWCPQ.jpg',
        'source/IMG00006.JPG': 'target/12_H4GX5QWG89.jpg',
        'source/IMG00013.JPG': 'target/13_2C59H7GM35.jpg',
        'source/IMG00023.JPG': 'target/14_693WVX4258.jpg',
        'source/IMG00043.JPG': 'target/15_XW5J4H53X2.jpg',
        'source/IMG00073.JPG': 'target/16_875Q832VMX.jpg',
        'source/IMG00103.JPG': 'target/17_5C494FHM73.jpg',
    }
    canonical_move_at_position_test (move_test, 7, 8)

    move_test = {
        'source/DSC00004.JPG':  'target/1_JM3CRQJFQH.jpg',
        'source/DSC00005.JPG':  'target/2_W8R6F65XCV.jpg',
        'source/DSC00006.JPG':  'target/3_X6F54GQV5H.jpg',
        'source/DSC00076.JPG':  'target/4_MGX8VQPRC3.jpg',
        'source/DSC00106.JPG':  'target/5_V24J2XQG9G.jpg',
        'source/DSC00334.JPG':  'target/6_48W996VP44.jpg',
        'source/IMG00001.JPG':  'target/7_GRQ5FVF5VG.jpg',

        'source/IMG00002.JPG':  'target/8_V8XVWFP4XJ.jpg',
        'source/IMG00003.JPG':  'target/9_GW9F7873XC.jpg',
        'source/IMG00004.JPG': 'target/10_Q446634VJR.jpg',
        'source/IMG00005.JPG': 'target/11_CR98WMWCPQ.jpg',
        'source/IMG00006.JPG': 'target/12_H4GX5QWG89.jpg',
        'source/IMG00013.JPG': 'target/13_2C59H7GM35.jpg',
        'source/IMG00023.JPG': 'target/14_693WVX4258.jpg',
        'source/IMG00043.JPG': 'target/15_XW5J4H53X2.jpg',
        'source/IMG00073.JPG': 'target/16_875Q832VMX.jpg',
        'source/IMG00103.JPG': 'target/17_5C494FHM73.jpg',
    }
    canonical_move_at_position_test (move_test, 7, -1)

    move_test = {
        'source/DSC00004.JPG':  'target/1_JM3CRQJFQH.jpg',
        'source/DSC00005.JPG':  'target/2_W8R6F65XCV.jpg',
        'source/DSC00006.JPG':  'target/3_X6F54GQV5H.jpg',
        'source/DSC00076.JPG':  'target/4_MGX8VQPRC3.jpg',
        'source/DSC00106.JPG':  'target/5_V24J2XQG9G.jpg',
        'source/DSC00334.JPG':  'target/6_48W996VP44.jpg',

        'source/IMG00002.JPG':  'target/7_V8XVWFP4XJ.jpg',
        'source/IMG00003.JPG':  'target/8_GW9F7873XC.jpg',
        'source/IMG00004.JPG':  'target/9_Q446634VJR.jpg',
        'source/IMG00005.JPG': 'target/10_CR98WMWCPQ.jpg',
        'source/IMG00006.JPG': 'target/11_H4GX5QWG89.jpg',
        'source/IMG00013.JPG': 'target/12_2C59H7GM35.jpg',
        'source/IMG00023.JPG': 'target/13_693WVX4258.jpg',
        'source/IMG00043.JPG': 'target/14_XW5J4H53X2.jpg',
        'source/IMG00073.JPG': 'target/15_875Q832VMX.jpg',
        'source/IMG00103.JPG': 'target/16_5C494FHM73.jpg',

        'source/IMG00001.JPG': 'target/17_GRQ5FVF5VG.jpg',
    }
    canonical_move_at_position_test (move_test, 7, -2)

    # TODO: Add followind tests:
    #  - Test case where prefix is not passed but target directory is prefixed.
    #  - Test dry_run mode when moving and renumbering, check that the verbose
    #    output is correct.
