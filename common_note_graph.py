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
        note_title = note_f.readline()[2:-1].strip() # Remove starting "# ", ending \n and leading/trailing spaces

        note_to_id[note_title] = fname
        id_to_title[fname] = note_title

        note_f.close()

    return note_to_id, id_to_title

def contains_linking_tag(tag_name, note_title, note_data):
    search_str = "\\" + tag_name + "{" + str(note_title) + "}"
    return search_str in note_data

def get_note_graph(notes_dir, note_title_to_id, link_tags):
    """
    This is a very quick and dirty implementation, it's O(n^2) on the number of
    notes!!! In reality we should compute this when we build the HTML, while we
    are parsing all the notes, then we should build a quick query datastructure
    for this information, maybe a graph?, just a list?, this data will be
    necessary to implement back references anyway. Right now I do it here just
    to pay the O(n^2) cost of the ugly implementation at build time, while
    keeping runtime fast.
    """

    note_titles = note_title_to_id.keys()
    root_notes = set(note_titles)
    note_links = {}
    note_backlinks = {}

    folder = os.fsencode(notes_dir)
    for fname_os in os.listdir(folder):
        fname = os.fsdecode(fname_os)

        note_path = path_cat(notes_dir, fname)
        note_f = open(note_path, 'r')
        note_data = note_f.read()

        for note_title in note_titles:
            link_exists = False
            for tag in link_tags:
                if contains_linking_tag (tag, note_title, note_data):
                    link_exists = True
                    break

            if link_exists:
                target_id = note_title_to_id[note_title]
                if fname not in note_links:
                    note_links[fname] = []
                if note_title not in note_links[fname]:
                    note_links[fname].append (target_id)

                if target_id not in note_backlinks:
                    note_backlinks[target_id] = []
                if note_title not in note_backlinks[target_id]:
                    note_backlinks[target_id].append (fname)

                if note_title in root_notes:
                    root_notes.remove(note_title)

        note_f.close()

    return [note_title_to_id[title] for title in root_notes], note_links, note_backlinks

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
