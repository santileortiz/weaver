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

let content_max_width = 600;
css_property_set ("--content-width", content_max_width + "px");

function is_desktop()
{
    return window.matchMedia(`screen and (min-width: ${content_max_width}px)`).matches;
}

function note_has_scroll()
{
    // NOTE: Probably breaks [1] if for some reason overflow is set to visible. But
    // I don't think we will eve set visible overflow for notes.
    //
    // [1]: https://stackoverflow.com/questions/143815/determine-if-an-html-elements-content-overflows
    let note_container = document.getElementById("note-container");
    return note_container.clientHeight < note_container.scrollHeight;
}

function sidebar_set_visible (is_visible)
{
    let sidebar_style;

    let topbar = document.getElementById("topbar");

    if (is_visible) {
        sidebar_style = "transform: translate(0); width: 240px; padding: calc(6px + var(--topbar-height)) 6px 6px 6px;";
        sidebar.classList.remove ("hidden");
        topbar.setAttribute("style", "padding-left:15px;");

    } else {
        sidebar_style =  "transform: translate(-240px); width: 0; padding: calc(6px + var(--topbar-height)) 6px 0 0;";
        sidebar.classList.add("hidden")
        topbar.removeAttribute("style");
    }

    if (!is_desktop()) {
        sidebar_style += "position: absolute;";
    }
    sidebar.setAttribute("style", sidebar_style)
}

function toggle_sidebar ()
{
    var sidebar = document.getElementById("sidebar");
    
    if (sidebar.classList.contains("hidden")) {
        sidebar_set_visible (true);

    } else {
        sidebar_set_visible (false);
    }
}

// TODO: Can this be simplified?, according to the HTML spec in the script
// element section, to run scripts they need to be added with the
// document.write() method instead of setting the innerHTML attribute.
function set_innerhtml_and_run_scripts(element, html) {
    element.innerHTML = html;

    for (let old_script of element.querySelectorAll("script")) {
        const new_script = document.createElement("script");

        for (let attr of old_script.attributes) {
            new_script.setAttribute(attr.name, attr.value);
        }

        const scriptText = document.createTextNode(old_script.innerHTML);
        new_script.appendChild(scriptText);

        old_script.parentNode.replaceChild(new_script, old_script);
    }
}

function get_title(id)
{
    if (data[id] !== undefined && data[id]["@type"] === "page") {
        return data[id]["name"];
    }

    return undefined;
}

function note_text_to_element (container, id, note_html)
{
    set_innerhtml_and_run_scripts(container, note_html);

    document.title = get_title(id);
    let new_note = document.getElementById(id);
    new_note.classList.add("note")

    if (is_desktop()) {
        new_note.setAttribute("style", `width: ${content_max_width}px; padding-left: 96px; padding-right:96px; box-sizing: content-box;`);
        sidebar_set_visible (true);
    } else {
        new_note.setAttribute("style", `width: 100%; padding-left: 24px; padding-right: 24px;`);
        sidebar_set_visible (false);
    }

    if (note_has_scroll()) {
        new_note.style.paddingBottom = "50vh";
    }

    return new_note;
}

function copy_note_cmd ()
{
    let dummy_input = document.createElement("input");
    // Is this necessary?...
    document.body.appendChild(dummy_input);
    let open_note = opened_notes[opened_notes.length-1]

    let psplx = "";
    if (open_note[0] === "virtual") {
        let unescaped = virtual_entities[open_note[1]].psplx;
        unescaped = unescaped.replace("\n", "\\n");
        unescaped = unescaped.replace("\"", "\\\"");
        unescaped = unescaped.replace("#", "\\#");
        psplx = ` -c ':r! printf "${unescaped}"'`;
    }

    dummy_input.value = `gvim ${home_path}/notes/` + open_note[1] + psplx
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
        if (i>0) {
            let sep = document.createElement("span");
            sep.classList.add("crumb-separator")
            sep.innerHTML = '/';
            breadcrumbs.appendChild(sep);
        }

        var note_type = opened_notes[i][0];
        var note_id = opened_notes[i][1];

        let crumb = document.createElement("div");
        crumb.classList.add("crumb")

        if (note_type === "normal") {
            crumb.innerHTML = get_title(note_id);
            crumb.setAttribute("onclick", "open_note('" + note_id + "');")
            breadcrumbs.appendChild(crumb);

        } else if (note_type === "virtual") {
            crumb.innerHTML = virtual_entities[note_id].title;
            crumb.setAttribute("onclick", "open_virtual_entity('" + note_id + "');")
            breadcrumbs.appendChild(crumb);
        }
    }

    // :hidden_scrollbar
    let breadcrumbs_clip = document.getElementById("breadcrumbs-clip");
    let scrollbar_width = breadcrumbs.clientHeight - breadcrumbs.offsetHeight;
    breadcrumbs_clip.style.top = scrollbar_width/2 + "px";
    breadcrumbs.style.bottom = scrollbar_width + "px";
    breadcrumbs.scrollLeft = breadcrumbs.scrollWidth;
}

function push_state ()
{
    let state = "";
    let is_first = true;
    for (let [type, id] of opened_notes) {
        if (is_first) {
            state += "?";
            is_first = false;
        } else {
            state += "&";
        }

        if (type === "normal") {
            state += "n=";
        } else {
            state += "v=";
        }

        state += id;
    }
    history.pushState(null, "", state);
    hashchange();
}

// NOTE: This pushes a new state. Intended for use in places where we handle
// user interaction that causes navigation to a new note.
// :pushes_state
function reset_and_open_note(note_id)
{
    get_note_and_run (note_id,
        function(response) {
            opened_notes = []
            opened_notes.push(["normal", note_id])

            let note_container = document.getElementById("note-container")
            note_text_to_element(note_container, note_id, response)
            set_breadcrumbs();
            push_state();
        }
    )

    return false
}

// :pushes_state
function open_virtual_entity(note_id)
{
    let opened_note_ids = opened_notes.map(e => e[1]);
    if (opened_note_ids.includes(note_id)) {
        let params = url_parameters();
        var idx = opened_note_ids.findIndex(e => e == note_id);
        opened_notes.length = idx + 1;

    } else {
        opened_notes.push(["virtual", note_id]);
    }

    let expanded_note = document.querySelector(".note");
    expanded_note.innerHTML = '';

    let note_container = document.getElementById("note-container");
    note_text_to_element(note_container, note_id, virtual_entities[note_id].html);
    set_breadcrumbs();
    push_state();

    return false;
}

// :pushes_state
function open_note(note_id)
{
    let opened_note_ids = opened_notes.map(e => e[1]);
    if (opened_note_ids.includes(note_id)) {
        let params = url_parameters();
        var idx = opened_note_ids.findIndex(e => e == note_id);
        opened_notes.length = idx + 1;

    } else {
        opened_notes.push(["normal", note_id]);
    }

    let expanded_note = document.querySelector(".note");
    expanded_note.innerHTML = '';

    get_note_and_run (note_id,
        function(response) {
            let note_container = document.getElementById("note-container");
            note_text_to_element(note_container, note_id, response);
            set_breadcrumbs();
            push_state();
        }
    );

    return false;
}

// Unlike reset_and_open_note() and open_note(), this one doesn't push a state.
// It's intended to be used in handlers for events that need to change the
// openened notes.
function navigate_to_current_url()
{
    let params = url_parameters()
    if (params != null) {
        opened_notes = [];
        for (let [key, note_id] of params[""]) {
            if (key === "n") {
                opened_notes.push(["normal", note_id]);
            } else if (key === "v") {
                opened_notes.push(["virtual", note_id]);
            }
        }

        let note_type, note_id;
        [note_type, note_id] = opened_notes[opened_notes.length - 1]

        if (note_type === "normal") {
            get_note_and_run (note_id,
                function(response) {
                    let note_container = document.getElementById("note-container")
                    note_text_to_element(note_container, note_id, response)
                    set_breadcrumbs();
                }
            )

        } else if (note_type === "virtual") {
            let note_container = document.getElementById("note-container")
            note_text_to_element(note_container, note_id, virtual_entities[note_id].html)
            set_breadcrumbs();
        }

    } else {
        // By default open the first note
        reset_and_open_note(title_notes[0])
    }
}

window.addEventListener('popstate', (event) => {
    navigate_to_current_url()
});

function fetch_data() {
  return new Promise((resolve, reject) => {
    fetch('data.json')
      .then(response => response.json())
      .then(data => {
        resolve(data);
      })
      .catch(error => {
        reject(error);
      });
  });
}

async function initialize() {
  try {
    const data = await fetch_data();
    window.data = data;

    let sidebar = document.getElementById("sidebar");
    for (let i=0; i<title_notes.length; i++) {
        let note_id = title_notes[i];
        let title_link = document.createElement("a");
        title_link.setAttribute("onclick", "return reset_and_open_note('" + note_id + "');");
        title_link.setAttribute("href", "?n=" + note_id );
        title_link.innerHTML = get_title(note_id);
        sidebar.appendChild(title_link);
    }

    navigate_to_current_url()

  } catch (error) {
    console.error('Error:', error);
  }
}

initialize();


// Implement hash navigation that avoids ID clashes between the user's headings
// and internal IDs the rest of the website may be using.
function hashchange() {
    // Only modify the hash target if it doesn't exist.
    if (document.querySelector(':target') != null) {
        return;
    }

    const hash = decodeURIComponent(location.hash.slice(1)).toLowerCase();
    const target_id = `user-content-${hash}`;
    const target = document.getElementById(target_id) || document.getElementsByName(target_id)[0];
    if (target) {
        const document = target.ownerDocument
        setTimeout(() => {
            if (document && document.defaultView) {
                target.scrollIntoView()
            }
        }, 0)
    }
}

window.addEventListener('hashchange', hashchange);

document.addEventListener('DOMContentLoaded', async function() {
  await new Promise(resolve => window.setTimeout(resolve, 0))
  hashchange()
})
