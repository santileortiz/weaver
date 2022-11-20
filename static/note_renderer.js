// For reference, previously when I needed to make calculations in javascript
// based on a CSS variable value I would define the variable like this:
//let content_width = 708 // px
//css_property_set ("--content-width", content_width + "px");

let note_link_color = "#0D52BF"
css_property_set ("--note-link-color", note_link_color);
css_property_set ("--note-link-color-bg", note_link_color + "0F");
css_property_set ("--note-link-color-sh", note_link_color + "1F");

let code_color = "#FDF6E3"
css_property_set ("--code-block-color", code_color);
css_property_set ("--code-inline-color", code_color);
css_property_set ("--code-inline-border-color", alpha_blend(code_color, "rgba(0,0,0,0.05)"));

let note_button_width = 22 // px
css_property_set ("--note-button-width", note_button_width + "px");

let code_block_padding = 12 // px
css_property_set ("--code-block-padding", code_block_padding + "px");

let opened_notes = [];

function note_has_scroll()
{
    // NOTE: Probably breaks [1] if for some reason overflow is set to visible. But
    // I don't think we will eve set visible overflow for notes.
    //
    // [1]: https://stackoverflow.com/questions/143815/determine-if-an-html-elements-content-overflows
    let note_container = document.getElementById("note-container");
    return note_container.clientHeight < note_container.scrollHeight;
}

function toggle_sidebar ()
{
    var sidebar = document.getElementById("sidebar");
    
    if (sidebar.classList.contains("hidden")) {
        sidebar.setAttribute("style", "transform: translate(0); width: 240px;")
        sidebar.classList.remove ("hidden");

    } else {
        sidebar.setAttribute("style", "transform: translate(-240px); width: 0;")
        sidebar.classList.add("hidden")
    }
}

function note_text_to_element (container, id, note_html)
{
    container.insertAdjacentHTML('beforeend', note_html);

    let new_expanded_note = document.getElementById(id);
    new_expanded_note.id = id
    new_expanded_note.classList.add("note")

    if (note_has_scroll()) {
        new_expanded_note.style.paddingBottom = "50vh";
    }

    return new_expanded_note;
}

function copy_note_cmd ()
{
    let dummy_input = document.createElement("input");
    // Is this necessary?...
    document.body.appendChild(dummy_input);

    dummy_input.value = "gvim ~/.weaver/notes/" + opened_notes[opened_notes.length-1]
    dummy_input.select();
    dummy_input.setSelectionRange(0, 99999);

    document.execCommand("copy");

    document.body.removeChild(dummy_input);

    // TODO: Show some feedback.
}

function get_note_and_run (note_id, callback)
{
    ajax_get ("notes/" + note_id,
        function(response) {
            callback (response);
        }
    )
}

function set_breadcrumbs()
{
    var breadcrumbs = document.getElementById("breadcrumbs");

    breadcrumbs.innerHTML = "";
    let i = 0
    for (; i<opened_notes.length; i++) {
        var note_id = opened_notes[i];

        if (i>0) {
            let sep = document.createElement("span");
            sep.classList.add("crumb-separator")
            sep.innerHTML = '/';
            breadcrumbs.appendChild(sep);
        }

        let crumb = document.createElement("div");
        crumb.classList.add("crumb")
        crumb.innerHTML = id_to_note_title[note_id];
        crumb.setAttribute("onclick", "open_note('" + note_id + "');")
        breadcrumbs.appendChild(crumb);
    }
}

// NOTE: This pushes a new state. Intended for use in places where we handle
// user interaction that causes navigation to a new note.
// :pushes_state
function reset_and_open_note(note_id)
{
    get_note_and_run (note_id,
        function(response) {
            opened_notes = []
            opened_notes.push(note_id)

            let note_container = document.getElementById("note-container")
            note_container.innerHTML = ''
            note_text_to_element(note_container, note_id, response)
            set_breadcrumbs();

            history.pushState(null, "", "?n=" + opened_notes.join("&n="))
        }
    )

    return false
}

// :pushes_state
function open_note(note_id)
{
    if (opened_notes.includes(note_id)) {
        let params = url_parameters();
        var idx = opened_notes.findIndex(e => e == note_id);
        opened_notes.length = idx + 1;

    } else {
        opened_notes.push(note_id);
    }

    let expanded_note = document.querySelector(".note");
    expanded_note.innerHTML = '';

    get_note_and_run (note_id,
        function(response) {
            let note_container = document.getElementById("note-container");
            note_container.innerHTML = ''
            note_text_to_element(note_container, note_id, response);
            set_breadcrumbs();

            history.pushState(null, "", "?n=" + opened_notes.join("&n="));
        }
    );

    return false;
}

// Unlike reset_and_open_note() and open_note(), this one doesn't push a state.
// It's intended to be used in handlers for events that need to change the
// openened notes.
function open_notes(note_ids)
{
    opened_notes = note_ids

    let note_id = note_ids[note_ids.length - 1] 

    get_note_and_run (note_id,
        function(response) {
            let note_container = document.getElementById("note-container")
            note_container.innerHTML = ''
            note_text_to_element(note_container, note_id, response)
            set_breadcrumbs();
        }
    )
}

function navigate_to_current_url()
{
    let params = url_parameters()
    if (params != null) {
        open_notes (params["n"])

    } else {
        // By default open the first note
        reset_and_open_note(title_notes[0])
    }
}

window.addEventListener('popstate', (event) => {
    navigate_to_current_url()
});

navigate_to_current_url()
