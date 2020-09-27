import glob
from mkpy.utility import *

def get_note_maps(notes_dir):
    note_to_id = {}
    id_to_title = {}

    folder = os.fsencode(notes_dir)
    for fname_os in os.listdir(folder):
        fname = os.fsdecode(fname_os)

        note_path = path_cat(notes_dir, fname)
        note_f = open(note_path, 'r')
        note_title = note_f.readline()[2:-1] # Remove starting "# " and ending \n

        note_to_id[note_title] = fname
        id_to_title[fname] = note_title

        note_f.close()

    return note_to_id, id_to_title

def replace_subpath(path, old_path, new_path):
    return path.replace(old_path.strip(os.sep), new_path.strip(os.sep), 1)

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

    install_files(file_dict, '')
