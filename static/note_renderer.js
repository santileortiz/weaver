let collapsed_note_width = 40 // px
let opened_notes = []

function ajax_get (url, callback)
{
    let request = new XMLHttpRequest();
    request.onreadystatechange = function() { 
        if (this.readyState == 4) {
            callback(request.responseText);
        }
    }
    request.open("GET", url, true);
    request.send(null);
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

function note_text_to_element (note_text)
{
    let new_expanded_note = document.createElement("div")
    new_expanded_note.classList.add("note")
    new_expanded_note.classList.add("expanded")
    new_expanded_note.style.left = (opened_notes.length)*collapsed_note_width + "px"

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

    return new_expanded_note
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
            note_container.appendChild(note_text_to_element(response))
            opened_notes.push(note_id)
            history.pushState(null, "", "?n=" + opened_notes.join("&n="))
        }
    )

    return false
}

// :pushes_state
function open_note(note_id)
{
    let expanded_note = document.querySelector(".expanded")
    expanded_note.classList.remove ("expanded")
    expanded_note.classList.add("collapsed")
    expanded_note.innerHTML = ''

    let collapsed_title = document.createElement("h3")
    collapsed_title.classList.add("collapsed-label")
    collapsed_title.innerHTML = id_to_note_title[opened_notes[opened_notes.length - 1]]
    expanded_note.appendChild(collapsed_title)

    ajax_get ("notes/" + note_id,
        function(response) {
            let note_container = document.getElementById("note-container")
            note_container.appendChild(note_text_to_element(response))
            opened_notes.push(note_id)
            history.pushState(null, "", "?n=" + opened_notes.join("&n="))
        }
    )

    return false
}

// Compute a dictionary of URL paramer keys to an array of values mapped to
// that key.
//
// These are relevant implementation details:
//  - Return null if there are no query parameters.
//  - Optionally pass a "flags" array where parameters that don't have a value
//    (like "...&flag&...") will be pushed once.
//  - Keys and values are URL decoded.
//  - Query parameters like "...&key=&..." will get the empty string as value.
//  - For each key, its array of values is guaranteed to be in the order they
//    were in the URL.
//  - Percent encoding of UTF-8 octets will be correctly decoded (%C3%B1 is
//    decoded to é).
//  - If a flags array is passed, flags not already present will be added in
//    the order they were in the URL. Flags already present are left in the
//    original position.
//  - Don't replace + with space.
//
// TODO: Test all these details are true in most browsers. Tested in Chrome.
//
// Why + isn't replaced to space
// -----------------------------
//
// The use of + characters as space is specific to the context where URL
// parameters are being parsed. The URI specification [1] never mentions that
// the + should be translated to the space character. It is the HTML
// specification that defines + as space in the form data serialization
// (it uses the application/x-www-form-urlencoded [2] serialization format).
// When using forms with the GET method [3], form data is sent appended to the
// URI [4], as query parameters (this is different from a form that uses POST,
// where form data is sent as the body of the HTTP request [5]).
//
// This function was written for the use case of client side parsing of URL
// parameters. In this case, we can't know if the query component of the URI
// has been serialized with the application/x-www-form-urlencoded format, so we
// don't replace + to space.
//
// [1]: https://tools.ietf.org/html/rfc3986
// [2]: https://url.spec.whatwg.org/#application/x-www-form-urlencoded
// [3]: https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#form-submission-algorithm
// [4]: https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#submit-mutate-action
// [5]: https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#submit-body
function url_parameters(flags)
{
    let params = null

    // NOTE: At least in Chrome, when using an IRI, its UTF-8 characters get
    // percent encoded to transform it into a URL. That's what we get from
    // window.location.href. Not sure if this happens in all browsers.
    let query = /\?([^#]+)/.exec(window.location.href)
    if (query != null) {
        params = {}

        let parameter_components = query[1].split("&")
        for (let i=0; i<parameter_components.length; i++) {
            // This regex allows distinguishing between a query component being:
            // 1. "key" -> "key" is a flag
            // 2. "key=" -> "key" parameter is set to "" (empty string)
            // 3. "key=value" -> "key" parameter is set to "value"
            let key_value = /([^=]+)(?:=(.*))?/.exec(parameter_components[i])
            if (key_value[2] != undefined) {
                let key = decodeURIComponent(key_value[1])
                if (params[key] == undefined) {
                    params[key] = []
                }
                params[key].push(decodeURIComponent(key_value[2]))

            } else if (flags != undefined && flags != null) {
                let flag = decodeURIComponent(key_value[1])
                if (!flags.includes(flag)) {
                    flags.push(flag)
                }
            }
        }
    }

    return params
}

let params = url_parameters()
if (params != null) {
    opened_notes = params["n"]
    reset_and_open_note(opened_notes[0])
    for (let i=1; i<opened_notes.length; i++) {
        open_note(opened_notes[i])
    }

} else {
    // By default open the first note
    reset_and_open_note(title_notes[0])
}
