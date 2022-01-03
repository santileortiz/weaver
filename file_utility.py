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

def canonical_move (source, target, prefix=None, ordered=False, position=None, dry_run=False, verbose=False):
    ensure_dir (target)

    for dirpath, dirnames, filenames in os.walk(source):
        i = 1
        filenames = natsorted (filenames)

        for fname in filenames:
            f_base, f_extension = path_split (fname)

            file_id = new_file_id ()

            prefix_str = ''
            if prefix != None:
                prefix_str = prefix + '_'

            if ordered:
                new_fname = f"{prefix_str}{i}_{file_id}{f_extension.lower()}"
                i += 1
            else:
                new_fname = f"{prefix_str}{file_id}{f_extension.lower()}"

            if verbose:
                print (f'{path_cat(dirpath, fname)} -> {new_fname}')

            if not dry_run:
                tgt_path = f'{path_cat(target, new_fname)}'
                if not path_exists (tgt_path):
                    os.rename (f'{path_cat(dirpath, fname)}', tgt_path)
                else:
                    print (ecma_red('error:') + f' target already exists: {tgt_path}')

def canonical_rename (path, prefix=None, ordered=False, dry_run=True):
    canonical_move (path, path, prefix=prefix, ordered=ordered, dry_run=dry_run)

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

def canonical_move_test(file_map, name, ordered=False):
    # Make ID generation reproducible
    random.seed (0)

    test_base = 'bin/test_tree/'
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
    canonical_move (src_path, tgt_path, ordered=ordered)

    test_push(name)

    success = test (path_to_dict(src_path), {}, 'Source tree is empty')
    success = test (path_to_dict(tgt_path), result_dict, 'Target tree is correct')
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
    canonical_move_test (rename_test, 'canonical_move()')

    rename_test = {
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
    canonical_move_test (rename_test, 'ordered canonical_move()', ordered=True)
