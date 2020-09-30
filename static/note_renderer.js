var collapsed_note_width = 40 // px
var opened_notes = []

function instantiate_template(id)
{
    return document.getElementById(id).content.firstElementChild.cloneNode(true)
}

function ajax_get (url, callback)
{
    var request = new XMLHttpRequest();
    request.onreadystatechange = function() { 
        if (this.readyState == 4) {
            callback(request.responseText);
        }
    }
    request.open("GET", url, true);
    request.send(null);
}

var TokenType = {
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
for (var e in TokenType) {
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
    for (var i=0; i<chars.length; i++) {
        if (chars.charAt(i) === c) {
            return true
        }
    }

    return false
}

function ps_next(ps) {
    var pos_is_operator = function(ps) {
        return char_in_str(ps.str.charAt(ps.pos), "#[]{}\n")
    }

    var pos_is_space = function(ps) {
        return char_in_str(ps.str.charAt(ps.pos), " \t")
    }

    var pos_is_eof = function(ps) {
        return ps.pos >= ps.str.length
    }

    if (pos_is_eof(ps)) {
        ps.is_eof = true

    } else if (ps.str.charAt(ps.pos) === "\n") {
        ps.pos++

        var pos_backup = ps.pos
        while (!pos_is_eof(ps) && pos_is_space(ps)) {
            ps.pos++
        }

        // Skip until we find something different to space or newline
        var margin = 0
        var multiple_newline = false
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
            var start = ps.pos
            while (!pos_is_eof(ps) && pos_is_space(ps)) {
                ps.pos++
            }

            // Here is where single newline characters become a single space character.
            ps.value = " " + ps.str.substr(start, ps.pos - start)
            ps.type = TokenType.TEXT
        }

    } else if (ps.str.charAt(ps.pos) === "\\") {
        ps.pos++

        var start = ps.pos
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
        var start = ps.pos
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
    var match = false;

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

var ContextType = {
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
    var curr_ctx = context_stack[context_stack.length - 1]
    var dom_element = document.createElement(element_name)
    curr_ctx.dom_element.appendChild(dom_element)
    var new_context = new ParseContext(type, margin, dom_element)
    context_stack.push(new_context)
    return new_context
}

function note_text_to_element (note_text)
{
    var new_expanded_note = document.createElement("div")
    new_expanded_note.classList.add("note")
    new_expanded_note.classList.add("expanded")
    new_expanded_note.style.left = (opened_notes.length)*collapsed_note_width + "px"

    var ps = new ParserState(note_text)

    // Parse title
    var title = ""
    ps_expect (ps, TokenType.OPERATOR, "#")
    ps_consume_spaces (ps)
    while (!ps.is_eof && !ps.error && ((ps_match(ps, TokenType.TEXT, null) || ps_match(ps, TokenType.SPACE, null)))) {
        title += ps.value
        ps_next (ps)
    }
    var title_element = document.createElement("h1")
    title_element.innerHTML = title
    new_expanded_note.appendChild(title_element)

    // Parse note's content
    var FLAG = true
    var context_type = ContextType.CONSUME_SPACES_AND_NEWLINE
    var newline_count = 0
    var paragraph = ""
    var list_element = null
    var context_stack = [new ParseContext(ContextType.PARAGRAPH, 0, new_expanded_note)]
    while (!ps.is_eof && !ps.error) {
        if (!FLAG) {
            ps_next (ps)
        } else {
            FLAG = false
        }

        if (ps_match(ps, TokenType.PARAGRAPH, null)) {
            var curr_ctx = context_stack.pop()
            while (context_stack.length > 0 &&
                   (curr_ctx.margin > ps.margin || curr_ctx.type == ContextType.PARAGRAPH)) {
                curr_ctx = context_stack.pop()
            }
            context_stack.push(curr_ctx)

            push_new_context (context_stack, ContextType.PARAGRAPH, ps.margin, "p")

        } else if (ps_match(ps, TokenType.BULLET_LIST, null)) {
            var curr_ctx = context_stack.pop()
            while (context_stack.length > 0) {
                if (curr_ctx.type == ContextType.LIST || curr_ctx.type == ContextType.ROOT) {
                    break
                } else {
                    curr_ctx = context_stack.pop()
                }
            }
            context_stack.push(curr_ctx)

            if (curr_ctx.type != ContextType.LIST) {
                var list_context = push_new_context (context_stack, ContextType.LIST, ps.margin, "ul")
                list_context.list_type = TokenType.BULLET_LIST
            }

            push_new_context (context_stack, ContextType.LIST_ITEM, ps.margin, "li")
            push_new_context (context_stack, ContextType.PARAGRAPH, ps.margin, "p")

        } else if (ps_match(ps, TokenType.TEXT, null) || ps_match(ps, TokenType.SPACE, null)) {
            var curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.innerHTML += ps.value

        } else if (ps_match(ps, TokenType.TAG, "link")) {
            ps_expect (ps, TokenType.OPERATOR, "{")

            var content = ""
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
            var pos = content.length - 1
            while (pos > 1 && content.charAt(pos) != ">") {
                pos--
            }

            var url = content
            var title = content
            if (pos != 1 && content.charAt(pos - 1) === "-") {
                pos--
                url = content.substr(pos + 2).trim()
                title = content.substr(0, pos).trim()
            }

            var link_element = document.createElement("a")
            link_element.setAttribute("href", url)
            link_element.setAttribute("target", "_blank")
            link_element.innerHTML = title

            var curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.appendChild(link_element)

        } else if (ps_match(ps, TokenType.TAG, "note")) {
            ps_expect (ps, TokenType.OPERATOR, "{")

            var note_title = ""
            ps_next(ps)
            while (!ps.is_eof && !ps.error && !ps_match(ps, TokenType.OPERATOR, "}")) {
                note_title += ps.value
                ps_next(ps)
            }

            var link_element = document.createElement("a")
            link_element.setAttribute("onclick", "return open_note('" + note_title_to_id[note_title] + "');")
            link_element.setAttribute("href", "#")
            link_element.innerHTML = note_title

            var curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.appendChild(link_element)

        } else if (ps_match(ps, TokenType.OPERATOR, null) || ps_match(ps, TokenType.TAG, null)) {
            // TODO: Implement this...
            var curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.innerHTML += ps.value
        }

        // console.log(token_type_names[ps.type] + ": " + ps.value)
    }

    return new_expanded_note
}

function reset_and_open_note(note_id)
{
    ajax_get ("notes/" + note_id,
        function(response) {
            var note_container = document.getElementById("note-container")
            note_container.innerHTML = ''
            opened_notes = []
            note_container.appendChild(note_text_to_element(response))
            opened_notes.push(note_id)
            history.pushState(null, "", "?n=" + opened_notes.join("&n="))
        }
    )

    return false
}

function open_note(note_id)
{
    var expanded_note = document.querySelector(".expanded")
    expanded_note.classList.remove ("expanded")
    expanded_note.classList.add("collapsed")
    expanded_note.innerHTML = ''
    var collapsed_title = instantiate_template("collapsed-title")
    collapsed_title.innerHTML = id_to_note_title[opened_notes[opened_notes.length - 1]]
    expanded_note.appendChild(collapsed_title)

    ajax_get ("notes/" + note_id,
        function(response) {
            var note_container = document.getElementById("note-container")
            note_container.appendChild(note_text_to_element(response))
            opened_notes.push(note_id)
            history.pushState(null, "", "?n=" + opened_notes.join("&n="))
        }
    )

    return false
}
