let screen_height = document.documentElement.clientHeight;
let header_height = document.getElementById("header").offsetHeight;
css_property_set ("--note-container-height", screen_height - header_height + "px");

let collapsed_note_width = 40 // px
css_property_set ("--collapsed-note-width", collapsed_note_width + "px");

let expanded_note_width = 620 // px
css_property_set ("--expanded-note-width", expanded_note_width + "px");

let content_width = 588 // px
css_property_set ("--content-width", content_width + "px");
css_property_set ("--content-padding", (expanded_note_width - content_width)/2 + "px");

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

// There doesn't seem to be a way to get the scrollbar's width in all cases. If
// the browser has hidden scrollbars we can't use the trick of creating hidden
// divs that force scrollbars and compute the difference between the client and
// offsed widths. So... we just set a fixed value of what we think should be
// the maximum width of a scrollbar.
//
// TODO: Probably a better approach will be to have our own scrollbar style
// with a known width.
let max_scrollbar_width = 15 // px

let opened_notes = [];

function note_text_to_element (container, id, note_html, x)
{
    container.insertAdjacentHTML ('beforeend', note_html);

    let new_expanded_note = document.getElementById(id);
    new_expanded_note.id = id
    new_expanded_note.classList.add("note")
    new_expanded_note.style.left = x + "px"
    new_expanded_note.classList.add("expanded");

    // Create button to start editing the note
    {
        let margin = 5

        let new_edit_button_icon = document.createElement("img")
        new_edit_button_icon.src = "edit.svg"

        let new_edit_button = document.createElement("button")
        new_edit_button.style.position = "absolute"
        new_edit_button.style.top = margin + "px"
        new_edit_button.setAttribute("onclick", "copy_note_cmd('" + id + "');")

        new_edit_button.style.left = expanded_note_width - margin - note_button_width + "px";

        new_edit_button.appendChild(new_edit_button_icon)

        // Insert at the start
        new_expanded_note.insertBefore(new_edit_button, new_expanded_note.firstChild)
    }

    // The width of the note element contains the width of the scrollbar. If
    // there is a scrollbar make the note element wider to compensate. This way
    // the "content" (never including the scrollbar's width), will be
    // expanded_note_width.
    //
    // TODO: What I now thing would be better is to put all the remaining space
    // allocated to the note, so that we can have a big editor UI like Notion
    // does. Then the additional spacing added by the scrollbar won't be
    // noticeable.
    {
        // :element_size_computation
        if (new_expanded_note.scrollHeight != new_expanded_note.clientHeight) {
            //// If the note has a scrollbar then move the button to the left.
            css_property_set ("--expanded-note-width", expanded_note_width + max_scrollbar_width + "px");
        } else {
            css_property_set ("--expanded-note-width", expanded_note_width + "px");
        }
    }

    return new_expanded_note;
}

function copy_note_cmd (id)
{
    let dummy_input = document.createElement("input");
    // Is this necessary?...
    document.body.appendChild(dummy_input);

    dummy_input.value = "gvim ~/.weaver/notes/" + id
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

// NOTE: This pushes a new state. Intended for use in places where we handle
// user interaction that causes navigation to a new note.
// :pushes_state
function reset_and_open_note(note_id)
{
    get_note_and_run (note_id,
        function(response) {
            let note_container = document.getElementById("note-container")
            note_container.innerHTML = ''
            opened_notes = []
            note_text_to_element(note_container, note_id, response, opened_notes.length*collapsed_note_width)
            opened_notes.push(note_id)
            history.pushState(null, "", "?n=" + opened_notes.join("&n="))
        }
    )

    return false
}

function collapsed_element_new (note_id)
{
    let collapsed_title = document.createElement("h3")
    collapsed_title.classList.add("collapsed-label")
    collapsed_title.innerHTML = id_to_note_title[note_id]

    return collapsed_title
}

// :pushes_state
function open_note(note_id)
{
    if (opened_notes.includes(note_id)) {
        let params = url_parameters();
        open_notes (params["n"], note_id);

    } else {
        let expanded_note = document.querySelector(".expanded");
        expanded_note.classList.remove ("expanded");
        expanded_note.classList.add("collapsed");
        expanded_note.classList.add("right-shadow");
        expanded_note.innerHTML = '';

        let collapsed_title = collapsed_element_new (expanded_note.id);
        expanded_note.appendChild(collapsed_title);

        get_note_and_run (note_id,
            function(response) {
                let note_container = document.getElementById("note-container");
                note_text_to_element(note_container, note_id, response, opened_notes.length*collapsed_note_width);
                opened_notes.push(note_id);
                history.pushState(null, "", "?n=" + opened_notes.join("&n="));
            }
        );
    }

    return false;
}

// Unlike reset_and_open_note() and open_note(), this one doesn't push a state.
// It's intended to be used in handlers for events that need to change the
// openened notes.
function open_notes(note_ids, expanded_note_id)
{
    opened_notes = note_ids

    if (expanded_note_id === undefined) {
        expanded_note_id = note_ids[note_ids.length - 1] 
    }

    get_note_and_run (expanded_note_id,
        function(response) {
            let note_container = document.getElementById("note-container")
            note_container.innerHTML = ''

            let i = 0
            for (; note_ids[i] !== expanded_note_id; i++) {
                let new_collapsed_note = note_element_new(note_ids[i], i*collapsed_note_width)
                new_collapsed_note.classList.add("collapsed")
                new_collapsed_note.classList.add("right-shadow")
                let collapsed_title = collapsed_element_new(note_ids[i])
                new_collapsed_note.appendChild(collapsed_title)

                note_container.appendChild(new_collapsed_note)
            }

            note_text_to_element(note_container, expanded_note_id, response, i*collapsed_note_width)
            i++

            for (; i<note_ids.length; i++) {
                let new_collapsed_note = note_element_new(note_ids[i], (i-1)*collapsed_note_width + expanded_note_width)
                new_collapsed_note.classList.add("collapsed")
                new_collapsed_note.classList.add("left-shadow")
                let collapsed_title = collapsed_element_new(note_ids[i])
                new_collapsed_note.appendChild(collapsed_title)

                note_container.appendChild(new_collapsed_note)
            }

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
