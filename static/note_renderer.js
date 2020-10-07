let screen_height = document.documentElement.clientHeight;
let header_height = document.getElementById("header").offsetHeight;
css_property_set ("--note-container-height", screen_height - header_height + "px");

let collapsed_note_width = 40 // px
css_property_set ("--collapsed-note-width", collapsed_note_width + "px");

let expanded_note_width = 520 // px
css_property_set ("--expanded-note-width", expanded_note_width + "px");

let note_link_color = "#0d52bf"
css_property_set ("--note-link-color", note_link_color);
css_property_set ("--note-link-color-bg", note_link_color + "0f");
css_property_set ("--note-link-color-sh", note_link_color + "1f");

let note_button_width = 22 // px
css_property_set ("--note-button-width", note_button_width + "px");

// There doesn't seem to be a way to get the scrollbar's width in all cases. If
// the browser has hidden scrollbars we can't use the trick of creating hidden
// divs that force scrollbars and compute the difference between the client and
// offsed widths. So... we just set a fixed value of what we think should be
// the maximum width of a scrollbar.
let max_scrollbar_width = 15 // px

let opened_notes = []

function note_element_new(id, x)
{
    let new_note = document.createElement("div")
    new_note.id = id
    new_note.classList.add("note")
    new_note.style.left = x + "px"

    return new_note;
}

let TokenType = {
    UNKNOWN: 0,
    PARAGRAPH: 1,
    BULLET_LIST: 2,
    TAG: 3,
    OPERATOR: 4,
    SPACE: 5,
    TEXT: 6,
    EOF: 7
}

token_type_names = []
for (let e in TokenType) {
    if (TokenType.hasOwnProperty(e)) {
        token_type_names.push('' + e)
    }
}

function ParserState (str) {
    this.pos = 0,
    this.str = str
    this.value = ''
    this.type = TokenType.UNKNOWN
    this.error = false
    this.error_msg = false
    this.is_eof = false
    this.margin = 0
}

// NOTE: chars.search() doesn't work if chars contains \ because it assumes we
// are passing an incomplete regex, sigh..
function char_in_str (c, chars)
{
    for (let i=0; i<chars.length; i++) {
        if (chars.charAt(i) === c) {
            return true
        }
    }

    return false
}

function ps_next(ps) {
    let pos_is_operator = function(ps) {
        return char_in_str(ps.str.charAt(ps.pos), "#[]{}\n")
    }

    let pos_is_space = function(ps) {
        return char_in_str(ps.str.charAt(ps.pos), " \t")
    }

    let pos_is_eof = function(ps) {
        return ps.pos >= ps.str.length
    }

    if (pos_is_eof(ps)) {
        ps.is_eof = true

    } else if (ps.str.charAt(ps.pos) === "\n") {
        ps.pos++

        let pos_backup = ps.pos
        while (!pos_is_eof(ps) && pos_is_space(ps)) {
            ps.pos++
        }

        // Skip until we find something different to space or newline
        let margin = 0
        let multiple_newline = false
        while (!pos_is_eof(ps) && (pos_is_space(ps) || ps.str.charAt(ps.pos) === "\n")) {
            if (ps.str.charAt(ps.pos) === "\n") {
                multiple_newline = true
                margin = 0

            } else {
                margin++
            }
            ps.pos++
        }

        if (pos_is_eof(ps)) {
            ps.is_eof = true

        } else if (ps.str.charAt(ps.pos) === "-" || ps.str.charAt(ps.pos) === "*") {
            ps.pos++
            while (!pos_is_eof(ps) && pos_is_space(ps)) {
                ps.pos++
            }

            ps.type = TokenType.BULLET_LIST
            ps.value = null
            ps.margin = ps.pos - pos_backup

        } else if (multiple_newline) {
            ps.type = TokenType.PARAGRAPH
            ps.value = null
            ps.margin = margin

        } else {
            let start = ps.pos
            while (!pos_is_eof(ps) && pos_is_space(ps)) {
                ps.pos++
            }

            // Here is where single newline characters become a single space character.
            ps.value = " " + ps.str.substr(start, ps.pos - start)
            ps.type = TokenType.TEXT
        }

    } else if (ps.str.charAt(ps.pos) === "\\") {
        ps.pos++

        let start = ps.pos
        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps)) {
            ps.pos++
        }
        ps.value = ps.str.substr(start, ps.pos - start)
        ps.type = TokenType.TAG

    } else if (pos_is_operator(ps)) {
        ps.type = TokenType.OPERATOR
        ps.value = ps.str.charAt(ps.pos)
        ps.pos++

    } else if (pos_is_space(ps)) {
        ps.type = TokenType.SPACE
        ps.value = ps.str.charAt(ps.pos)
        ps.pos++

    } else {
        let start = ps.pos
        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps)) {
            ps.pos++
        }

        ps.value = ps.str.substr(start, ps.pos - start)
        ps.type = TokenType.TEXT
    }

    if (pos_is_eof(ps)) {
        ps.is_eof = true
        ps.type = TokenType.EOF
        ps.value = null
    }
}

function ps_match(ps, type, value)
{
    let match = false;

    if (type == ps.type) {
        if (value == null) {
            match = true;

        } else if (ps.value === value) {
                match = true;
        }
    }

    return match
}

function ps_expect(ps, type, value)
{
    ps_next (ps)
    if (!ps_match(ps, type, value)) {
        ps.error = true
        if (ps.type != type) {
            if (value == null) {
                ps.error_msg = sprintf("Expected token of type %s, got '%s' of type %s.",
                                       token_type_names[type], ps.value, token_type_names[ps.type]);
            } else {
                ps.error_msg = sprintf("Expected token '%s' of type %s, got '%s' of type %s.",
                                       value, token_type_names[type], ps.value, token_type_names[ps.type]);
            }

        } else {
            // Value didn't match.
            ps.error_msg = sprintf("Expected '%s', got '%s'.", value, ps.value);
        }
    }
}

function ps_consume_spaces (ps)
{
    ps_next (ps)
    while (!ps.is_eof && !ps.error && ps_match(ps, TokenType.SPACE, null)) {
        ps_next (ps)
    }
}

function ps_consume_spaces_or_newline (ps)
{
    ps_next (ps)
    while (!ps.is_eof && !ps.error &&
           (ps_match(ps, TokenType.SPACE, null) || ps_match(ps, TokenType.OPERATOR, "\n"))) {
        ps_next (ps)
    }
}

let ContextType = {
    ROOT: 0,
    PARAGRAPH: 1,
    LIST: 2,
    LIST_ITEM: 3
}

function ParseContext (type, margin, dom_element) {
    this.type = type
    this.margin = margin
    this.dom_element = dom_element

    this.list_type = null
}

function push_new_context (context_stack, type, margin, element_name)
{
    let curr_ctx = context_stack[context_stack.length - 1]
    let dom_element = document.createElement(element_name)
    curr_ctx.dom_element.appendChild(dom_element)
    let new_context = new ParseContext(type, margin, dom_element)
    context_stack.push(new_context)
    return new_context
}

function note_text_to_element (container, id, note_text, x)
{
    let new_expanded_note = note_element_new (id, x)
    new_expanded_note.classList.add("expanded")

    // NOTE: We append this at the beginning so we get the correct client and
    // scroll height at the end.
    // :element_size_computation
    container.appendChild(new_expanded_note)

    let ps = new ParserState(note_text)

    // Parse title
    let title = ""
    ps_expect (ps, TokenType.OPERATOR, "#")
    ps_consume_spaces (ps)
    while (!ps.is_eof && !ps.error && ((ps_match(ps, TokenType.TEXT, null) || ps_match(ps, TokenType.SPACE, null)))) {
        title += ps.value
        ps_next (ps)
    }
    let title_element = document.createElement("h1")
    title_element.innerHTML = title
    new_expanded_note.appendChild(title_element)

    // Parse note's content
    let FLAG = true
    let context_type = ContextType.CONSUME_SPACES_AND_NEWLINE
    let newline_count = 0
    let paragraph = ""
    let list_element = null
    let context_stack = [new ParseContext(ContextType.PARAGRAPH, 0, new_expanded_note)]
    while (!ps.is_eof && !ps.error) {
        if (!FLAG) {
            ps_next (ps)
        } else {
            FLAG = false
        }

        if (ps_match(ps, TokenType.PARAGRAPH, null)) {
            let curr_ctx = context_stack.pop()
            while (context_stack.length > 0 &&
                   (curr_ctx.margin > ps.margin || curr_ctx.type == ContextType.PARAGRAPH)) {
                curr_ctx = context_stack.pop()
            }
            context_stack.push(curr_ctx)

            push_new_context (context_stack, ContextType.PARAGRAPH, ps.margin, "p")

        } else if (ps_match(ps, TokenType.BULLET_LIST, null)) {
            let curr_ctx = context_stack.pop()
            while (context_stack.length > 0) {
                if (curr_ctx.type == ContextType.LIST || curr_ctx.type == ContextType.ROOT) {
                    break
                } else {
                    curr_ctx = context_stack.pop()
                }
            }
            context_stack.push(curr_ctx)

            if (curr_ctx.type != ContextType.LIST) {
                let list_context = push_new_context (context_stack, ContextType.LIST, ps.margin, "ul")
                list_context.list_type = TokenType.BULLET_LIST
            }

            push_new_context (context_stack, ContextType.LIST_ITEM, ps.margin, "li")
            push_new_context (context_stack, ContextType.PARAGRAPH, ps.margin, "p")

        } else if (ps_match(ps, TokenType.TEXT, null) || ps_match(ps, TokenType.SPACE, null)) {
            let curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.innerHTML += ps.value

        } else if (ps_match(ps, TokenType.TAG, "link")) {
            ps_expect (ps, TokenType.OPERATOR, "{")

            let content = ""
            ps_next(ps)
            while (!ps.is_eof && !ps.error && !ps_match(ps, TokenType.OPERATOR, "}")) {
                content += ps.value
                ps_next(ps)
            }

            // We parse the URL from the title starting at the end. I thinkg is
            // far less likely to have a non URL encoded > character in the
            // URL, than a user wanting to use > inside their title.
            //
            // TODO: Support another syntax for the rare case of a user that
            // wants a > character in a URL. Or make sure URLs are URL encoded
            // from the UI that will be used to edit this.
            let pos = content.length - 1
            while (pos > 1 && content.charAt(pos) != ">") {
                pos--
            }

            let url = content
            let title = content
            if (pos != 1 && content.charAt(pos - 1) === "-") {
                pos--
                url = content.substr(pos + 2).trim()
                title = content.substr(0, pos).trim()
            }

            let link_element = document.createElement("a")
            link_element.setAttribute("href", url)
            link_element.setAttribute("target", "_blank")
            link_element.innerHTML = title

            let curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.appendChild(link_element)

        } else if (ps_match(ps, TokenType.TAG, "note")) {
            ps_expect (ps, TokenType.OPERATOR, "{")

            let note_title = ""
            ps_next(ps)
            while (!ps.is_eof && !ps.error && !ps_match(ps, TokenType.OPERATOR, "}")) {
                note_title += ps.value
                ps_next(ps)
            }

            let link_element = document.createElement("a")
            link_element.setAttribute("onclick", "return open_note('" + note_title_to_id[note_title] + "');")
            link_element.setAttribute("href", "#")
            link_element.classList.add("note-link")
            link_element.innerHTML = note_title

            let curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.appendChild(link_element)

        } else if (ps_match(ps, TokenType.OPERATOR, null) || ps_match(ps, TokenType.TAG, null)) {
            // TODO: Implement this...
            let curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.innerHTML += ps.value
        }

        // console.log(token_type_names[ps.type] + ": " + ps.value)
    }

    // Create button to start editing the note
    {
        let margin = 5

        let new_edit_button_icon = document.createElement("img")
        new_edit_button_icon.src = "edit.svg"

        let new_edit_button = document.createElement("button")
        new_edit_button.style.position = "absolute"
        new_edit_button.style.top = margin + "px"
        new_edit_button.setAttribute("onclick", "copy_note_cmd('" + id + "');")

        // :element_size_computation
        let x_pos = expanded_note_width - margin - note_button_width
        if (new_expanded_note.scrollHeight != new_expanded_note.clientHeight) {
            // If the note has a scrollbar then move the button to the left.
            x_pos -= max_scrollbar_width
        }
        new_edit_button.style.left = x_pos + "px"

        new_edit_button.appendChild(new_edit_button_icon)

        // Insert at the start
        new_expanded_note.insertBefore(new_edit_button, new_expanded_note.firstChild)
    }


    return new_expanded_note
}

function copy_note_cmd (id)
{
    let dummy_input = document.createElement("input");
    // Is this necessary?...
    document.body.appendChild(dummy_input);

    dummy_input.value = "gvim notes/" + id
    dummy_input.select();
    dummy_input.setSelectionRange(0, 99999);

    document.execCommand("copy");

    document.body.removeChild(dummy_input);

    // TODO: Show some feedback.
}

// NOTE: This pushes a new state. Intended for use in places where we handle
// user interaction that causes navigation to a new note.
// :pushes_state
function reset_and_open_note(note_id)
{
    ajax_get ("notes/" + note_id,
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
        let params = url_parameters()
        open_notes (params["n"], note_id)

    } else {
        let expanded_note = document.querySelector(".expanded")
        expanded_note.classList.remove ("expanded")
        expanded_note.classList.add("collapsed")
        expanded_note.classList.add("right-shadow")
        expanded_note.innerHTML = ''

        let collapsed_title = collapsed_element_new (expanded_note.id)
        expanded_note.appendChild(collapsed_title)

        ajax_get ("notes/" + note_id,
                  function(response) {
                      let note_container = document.getElementById("note-container")
                      note_text_to_element(note_container, note_id, response, opened_notes.length*collapsed_note_width)
                      opened_notes.push(note_id)
                      history.pushState(null, "", "?n=" + opened_notes.join("&n="))
                  }
        )
    }

    return false
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

    ajax_get ("notes/" + expanded_note_id,
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

function navigate_to_current_url() {
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
