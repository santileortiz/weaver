from mkpy.utility import *
from file_utility import *

from natsort import natsorted
import random

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
    #test_push ('canonical_rename()')

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

    #test_pop()

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

def mfd_multiple_new_test():
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
    mfd_multiple_new_test()
    mfd_test1()
    mfd_test2()

def tests():
    #test_force_output()

    canonical_file_name_tests()
    canonical_rename_tests()
    canonical_move_tests()

    mfd_tests()
