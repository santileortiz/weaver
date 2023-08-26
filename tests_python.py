from mkpy.utility import *
from file_utility import *

from natsort import natsorted
import random
import filecmp
import time
import psutil

import requests

test_base = 'bin/test_tree/'

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

def reset_id_generator():
    """
    Reset the RNG to a fixed seed so that generated IDs are always the same.
    """
    random.seed (0)


###############
# Test Wrappers
###############

def file_canonical_rename_test(test_name, test_fname, expected_fname,
        prefix, idx, identifier, location, name, **kwargs):

    test_file_path = path_cat(test_base, test_fname)
    ensure_dir (path_dirname(test_file_path))
    test_file = open (test_file_path, 'w+')
    test_file.write (test_file_path + '\n')
    test_file.close()

    target_dir = path_cat(test_base, 'tgt')
    ensure_dir (target_dir)

    test_push(f"{test_name}")

    reset_id_generator()
    file_canonical_rename (test_file_path, target_dir,
            prefix, idx, identifier, location, name,
            False, False, **kwargs)
    actual_fname = ex(f'ls {target_dir} -1', ret_stdout=True, echo=False)
    success = test (actual_fname, expected_fname, f'{test_fname} -> {expected_fname}')

    if not success:
        test_error(f'{actual_fname} != {expected_fname}')

    test_pop()

    shutil.rmtree (test_base)

def file_canonical_rename_tests():
    test_push("file_canonical_rename()")

    file_canonical_rename_test("Default behavior", 'My File.JPG',  'JM3CRQJFQH.jpg',
            None, None, None, [], None)

    file_canonical_rename_test("Keep names", 'My File.JPG',  'JM3CRQJFQH My File.jpg',
            None, None, None, [], None, keep_name=True)

    file_canonical_rename_test("Name is already ID", '23456789.JPG',  '23456789.jpg',
            None, None, None, [], None)

    test_pop()

def canonical_move_test(file_map, name, ordered=False):
    create_files_from_map (test_base, file_map)

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
        test_error (pretty_dict(result_dict))
        test_error (pretty_dict(path_to_dict(tgt_path)))

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
        test_error (pretty_dict(result_dict))
        test_error (pretty_dict(path_to_dict(path)))

    test_pop()

    shutil.rmtree (test_base)

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
        test_error (pretty_dict(result_dict))
        test_error (pretty_dict(path_to_dict(tgt_path)))

    test_pop()

    shutil.rmtree (test_base)

def canonical_renumber_test(file_map, ordered_ids):
    create_files_from_map(test_base, file_map)
    result_dict = create_result_map(test_base, file_map)

    dir_path = path_cat(test_base, 'directory')

    test_push(f'canonical_renumber()')

    reset_id_generator()
    canonical_renumber (dir_path, ordered_ids)

    success = test (path_to_dict(dir_path), result_dict, 'Resulting file tree is correct')
    if success == False:
        # TODO: Get better debugging output for errors
        test_error (pretty_dict(result_dict))
        test_error (pretty_dict(path_to_dict(dir_path)))

    test_pop()

    shutil.rmtree (test_base)

def mfd_test_create_page_file(path):
    error = None
    mfd_test_create_page_file.cnt += 1
    cmd = f"echo {mfd_test_create_page_file.cnt} > '{path}'"
    #cmd = f"echo scanimage --resolution 300 --format jpeg --output-file '{path}'"

    status = ex(cmd, echo=False, no_stdout=True)
    if status != 0:
        error = True

    return error, cmd

def mfd_new_test(self):
    path = mfd_new(self)
    error, _ = mfd_test_create_page_file(path)

    if error != None:
        self.error = True

def mfd_new_page_test(self):
    path = mfd_new_page(self)
    error, _ = mfd_test_create_page_file (path)

    if error != None:
        self.error = True

# Dummy just so all tests use *_test functions and we don't have to remember
# which have test functions and which don't. All API functions have test
# versions.
def mfd_end_section_test(self):
    mfd_end_section(self)

def mfd_test(self, expected):
    if not self.error:
        expected_dict = {}
        for i, f in enumerate(expected, 1):
            expected_dict[path_cat(test_base, f)] = f'{i}\n'

        success = test (path_to_dict(test_base), expected_dict, f'{self.last_action}')
        if success == False:
            self.error = True
            test_error (pretty_dict(path_to_dict(test_base), 'Result'))
            test_error (pretty_dict(expected_dict, 'Expected'))

        else:
            test_error (pretty_list(expected))


##############
# Actual Tests
##############

def canonical_file_name_tests():
    test_push ('canonical filename regex')
    regex_test (canonical_fname_r, "10_fscn_8JRPV4P32J.jpg", True)
    regex_test (canonical_fname_r, "10_nsdklfjlsdjflnjndsk_8JRPV4P32J.jpg", True)
    regex_test (canonical_fname_r, "nsdklfjlsdjflnjndsk_8JRPV4P32J.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J_8JRPV4P32J.jpg", True) # First 8JRPV4P32J is the prefix
    regex_test (canonical_fname_r, "24_9F663P3R7W.mov", True)

    # Pagination support tests
    regex_test (canonical_fname_r, "10_fscn_8JRPV4P32J.1.jpg", True)
    regex_test (canonical_fname_r, "10_nsdklfjlsdjflnjndsk_8JRPV4P32J.1.jpg", True)
    regex_test (canonical_fname_r, "nsdklfjlsdjflnjndsk_8JRPV4P32J.1.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J.1.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J_8JRPV4P32J.1.jpg", True)
    regex_test (canonical_fname_r, "24_9F663P3R7W.1.jpg", True)
    regex_test (canonical_fname_r, "10_fscn_8JRPV4P32J.1.10.100.1000.jpg", True)
    regex_test (canonical_fname_r, "10_nsdklfjlsdjflnjndsk_8JRPV4P32J.1.10.100.1000.jpg", True)
    regex_test (canonical_fname_r, "nsdklfjlsdjflnjndsk_8JRPV4P32J.1.10.100.1000.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J.1.10.100.1000.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J_8JRPV4P32J.1.10.100.1000.jpg", True)

    # Named
    regex_test (canonical_fname_r, "10_fscn_8JRPV4P32J My File Name.jpg", True)
    regex_test (canonical_fname_r, "10_nsdklfjlsdjflnjndsk_8JRPV4P32J My File Name.jpg", True)
    regex_test (canonical_fname_r, "nsdklfjlsdjflnjndsk_8JRPV4P32J My File Name.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J My File Name.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J_8JRPV4P32J My File Name.jpg", True)
    regex_test (canonical_fname_r, "24_9F663P3R7W My File Name.mov", True)

    regex_test (canonical_fname_r, "10_fscn_8JRPV4P32J.1 My File Name.jpg", True)
    regex_test (canonical_fname_r, "10_nsdklfjlsdjflnjndsk_8JRPV4P32J.1 My File Name.jpg", True)
    regex_test (canonical_fname_r, "nsdklfjlsdjflnjndsk_8JRPV4P32J.1 My File Name.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J.1 My File Name.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J_8JRPV4P32J.1 My File Name.jpg", True)
    regex_test (canonical_fname_r, "24_9F663P3R7W.1 My File Name.jpg", True)
    regex_test (canonical_fname_r, "10_fscn_8JRPV4P32J.1.10.100.1000 My File Name.jpg", True)
    regex_test (canonical_fname_r, "10_nsdklfjlsdjflnjndsk_8JRPV4P32J.1.10.100.1000 My File Name.jpg", True)
    regex_test (canonical_fname_r, "nsdklfjlsdjflnjndsk_8JRPV4P32J.1.10.100.1000 My File Name.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J.1.10.100.1000 My File Name.jpg", True)
    regex_test (canonical_fname_r, "8JRPV4P32J_8JRPV4P32J.1.10.100.1000 My File Name.jpg", True)

    # Non canonical
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
    canonical_parse_test ("SAM_2345.JPG", CanonicalName())

    canonical_parse_test ("XC9RJ594PR.jpg", CanonicalName(True, None, None, "XC9RJ594PR", [], None, 'jpg'))
    canonical_parse_test ("picture_5PPMQ734QQ.jpg", CanonicalName(True, None, "picture", "5PPMQ734QQ", [], None, 'jpg'))
    canonical_parse_test ("10_page_8JRPV4P32J.jpg", CanonicalName(True, 10, "page", "8JRPV4P32J", [], None, 'jpg'))
    canonical_parse_test ("24_9F663P3R7W.mov", CanonicalName(True, 24, None, "9F663P3R7W", [], None, 'mov'))
    canonical_parse_test ("8JRPV4P32J_8JRPV4P32J.jpg", CanonicalName(True, None, "8JRPV4P32J", "8JRPV4P32J", [], None, 'jpg'))

    # Location
    canonical_parse_test ("XC9RJ594PR.1.10.100.1000.jpg", CanonicalName(True, None, None, "XC9RJ594PR", [1, 10, 100, 1000], None, 'jpg'))
    canonical_parse_test ("picture_5PPMQ734QQ.1.10.100.1000.txt", CanonicalName(True, None, "picture", "5PPMQ734QQ", [1, 10, 100, 1000], None, 'txt'))
    canonical_parse_test ("10_page_8JRPV4P32J.1.10.100.1000.jpg", CanonicalName(True, 10, "page", "8JRPV4P32J", [1, 10, 100, 1000], None, 'jpg'))
    canonical_parse_test ("24_9F663P3R7W.1000.100.10.1.jpg", CanonicalName(True, 24, None, "9F663P3R7W", [1000, 100, 10, 1], None, 'jpg'))
    canonical_parse_test ("1_scn_8JRPV4P32J.2.jpg", CanonicalName(True, 1, "scn", "8JRPV4P32J", [2], None, 'jpg'))

    # Named
    canonical_parse_test ("XC9RJ594PR My File Name.jpg", CanonicalName(True, None, None, "XC9RJ594PR", [], "My File Name", 'jpg'))
    canonical_parse_test ("picture_5PPMQ734QQ My File Name.jpg", CanonicalName(True, None, "picture", "5PPMQ734QQ", [], "My File Name", 'jpg'))
    canonical_parse_test ("10_page_8JRPV4P32J My File Name.jpg", CanonicalName(True, 10, "page", "8JRPV4P32J", [], "My File Name", 'jpg'))
    canonical_parse_test ("24_9F663P3R7W My File Name.mov", CanonicalName(True, 24, None, "9F663P3R7W", [], "My File Name", 'mov'))
    canonical_parse_test ("XC9RJ594PR.1.10.100.1000 My File Name.jpg", CanonicalName(True, None, None, "XC9RJ594PR", [1, 10, 100, 1000], "My File Name", 'jpg'))
    canonical_parse_test ("picture_5PPMQ734QQ.1.10.100.1000 My File Name.txt", CanonicalName(True, None, "picture", "5PPMQ734QQ", [1, 10, 100, 1000], "My File Name", 'txt'))
    canonical_parse_test ("10_page_8JRPV4P32J.1.10.100.1000 My File Name.jpg", CanonicalName(True, 10, "page", "8JRPV4P32J", [1, 10, 100, 1000], "My File Name", 'jpg'))
    canonical_parse_test ("24_9F663P3R7W.1000.100.10.1 My File Name.jpg", CanonicalName(True, 24, None, "9F663P3R7W", [1000, 100, 10, 1], "My File Name", 'jpg'))
    canonical_parse_test ("1_scn_8JRPV4P32J.2 My File Name.jpg", CanonicalName(True, 1, "scn", "8JRPV4P32J", [2], "My File Name", 'jpg'))
    canonical_parse_test ("1_scn_8JRPV4P32J.2  My File Name.jpg", CanonicalName(True, 1, "scn", "8JRPV4P32J", [2], " My File Name", 'jpg'))
    test_pop()

def canonical_rename_tests():
    test_push ('canonical_rename()')

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
    canonical_rename_test (rename_test, 'unordered')

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
    canonical_rename_test (rename_test, 'ordered', ordered=True)

    test_pop()

def canonical_move_tests():
    test_push('canonical_move()')

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
    canonical_move_test (move_test, 'unordered')

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
    canonical_move_test (move_test, 'ordered', ordered=True)

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

    test_pop()

def canonical_renumber_tests():
    test_push ('canonical_renumber()')

    renumber_test = {
        'directory/V24J2XQG9G.jpg':  'directory/1_V24J2XQG9G.jpg',
        'directory/X6F54GQV5H.jpg':  'directory/2_X6F54GQV5H.jpg',
        'directory/MGX8VQPRC3.jpg':  'directory/3_MGX8VQPRC3.jpg',
        'directory/W8R6F65XCV.jpg':  'directory/4_W8R6F65XCV.jpg',
        'directory/JM3CRQJFQH.jpg':  'directory/5_JM3CRQJFQH.jpg',
        'directory/48W996VP44.jpg':  'directory/6_48W996VP44.jpg',
    }
    canonical_renumber_test(renumber_test, ["V24J2XQG9G", "X6F54GQV5H", "MGX8VQPRC3", "W8R6F65XCV", "JM3CRQJFQH", "48W996VP44"])

    renumber_test = {
        'directory/5_V24J2XQG9G.jpg':  'directory/1_V24J2XQG9G.jpg',
        'directory/3_X6F54GQV5H.jpg':  'directory/2_X6F54GQV5H.jpg',
        'directory/4_MGX8VQPRC3.jpg':  'directory/3_MGX8VQPRC3.jpg',
        'directory/2_W8R6F65XCV.jpg':  'directory/4_W8R6F65XCV.jpg',
        'directory/1_JM3CRQJFQH.jpg':  'directory/5_JM3CRQJFQH.jpg',
        'directory/6_48W996VP44.jpg':  'directory/6_48W996VP44.jpg',
    }
    canonical_renumber_test(renumber_test, ["V24J2XQG9G", "X6F54GQV5H", "MGX8VQPRC3", "W8R6F65XCV", "JM3CRQJFQH", "48W996VP44"])

    renumber_test = {
        'directory/5_V24J2XQG9G.jpg':  'directory/0_V24J2XQG9G.jpg',
        'directory/3_X6F54GQV5H.jpg':  'directory/0_X6F54GQV5H.jpg',
        'directory/4_MGX8VQPRC3.jpg':  'directory/0_MGX8VQPRC3.jpg',
        'directory/2_W8R6F65XCV.jpg':  'directory/1_W8R6F65XCV.jpg',
        'directory/1_JM3CRQJFQH.jpg':  'directory/2_JM3CRQJFQH.jpg',
        'directory/6_48W996VP44.jpg':  'directory/3_48W996VP44.jpg',
    }
    canonical_renumber_test(renumber_test, ["W8R6F65XCV", "JM3CRQJFQH", "48W996VP44"])

    renumber_test = {
        'directory/5_prfxa_V24J2XQG9G.6 My E Name.jpg':  'directory/1_prfxa_V24J2XQG9G.6 My E Name.jpg',
        'directory/3_prfxb_X6F54GQV5H.5 My F Name.jpg':  'directory/2_prfxb_X6F54GQV5H.5 My F Name.jpg',
        'directory/4_prfxc_MGX8VQPRC3.4 My A Name.jpg':  'directory/3_prfxc_MGX8VQPRC3.4 My A Name.jpg',
        'directory/2_prfxd_W8R6F65XCV.3 My C Name.jpg':  'directory/4_prfxd_W8R6F65XCV.3 My C Name.jpg',
        'directory/1_prfxe_JM3CRQJFQH.2 My B Name.jpg':  'directory/5_prfxe_JM3CRQJFQH.2 My B Name.jpg',
        'directory/6_prfxf_48W996VP44.1 My D Name.jpg':  'directory/6_prfxf_48W996VP44.1 My D Name.jpg',
    }
    canonical_renumber_test(renumber_test, ["V24J2XQG9G", "X6F54GQV5H", "MGX8VQPRC3", "W8R6F65XCV", "JM3CRQJFQH", "48W996VP44"])

    renumber_test = {
        'directory/V24J2XQG9G.jpg':          'directory/0_V24J2XQG9G.jpg',
        'directory/48W996VP44.jpg':          'directory/0_48W996VP44.jpg',
        'directory/X6F54GQV5H.1.jpg':        'directory/1_X6F54GQV5H.1.jpg',
        'directory/X6F54GQV5H.2.jpg':        'directory/1_X6F54GQV5H.2.jpg',
        'directory/MGX8VQPRC3.1.jpg':        'directory/2_MGX8VQPRC3.1.jpg',
        'directory/MGX8VQPRC3.2.jpg':        'directory/2_MGX8VQPRC3.2.jpg',
        'directory/W8R6F65XCV.jpg':          'directory/3_W8R6F65XCV.jpg',
        'directory/JM3CRQJFQH.3.1.1.99.jpg': 'directory/4_JM3CRQJFQH.3.1.1.99.jpg',
        'directory/JM3CRQJFQH.1.5.1.1.jpg':  'directory/4_JM3CRQJFQH.1.5.1.1.jpg',
        'directory/JM3CRQJFQH.1.1.7.1.jpg':  'directory/4_JM3CRQJFQH.1.1.7.1.jpg',
    }
    canonical_renumber_test(renumber_test, ["X6F54GQV5H", "MGX8VQPRC3", "W8R6F65XCV", "JM3CRQJFQH"])

    test_pop()

def get_target_test_w(*args):
    expected = tuple(None if p==None else os.path.abspath(path_resolve(p)) for p in args[-1])

    new_args = list(args[:-1])
    new_args.append(expected)

    #print(new_args[:-1])
    #print (expected)
    get_target_test(*new_args, silent=True)
    #print()

def get_target_tests():
    test_push ("get_target()")

    # TODO: I'm pretty sure all these tests can be compressed into much more
    # smaller code, but at least for now this is exhaustive and easy to debug
    # when one of them fails.
    get_target_test_w(None, None, None, (None, None))
    get_target_test_w(None, None, "anything", (None, None))

    get_target_test_w(None, ".hidden-folder",        None, ('~/.weaver/files/.hidden-folder',        None))
    get_target_test_w(None, "subdir/my-data-folder", None, ('~/.weaver/files/subdir/my-data-folder', None))
    get_target_test_w(None, "~/.app/files/",         None, ('~/.app/files',                          None))
    get_target_test_w(None, "/opt/app/files/",       None, ('/opt/app/files',                        None))
    get_target_test_w(None, "./subdir/files/",       None, ('./subdir/files',                        None))
    get_target_test_w(None, "../../subdir/files/",   None, ('../../subdir/files',                    None))

    get_target_test_w("my-data-type", None, None, ('~/.weaver/files/my-data-type', '~/.weaver/data/my-data-type.tsplx'))

    get_target_test_w("my-data-type", None, ".hidden_file.tsplx",                     ('~/.weaver/files/my-data-type', '~/.weaver/data/.hidden_file.tsplx'))
    get_target_test_w("my-data-type", None, "subdir/my_custom_data_file.tsplx",       ('~/.weaver/files/my-data-type', '~/.weaver/data/subdir/my_custom_data_file.tsplx'))
    get_target_test_w("my-data-type", None, "/opt/my-app/app-type/somename.tsplx",    ('~/.weaver/files/my-data-type', '/opt/my-app/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", None, "~/.app-dir/app-type/somename.tsplx",     ('~/.weaver/files/my-data-type', '~/.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", None, "./.app-dir/app-type/somename.tsplx",     ('~/.weaver/files/my-data-type', './.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", None, "../../.app-dir/app-type/somename.tsplx", ('~/.weaver/files/my-data-type', '../../.app-dir/app-type/somename.tsplx'))

    get_target_test_w("my-data-type", ".hidden-folder",        None, ('~/.weaver/files/.hidden-folder',        '~/.weaver/data/my-data-type.tsplx'))
    get_target_test_w("my-data-type", "subdir/my-data-folder", None, ('~/.weaver/files/subdir/my-data-folder', '~/.weaver/data/my-data-type.tsplx'))
    get_target_test_w("my-data-type", "~/.app/files/",         None, ('~/.app/files',                          '~/.weaver/data/my-data-type.tsplx'))
    get_target_test_w("my-data-type", "/opt/app/files/",       None, ('/opt/app/files',                        '~/.weaver/data/my-data-type.tsplx'))
    get_target_test_w("my-data-type", "./subdir/files/",       None, ('./subdir/files',                        '~/.weaver/data/my-data-type.tsplx'))
    get_target_test_w("my-data-type", "../../subdir/files/",   None, ('../../subdir/files',                    '~/.weaver/data/my-data-type.tsplx'))

    get_target_test_w("my-data-type", ".hidden-folder", ".hidden_file.tsplx",                     ('~/.weaver/files/.hidden-folder', '~/.weaver/data/.hidden_file.tsplx'))
    get_target_test_w("my-data-type", ".hidden-folder", "subdir/my_custom_data_file.tsplx",       ('~/.weaver/files/.hidden-folder', '~/.weaver/data/subdir/my_custom_data_file.tsplx'))
    get_target_test_w("my-data-type", ".hidden-folder", "/opt/my-app/app-type/somename.tsplx",    ('~/.weaver/files/.hidden-folder', '/opt/my-app/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", ".hidden-folder", "~/.app-dir/app-type/somename.tsplx",     ('~/.weaver/files/.hidden-folder', '~/.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", ".hidden-folder", "./.app-dir/app-type/somename.tsplx",     ('~/.weaver/files/.hidden-folder', './.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", ".hidden-folder", "../../.app-dir/app-type/somename.tsplx", ('~/.weaver/files/.hidden-folder', '../../.app-dir/app-type/somename.tsplx'))

    get_target_test_w("my-data-type", "subdir/my-data-folder", ".hidden_file.tsplx",                     ('~/.weaver/files/subdir/my-data-folder', '~/.weaver/data/.hidden_file.tsplx'))
    get_target_test_w("my-data-type", "subdir/my-data-folder", "subdir/my_custom_data_file.tsplx",       ('~/.weaver/files/subdir/my-data-folder', '~/.weaver/data/subdir/my_custom_data_file.tsplx'))
    get_target_test_w("my-data-type", "subdir/my-data-folder", "/opt/my-app/app-type/somename.tsplx",    ('~/.weaver/files/subdir/my-data-folder', '/opt/my-app/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "subdir/my-data-folder", "~/.app-dir/app-type/somename.tsplx",     ('~/.weaver/files/subdir/my-data-folder', '~/.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "subdir/my-data-folder", "./.app-dir/app-type/somename.tsplx",     ('~/.weaver/files/subdir/my-data-folder', './.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "subdir/my-data-folder", "../../.app-dir/app-type/somename.tsplx", ('~/.weaver/files/subdir/my-data-folder', '../../.app-dir/app-type/somename.tsplx'))

    get_target_test_w("my-data-type", "~/.app/files/", ".hidden_file.tsplx",                     ('~/.app/files', '~/.weaver/data/.hidden_file.tsplx'))
    get_target_test_w("my-data-type", "~/.app/files/", "subdir/my_custom_data_file.tsplx",       ('~/.app/files', '~/.weaver/data/subdir/my_custom_data_file.tsplx'))
    get_target_test_w("my-data-type", "~/.app/files/", "/opt/my-app/app-type/somename.tsplx",    ('~/.app/files', '/opt/my-app/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "~/.app/files/", "~/.app-dir/app-type/somename.tsplx",     ('~/.app/files', '~/.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "~/.app/files/", "./.app-dir/app-type/somename.tsplx",     ('~/.app/files', './.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "~/.app/files/", "../../.app-dir/app-type/somename.tsplx", ('~/.app/files', '../../.app-dir/app-type/somename.tsplx'))

    get_target_test_w("my-data-type", "/opt/app/files/", ".hidden_file.tsplx",                     ('/opt/app/files', '~/.weaver/data/.hidden_file.tsplx'))
    get_target_test_w("my-data-type", "/opt/app/files/", "subdir/my_custom_data_file.tsplx",       ('/opt/app/files', '~/.weaver/data/subdir/my_custom_data_file.tsplx'))
    get_target_test_w("my-data-type", "/opt/app/files/", "/opt/my-app/app-type/somename.tsplx",    ('/opt/app/files', '/opt/my-app/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "/opt/app/files/", "~/.app-dir/app-type/somename.tsplx",     ('/opt/app/files', '~/.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "/opt/app/files/", "./.app-dir/app-type/somename.tsplx",     ('/opt/app/files', './.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "/opt/app/files/", "../../.app-dir/app-type/somename.tsplx", ('/opt/app/files', '../../.app-dir/app-type/somename.tsplx'))

    get_target_test_w("my-data-type", "./subdir/files/", ".hidden_file.tsplx",                     ('./subdir/files', '~/.weaver/data/.hidden_file.tsplx'))
    get_target_test_w("my-data-type", "./subdir/files/", "subdir/my_custom_data_file.tsplx",       ('./subdir/files', '~/.weaver/data/subdir/my_custom_data_file.tsplx'))
    get_target_test_w("my-data-type", "./subdir/files/", "/opt/my-app/app-type/somename.tsplx",    ('./subdir/files', '/opt/my-app/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "./subdir/files/", "~/.app-dir/app-type/somename.tsplx",     ('./subdir/files', '~/.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "./subdir/files/", "./.app-dir/app-type/somename.tsplx",     ('./subdir/files', './.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "./subdir/files/", "../../.app-dir/app-type/somename.tsplx", ('./subdir/files', '../../.app-dir/app-type/somename.tsplx'))

    get_target_test_w("my-data-type", "../../subdir/files/", ".hidden_file.tsplx",                     ('../../subdir/files', '~/.weaver/data/.hidden_file.tsplx'))
    get_target_test_w("my-data-type", "../../subdir/files/", "subdir/my_custom_data_file.tsplx",       ('../../subdir/files', '~/.weaver/data/subdir/my_custom_data_file.tsplx'))
    get_target_test_w("my-data-type", "../../subdir/files/", "/opt/my-app/app-type/somename.tsplx",    ('../../subdir/files', '/opt/my-app/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "../../subdir/files/", "~/.app-dir/app-type/somename.tsplx",     ('../../subdir/files', '~/.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "../../subdir/files/", "./.app-dir/app-type/somename.tsplx",     ('../../subdir/files', './.app-dir/app-type/somename.tsplx'))
    get_target_test_w("my-data-type", "../../subdir/files/", "../../.app-dir/app-type/somename.tsplx", ('../../subdir/files', '../../.app-dir/app-type/somename.tsplx'))

    test_pop()

def mfd_multiple_new_test():
    reset_id_generator()

    test_push(f'{get_function_name()}')
    ensure_dir(test_base)

    scnr = MultiFileDocument(test_base)
    mfd_test_create_page_file.cnt = 0

    mfd_new_test(scnr)
    expected = [
        'scn_JM3CRQJFQH.jpg',
    ]
    mfd_test(scnr, expected)

    mfd_new_test(scnr)
    expected.append('scn_W8R6F65XCV.jpg')
    mfd_test(scnr, expected)

    mfd_new_test(scnr)
    expected.append('scn_X6F54GQV5H.jpg')
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected = [
        'scn_JM3CRQJFQH.jpg',
        'scn_W8R6F65XCV.jpg',
        'scn_X6F54GQV5H.1.jpg',
        'scn_X6F54GQV5H.2.jpg',
    ]
    mfd_test(scnr, expected)

    mfd_new_test(scnr)
    expected.append('scn_MGX8VQPRC3.jpg')
    mfd_test(scnr, expected)

    test_pop()
    shutil.rmtree (test_base)

def mfd_test1():
    reset_id_generator()

    #test_force_output()
    test_push(f'{get_function_name()}')
    ensure_dir(test_base)

    scnr = MultiFileDocument(test_base)
    mfd_test_create_page_file.cnt = 0

    mfd_new_test(scnr)
    expected = [
        'scn_JM3CRQJFQH.jpg',
    ]
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected = [
        'scn_JM3CRQJFQH.1.jpg',
        'scn_JM3CRQJFQH.2.jpg',
    ]
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.3.jpg')
    mfd_test(scnr, expected)

    mfd_end_section_test(scnr)
    expected = [
        'scn_JM3CRQJFQH.1.1.jpg',
        'scn_JM3CRQJFQH.1.2.jpg',
        'scn_JM3CRQJFQH.1.3.jpg',
    ]
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.1.jpg')
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.2.jpg')
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.3.1.jpg')
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.3.2.jpg')
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    expected = [
        'scn_JM3CRQJFQH.1.1.1.jpg',
        'scn_JM3CRQJFQH.1.1.2.jpg',
        'scn_JM3CRQJFQH.1.1.3.jpg',
        'scn_JM3CRQJFQH.1.2.1.jpg',
        'scn_JM3CRQJFQH.1.2.2.jpg',
        'scn_JM3CRQJFQH.1.3.1.jpg',
        'scn_JM3CRQJFQH.1.3.2.jpg',
    ]
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.1.1.jpg')
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.1.2.jpg')
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.1.3.jpg')
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.2.1.jpg')
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.2.2.jpg')
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.3.1.jpg')
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.3.2.jpg')
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.3.1.1.jpg')
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    mfd_test(scnr, expected)

    mfd_end_section(scnr)
    expected = [
        'scn_JM3CRQJFQH.1.1.1.1.jpg',
        'scn_JM3CRQJFQH.1.1.1.2.jpg',
        'scn_JM3CRQJFQH.1.1.1.3.jpg',
        'scn_JM3CRQJFQH.1.1.2.1.jpg',
        'scn_JM3CRQJFQH.1.1.2.2.jpg',
        'scn_JM3CRQJFQH.1.1.3.1.jpg',
        'scn_JM3CRQJFQH.1.1.3.2.jpg',
        'scn_JM3CRQJFQH.1.2.1.1.jpg',
        'scn_JM3CRQJFQH.1.2.1.2.jpg',
        'scn_JM3CRQJFQH.1.2.1.3.jpg',
        'scn_JM3CRQJFQH.1.2.2.1.jpg',
        'scn_JM3CRQJFQH.1.2.2.2.jpg',
        'scn_JM3CRQJFQH.1.2.3.1.jpg',
        'scn_JM3CRQJFQH.1.2.3.2.jpg',
        'scn_JM3CRQJFQH.1.3.1.1.jpg',
    ]
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected.append('scn_JM3CRQJFQH.2.1.1.1.jpg')
    mfd_test(scnr, expected)

    test_pop()
    shutil.rmtree (test_base)

def mfd_test2():
    reset_id_generator()

    #test_force_output()
    test_push(f'{get_function_name()}')
    ensure_dir(test_base)

    scnr = MultiFileDocument(test_base)
    mfd_test_create_page_file.cnt = 0

    mfd_new_test(scnr)
    expected = [
        'scn_JM3CRQJFQH.jpg',
    ]
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected = [
        'scn_JM3CRQJFQH.1.jpg',
        'scn_JM3CRQJFQH.2.jpg',
    ]
    mfd_test(scnr, expected)

    mfd_new_test(scnr)
    expected.append('scn_W8R6F65XCV.jpg')
    mfd_test(scnr, expected)

    mfd_new_test(scnr)
    expected.append('scn_X6F54GQV5H.jpg')
    mfd_test(scnr, expected)

    mfd_new_page_test(scnr)
    expected = [
        'scn_JM3CRQJFQH.1.jpg',
        'scn_JM3CRQJFQH.2.jpg',
        'scn_W8R6F65XCV.jpg',
        'scn_X6F54GQV5H.1.jpg',
        'scn_X6F54GQV5H.2.jpg',
    ]
    mfd_test(scnr, expected)

    test_pop()
    shutil.rmtree (test_base)

def mfd_tests():
    test_push("Multi File Document")

    mfd_multiple_new_test()
    mfd_test1()
    mfd_test2()

    test_pop()


def file_utility():
    test_push("file_utility.py")

    canonical_file_name_tests()
    canonical_rename_tests()
    canonical_move_tests()
    get_target_tests()
    mfd_tests()
    canonical_renumber_tests()
    file_canonical_rename_tests()

    test_pop()

#########
#  API

def autolink_test(node_id, q, expected_target):
    test_push(f'q={q}')
    url = f"http://localhost:9000/autolink?q={q}&n={node_id}"

    response = requests.get(url, allow_redirects=False)
    success = test (response.status_code, 302, 'Is redirect')

    success = test (response.headers.get('Location'), expected_target, 'Expected redirect location')
    if not success:
        actual = response.headers.get('Location')
        if actual == None:
            actual = '{None}'

        test_error(actual + ' != ' + expected_target)

    test_pop()

# TODO: How can we do this only using Python's standard library?, so we can
# include it in mkpy/utility.py
def pid_is_running(pid):        
    """
    Check if process is running.
    """

    try:
        process = psutil.Process(pid)
        return process.status() != psutil.STATUS_ZOMBIE
    except psutil.NoSuchProcess:
        return False

def server_start(weaver_server_home):
    log = f'{weaver_server_home}/server.log'
    ensure_dir (weaver_server_home)
    ex(f'cp tests/example/config.json {weaver_server_home}')
    pid = ex_bg (f'python3 ../../server/main.py', log=log, quiet=True, cwd=weaver_server_home, env={"PYTHONPATH": "../../", "WEAVERHOME": '.'})
    time.sleep(0.5)

    if pid_is_running(pid):
        print (f'Started API server, PID: {pid}')
        return pid
    else:
        ex (f'cat {log}')
        print (ecma_red('error:'), 'server failed to start')
        return 0

def server_stop(pid):
    if pid_is_running(pid):
        print (f'Stopping API server, PID: {pid}')
        ex_bg_kill (pid, echo=False)

def api_public_data(weaver_server_home):
    test_push(f'API (public data)')

    pid = server_start(weaver_server_home)
    success = test (pid > 0, True, 'Server Start')
    if not success:
        test_error('Could not start weaver server.')

    if success:
        test_push(f'autolink')
        node_id = "FRR94HG5FP"
        autolink_test (node_id, "NotInWeaver", "https://google.com/search?q=NotInWeaver");
        autolink_test (node_id, "Sample PKB", "http://localhost:8000?n=68W5X99W59");
        
        # TODO: Test a private page and check that it doesn't resolve in the weaver map.

        # Non existent id
        autolink_test ("RP6Q62PG7F", "Sample PKB", "https://google.com/search?q=Sample%20PKB");
        test_pop()

        server_stop (pid)

    test_pop()

def api_full_data(weaver_server_home):
    test_push(f'API (full data)')

    pid = server_start(weaver_server_home)
    success = test (pid > 0, True, 'Server Start')
    if not success:
        test_error('Could not start weaver server.')

    if success:
        test_push(f'autolink')
        node_id = "FRR94HG5FP"
        autolink_test (node_id, "NotInWeaver", "https://google.com/search?q=NotInWeaver");
        autolink_test (node_id, "Sample PKB", "http://localhost:8000?n=68W5X99W59");

        # Non existent id
        autolink_test ("RP6Q62PG7F", "Sample PKB", "https://google.com/search?q=Sample%20PKB");
        test_pop()

        server_stop (pid)

    test_pop()

###############
#  Static Site

def test_dir(dir1, dir2):
    """
    Recursively tests that two directories are equal.
    """

    dirs_cmp = filecmp.dircmp(dir1, dir2)
    equal = True

    for file in dirs_cmp.left_only:
        test_error(f"File {path_cat(dir1, file)} exists only in {dir1}")
        test_error(f"cp {path_cat(dir1, file)} {dir2}")
        test_error(f"nvim -- -O {path_cat('./tests/example/notes/', file)} {path_cat(dir2, file)}")
        equal = False

    for file in dirs_cmp.right_only:
        test_error(f"File {path_cat(dir2, file)} exists only in {dir2}")
        test_error(f"cp {path_cat(dir2, file)} {dir1}")
        test_error(f"nvim -- -O {path_cat('./tests/example/notes/', file)} {path_cat(dir1, file)}")
        equal = False

    for file in dirs_cmp.diff_files:
        # FIXME: Nasty hack just to have correct paths in the command. I should
        # rework these tests to use code closer to the same we use to generate,
        # but to assemble the full expected result, then compare the full
        # generator's output not just the data and pages. Make that code track
        # the original location of the sourcecode of the expected files.
        src_dir = dir2
        if dir2 == './bin/full_static_expected':
            src_dir = './tests/example.full'
        elif dir2 == './bin/full_static_expected/notes':
            src_dir = './tests/example.full/notes'

        test_error(f"{path_cat(dir1, file)} != {path_cat(src_dir, file)}")
        test_error(f"nvim -- -d {path_cat(dir1, file)} {path_cat(src_dir, file)} -c 'wincmd l | normal! zR'")
        equal = False

    for common_dir in dirs_cmp.common_dirs:
        dir1_path = path_cat(dir1, common_dir)
        dir2_path = path_cat(dir2, common_dir)
        if not test_dir(dir1_path, dir2_path):
            equal = False

    return equal

def static_site_test(source, target, expected, public=False):
    reset_id_generator()
    public_str = ''
    if public:
        public_str = '--public'
    ex (f'./bin/weaver generate --home {source} --output-dir {target} --static {public_str} --deterministic', echo=False)
    success = test_dir(target, expected)
    return success

def data_to_autolink_map(data, target):
    with open(data) as data_json:
        data = json.load(data_json)

    autolink_map = {}
    for key, value in data.items():
        lowercase_name = value['name'].lower()
        autolink_map[lowercase_name] = key

    ensure_dir (path_dirname(target))
    with open(target, 'w') as target_file:
        json.dump(autolink_map, target_file)

    return data
    

###############
#  Entrypoint

def tests():
    """
    Tests to be created
     - Test correct attachment detection to entity and to file.
     - Test that we can't store a file in attachment mode if there's already an attachment.
         run: /pymk.py file_store --vim --attach '/home/santiago/.weaver/notes/33882JV8VM'
         expected: Target already has an attachment.
    """

    #test_force_output()

    #test_push(f'TSPLX')
    #output = ex('bin/tsplx_parser_tests', ret_stdout=True, echo=False)
    #test_error(output, indent=0)
    #test_pop(False)

    #test_push(f'PSPLX')
    #output = ex('bin/psplx_parser_tests', ret_stdout=True, echo=False)
    #test_error(output, indent=0)
    #test_pop(False)

    file_utility()


    static_target_full = './bin/full_static'
    static_target_public = './bin/public_static'

    if path_exists(static_target_full):
        shutil.rmtree(static_target_full)
    if path_exists(static_target_public):
        shutil.rmtree(static_target_public)


    test_push(f'Public static example')
    success = static_site_test ('./tests/example', static_target_public, './tests/example.public', public=True)
    test_pop(success)

    server_home_public = './bin/.weaver_public'
    data_to_autolink_map(f'{static_target_public}/data.json', f'{server_home_public}/files/map/Q976XFMWMW.json')
    api_public_data(server_home_public)
    shutil.rmtree (server_home_public)


    test_push(f'Full static example')
    # To avoid having to add all public pages both in the public and full
    # expected outputs, we always use the public one as a base, then the full
    # expected only contains files that will change its rendering from full to
    # public.
    expected_full = './bin/full_static_expected'
    ex (f'cp -r ./tests/example.public/ {expected_full}', echo=False)
    ex (f'cp -r ./tests/example.full/* {expected_full}', echo=False)
    success = static_site_test ('./tests/example', static_target_full, expected_full)
    shutil.rmtree (expected_full)
    test_pop(success)

    server_home_full = './bin/.weaver_full'
    data_to_autolink_map(f'{static_target_full}/data.json', f'{server_home_full}/files/map/Q976XFMWMW.json')
    api_full_data(server_home_full)
    shutil.rmtree (server_home_full)


    if success:
        shutil.rmtree (static_target_public)
        shutil.rmtree (static_target_full)
