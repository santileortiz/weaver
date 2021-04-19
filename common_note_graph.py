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

def get_orphans(notes_dir, note_titles_in):
    """
    This is a very quick and dirty implementation, it's O(n^2)!!! In reality we
    should compute this when we build the HTML, while we are parsing all the
    notes, then we should build a quick query datastructure for this
    information, maybe a graph?, just a list?, this data will be necessary to
    implement back references anyway. Right now I do it here just to pay the
    O(n^2) cost of the ugly implementation at build time, while keeping runtime
    fast.
    """

    note_titles = [n.strip() for n in note_titles_in]
    orphan_names = note_titles[:]

    folder = os.fsencode(notes_dir)
    for fname_os in os.listdir(folder):
        fname = os.fsdecode(fname_os)

        note_path = path_cat(notes_dir, fname)
        note_f = open(note_path, 'r')
        note_data = note_f.read()

        for note_title in note_titles:
            note_title = note_title
            search_str = "\\note{" + str(note_title) + "}"
            if note_title in orphan_names and search_str in note_data:
                orphan_names.remove(note_title)

        note_f.close()

    return orphan_names

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

    install_files(file_dict, '')
