let screen_height = document.documentElement.clientHeight;
let header_height = document.getElementById("header").offsetHeight;
css_property_set ("--note-container-height", screen_height - header_height + "px");

let collapsed_note_width = 40 // px
css_property_set ("--collapsed-note-width", collapsed_note_width + "px");

let expanded_note_width = 620 // px
css_property_set ("--expanded-note-width", expanded_note_width + "px");

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

let NoteTokenType = {
    UNKNOWN: 0,
    TITLE: 1,
    PARAGRAPH: 2,
    BULLET_LIST: 3,
    NUMBERED_LIST: 4,
    TAG: 5,
    OPERATOR: 6,
    SPACE: 7,
    TEXT: 8,
    CODE_HEADER: 9,
    CODE_LINE: 10,
    BLANK_LINE: 11,
    EOF: 12
}

token_type_names = []
for (let e in NoteTokenType) {
    if (NoteTokenType.hasOwnProperty(e)) {
        token_type_names.push('' + e)
    }
}

function Token () {
    this.value = '';
    this.type = NoteTokenType.UNKNOWN;
    this.margin = 0
    this.content_start = 0
    this.heading_number = 0
    this.is_eol = false;
}

function ParserState (str) {
    this.error = false
    this.error_msg = false
    this.is_eof = false
    this.str = str;

    this.pos = 0;
    this.pos_peek = 0;

    this.token = null;
    this.token_peek = null;
}

let pos_is_eof = function(ps) {
    return ps.pos >= ps.str.length
}

let pos_is_operator = function(ps) {
    return !pos_is_eof(ps) && char_in_str(ps.str.charAt(ps.pos), ",=[]{}\n\\")
}

let pos_is_digit = function(ps) {
    return !pos_is_eof(ps) && char_in_str(ps.str.charAt(ps.pos), "1234567890")
}

let pos_is_space = function(ps) {
    return !pos_is_eof(ps) && char_in_str(ps.str.charAt(ps.pos), " \t")
}

let is_empty_line = function(line) {
    let pos = 0;
    while (char_in_str(line.charAt(pos), " \t")) {
        pos++;
    }

    return line.charAt(pos) === "\n" || pos+1 === line.length-1;
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

function str_at(haystack, pos, needle)
{
    let found = true;
    let i = 0;
    while (i < needle.length) {
        if (haystack.charAt(pos+i) !== needle.charAt(i)) {
            found = false;
            break;
        }
        i++;
    }

    return found;
}

function advance_line(ps)
{
    let start = ps.pos;
    while (!pos_is_eof(ps) && ps.str.charAt(ps.pos) !== "\n") {
        ps.pos++;
    }

    // Advance over the \n character. We leave \n characters because they are
    // handled by the inline parser. Sometimes they are ignored (between URLs),
    // and sometimes they are replaced to space (multiline paragraphs).
    ps.pos++; 

    return ps.str.substr(start, ps.pos - start);
}

function ps_next_block_peek(ps)
{
    let tok = new Token();

    let backup_pos = ps.pos;
    while (pos_is_space(ps)) {
        ps.pos++
    }

    tok.value = null;
    tok.type = NoteTokenType.PARAGRAPH;
    let non_space_pos = ps.pos;
    if (pos_is_eof(ps)) {
        ps.is_eof = true;
        ps.is_eol = true;
        tok.type = NoteTokenType.EOF;

    } else if (str_at(ps.str, ps.pos, "- ") || str_at(ps.str, ps.pos, "* ")) {
        ps.pos++;
        while (pos_is_space(ps)) {
            ps.pos++;
        }

        tok.type = NoteTokenType.BULLET_LIST;
        tok.value = ps.str.charAt(non_space_pos);
        tok.margin = non_space_pos - backup_pos;
        tok.content_start = ps.pos - non_space_pos;

    } else if (pos_is_digit(ps)) {
        while (pos_is_digit(ps)) {
            ps.pos++;
        }

        let number_end = ps.pos;
        if (str_at(ps.str, ps.pos, ". ") && ps.pos - non_space_pos <= 9) {
            ps.pos++;
            while (pos_is_space(ps)) {
                ps.pos++;
            }

            tok.type = NoteTokenType.NUMBERED_LIST;
            tok.value = ps.str.substr(non_space_pos, number_end - non_space_pos);
            tok.margin = non_space_pos - backup_pos;
            tok.content_start = ps.pos - non_space_pos;

        } else {
            // It wasn't a numbered list marker, restore position.
            ps.pos = backup_pos;
        }

    } else if (ps.str.charAt(ps.pos) === "#") {
        let heading_number = 1;
        ps.pos++
        while (ps.str.charAt(ps.pos) === "#") {
            heading_number++;
            ps.pos++
        }

        if (pos_is_space (ps) && heading_number <= 6) {
            while (pos_is_space(ps)) {
                ps.pos++;
            }

            tok.value = advance_line (ps).trim();
            tok.is_eol = true;
            tok.margin = non_space_pos - backup_pos;
            tok.type = NoteTokenType.TITLE;
            tok.heading_number = heading_number;
        }

    } else if (str_at(ps.str, ps.pos, "\\code")) {
        ps.pos += "\\code".length;

        let attributes = {
            positional: [],
            named: {}
        }
        if (ps.str.charAt(ps.pos) === "[") {
            while (!ps.is_eof && ps.str.charAt(ps.pos) !== "]") {
                ps.pos++;

                let start = ps.pos;
                if (ps.str.charAt(ps.pos) !== "\"") {
                    // TODO: Allow quote strings for values. This is to allow
                    // values containing "," or "]".
                }

                while (ps.str.charAt(ps.pos) !== "," &&
                       ps.str.charAt(ps.pos) !== "=" &&
                       ps.str.charAt(ps.pos) !== "]") {
                    ps.pos++;
                }

                if (ps.str.charAt(ps.pos) === "," || ps.str.charAt(ps.pos) === "]") {
                    let value = ps.str.substr(start, ps.pos - start).trim();
                    attributes.positional.push (value)

                } else {
                    let name = ps.str.substr(ps.pos - start).trim();

                    ps.pos++;
                    let value_start = ps.pos;
                    while (ps.str.charAt(ps.pos) !== "," &&
                           ps.str.charAt(ps.pos) !== "]") {
                        ps.pos++;
                    }

                    let value = ps.str.substr(start, ps.pos - value_start).trim();

                    attributes.named[name] = value
                }

                if (ps.str.charAt(ps.pos) !== "]") {
                    ps.pos++;
                }
            }

            ps.pos++;
        }

        if (ps.str.charAt(ps.pos) !== "{") {
            tok.type = NoteTokenType.CODE_HEADER;
            while (pos_is_space(ps) || ps.str.charAt(ps.pos) === "\n") {
                ps.pos++;
            }

        } else {
            // False alarm, this was an inline code block, restore position.
            ps.pos = backup_pos;
        }

    } else if (ps.str.charAt(ps.pos) === "|") {
        ps.pos++;
        tok.value = advance_line (ps);
        tok.is_eol = true;
        tok.type = NoteTokenType.CODE_LINE;

    } else if (ps.str.charAt(ps.pos) === "\n") {
        ps.pos++;
        tok.type = NoteTokenType.BLANK_LINE;
    }

    if (tok.type === NoteTokenType.PARAGRAPH) {
        tok.value = advance_line (ps);
        tok.is_eol = true;
    }


    // Because we are only peeking, restore the old position and store the
    // resulting one in a separate variable.
    ps.token_peek = tok;
    ps.pos_peek = ps.pos;
    ps.pos = backup_pos;

    return ps.token_peek;
}

function ps_next_block(ps) {
    if (ps.token_peek === null) {
        ps_next_block_peek(ps);
    }

    ps.pos = ps.pos_peek;
    ps.token = ps.token_peek;
    ps.token_peek = null;

    console.log (ps.token.type + ": " + "'" + ps.token.value + "'");

    return ps.token;
}

function ps_next(ps) {
    let pos_is_operator = function(ps) {
        return char_in_str(ps.str.charAt(ps.pos), ",=[]{}\n\\")
    }

    let pos_is_space = function(ps) {
        return char_in_str(ps.str.charAt(ps.pos), " \t")
    }

    let pos_is_eof = function(ps) {
        return ps.pos >= ps.str.length
    }

    if (pos_is_eof(ps)) {
        ps.is_eof = true

    } else if (ps.pos === 0 || ps.str.charAt(ps.pos) === "\n") {
        if (ps.pos != 0) {
            ps.pos++
        }

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

            ps.type = NoteTokenType.BULLET_LIST
            ps.value = null
            ps.margin = ps.pos - pos_backup

        } else if (ps.str.charAt(ps.pos) === "#") {
            ps.pos++
            let margin = 1
            while (ps.str.charAt(ps.pos) === "#") {
                margin++
                ps.pos++
            }

            let start = ps.pos
            while (ps.str.charAt(ps.pos) !== "\n") {
                ps.pos++
            }

            ps.value = ps.str.substr(start, ps.pos - start).trim()
            ps.margin = Math.min (6, margin)
            ps.type = NoteTokenType.TITLE

        } else if (ps.str.charAt(ps.pos) === "|") {
            ps.pos++

            let start = ps.pos
            while (ps.str.charAt(ps.pos) !== "\n") {
                ps.pos++
            }

            ps.value = ps.str.substr(start, ps.pos - start)
            ps.type = NoteTokenType.CODE_LINE

        } else if (multiple_newline) {
            ps.type = NoteTokenType.PARAGRAPH
            ps.value = null
            ps.margin = margin

        } else {
            let start = ps.pos
            while (!pos_is_eof(ps) && pos_is_space(ps)) {
                ps.pos++
            }

            // Here is where single newline characters become a single space character.
            ps.value = " " + ps.str.substr(start, ps.pos - start)
            ps.type = NoteTokenType.TEXT
        }

    } else if (ps.str.charAt(ps.pos) === "\\") {
        // :tag_parsing
        ps.pos++

        let start = ps.pos
        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps)) {
            ps.pos++
        }
        ps.value = ps.str.substr(start, ps.pos - start)
        ps.type = NoteTokenType.TAG

    } else if (pos_is_operator(ps)) {
        ps.type = NoteTokenType.OPERATOR
        ps.value = ps.str.charAt(ps.pos)
        ps.pos++

    } else if (pos_is_space(ps)) {
        ps.type = NoteTokenType.SPACE
        ps.value = ps.str.charAt(ps.pos)
        ps.pos++

    } else {
        let start = ps.pos
        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps)) {
            ps.pos++
        }

        ps.value = ps.str.substr(start, ps.pos - start)
        ps.type = NoteTokenType.TEXT
    }

    if (pos_is_eof(ps)) {
        ps.is_eof = true
        ps.type = NoteTokenType.EOF
        ps.value = null
    }

    //console.log(token_type_names[ps.type] + ": " + ps.value)
}

function ps_match(ps, type, value)
{
    let match = false;

    if (type === ps.token.type) {
        if (value == null) {
            match = true;

        } else if (ps.token.value === value) {
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

let ContextType = {
    ROOT: 0,
    PARAGRAPH: 1,
    LIST: 2,
    LIST_ITEM: 3,
    CODE: 4
}

function ParseContext (type, margin, dom_element) {
    this.type = type
    this.margin = margin
    this.dom_element = dom_element

    this.list_type = null
}

function array_end(array)
{
    return array[array.length - 1]
}

function html_escape (str)
{
    return str.replaceAll("<", "&lt;").replaceAll(">", "&gt;")
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

// Code is handled differently because we can't know the mapping into DOM
// elements before the context of type CODE is popped from the stack. This
// happens because we can't know if a \code[] tag is followed by a {} block or
// a | block until after we process the \code tag. This means that when
// processing the \code tag we can't know which DOM elements to add.
//
// To work around this we store the content ina variable inside the context,
// and create the DOM elements in pop_context().
function push_new_code_context (context_stack, attributes)
{
    let new_context = new ParseContext(ContextType.CODE, 0, null)
    new_context.code = ""
    new_context.is_block = false
    new_context.empty = true
    new_context.min_leading_spaces = Infinity

    if (attributes != null) {
        if (attributes.positional[0] === "plain") {
            new_context.language = attributes.positional[0]
        }

        // TODO: Also support receiving a filename as argument and infer the
        // language from it.
    }

    context_stack.push(new_context)
    return new_context
}

function pop_context (context_stack)
{
    let curr_ctx = context_stack.pop()
    let next_ctx = array_end(context_stack)
    if (curr_ctx.type === ContextType.CODE) {
        let code_dom = document.createElement("code")
        next_ctx.dom_element.appendChild(code_dom)

        if (curr_ctx.is_block) {
            // NOTE: Currently code blocks are wrapped inside <p></p>, not sure
            // if this is a good approach but I think if we were to remove it,
            // we would have to do so here, after the PARAGRAPH context has
            // been pushed. We sould need to detect that the inly child of the
            // <p> tag is a code block, then remove the extra parent.
            code_dom.classList.add("code-block")
            code_dom.style.paddingLeft = "1em"

            // If we use line numbers, this should be the padding WRT the
            // column containing the numbers.
            // code_dom.style.paddingLeft = "0.25em"

            if (curr_ctx.min_leading_spaces != Infinity && curr_ctx.min_leading_spaces > 0) {
                // Remove the most leading spaces we can remove. I call this
                // automatic space normalization.
                curr_ctx.code = curr_ctx.code.substr(curr_ctx.min_leading_spaces)
                curr_ctx.code = curr_ctx.code.replaceAll("\n" + " ".repeat(curr_ctx.min_leading_spaces), "\n")
            }

            let preformatted_dom = document.createElement("pre")
            preformatted_dom.innerHTML = curr_ctx.code
            code_dom.appendChild(preformatted_dom)

        } else {
            code_dom.classList.add("code-inline")
            code_dom.innerHTML = curr_ctx.code
        }

    }

    return next_ctx
}

// This function ends th current paragraph and starts a new one.
// I think the margin conditional needs some more testing...
function start_paragraph (ps, context_stack)
{
    let curr_ctx = array_end (context_stack)
    while (context_stack.length > 0 &&
           (curr_ctx.margin > ps.margin ||
               curr_ctx.type === ContextType.PARAGRAPH ||
               curr_ctx.type === ContextType.CODE)) {
        curr_ctx = pop_context (context_stack)
    }

    push_new_context (context_stack, ContextType.PARAGRAPH, ps.margin, "p")
}

// This function returns the same string that was parsed as the current token.
// It's used as fallback, for example in the case of , an # characters in the
// middle of paragraphs, or unrecognized tag sequences.
//
// TODO: I fell that using this for operators isn't nice, we should probably
// have a stateful tokenizer that will consider ',' as part of a TEXT token
// sometimes and as operators other times.
function ps_literal_token (ps)
{
    let res = ps.token.value != null ? ps.token.value : ""
    if (ps_match (ps, NoteTokenType.TAG, null)) {
        res = "\\" + ps.token.value
    }

    return res
}

function ps_next_content(ps)
{
    let pos_is_operator = function(ps) {
        return char_in_str(ps.str.charAt(ps.pos), ",=[]{}\\");
    }

    let pos_is_space = function(ps) {
        return char_in_str(ps.str.charAt(ps.pos), " \t");
    }

    let pos_is_eof = function(ps) {
        return ps.pos >= ps.str.length;
    }

    let tok = new Token();

    if (pos_is_eof(ps)) {
        ps.is_eof = true;

    } else if (ps.str.charAt(ps.pos) === "\\") {
        // :tag_parsing
        ps.pos++;

        let start = ps.pos;
        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps)) {
            ps.pos++;
        }
        tok.value = ps.str.substr(start, ps.pos - start);
        tok.type = NoteTokenType.TAG;

    } else if (pos_is_operator(ps)) {
        tok.type = NoteTokenType.OPERATOR;
        tok.value = ps.str.charAt(ps.pos);
        ps.pos++;

    } else if (pos_is_space(ps) || ps.str.charAt(ps.pos) === "\n") {
        // This consumes consecutive spaces into a single one.
        while (pos_is_space(ps) || ps.str.charAt(ps.pos) === "\n") {
            ps.pos++;
        }

        tok.value = " ";
        tok.type = NoteTokenType.SPACE;

    } else {
        let start = ps.pos;
        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps) && ps.str.charAt(ps.pos) !== "\n") {
            ps.pos++;
        }

        tok.value = ps.str.substr(start, ps.pos - start);
        tok.type = NoteTokenType.TEXT;
    }

    if (pos_is_eof(ps)) {
        ps.is_eof = true;
    }

    ps.token = tok;

    //console.log(token_type_names[tok.type] + ": " + tok.value)
    return tok;
}

function ps_expect_content(ps, type, value)
{
    ps_next_content (ps)
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

// This function parses the content of a block of text. The formatting is
// limited to tags that affect the formating inline. This parsing functiuon
// will not add nested blocks like paragraphs, lists, code blocks etc.
//
// TODO: How do we handle the prescence of nested blocks here?, ignore them and
// print them or raise an error and stop parsing.
function block_content_parse_text (container, content)
{
    let ps = new ParserState(content)

    let context_stack = [new ParseContext(ContextType.ROOT, 0, container)]
    while (!ps.is_eof && !ps.error) {
        let tok = ps_next_content (ps)

        if (ps_match(ps, NoteTokenType.TEXT, null) || ps_match(ps, NoteTokenType.SPACE, null)) {
            let curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.innerHTML += tok.value

        } else if (ps_match(ps, NoteTokenType.TAG, "link")) {
            ps_expect_content (ps, NoteTokenType.OPERATOR, "{")

            let content = ""
            tok = ps_next_content(ps)
            while (!ps.is_eof && !ps.error && !ps_match(ps, NoteTokenType.OPERATOR, "}")) {
                content += tok.value
                tok = ps_next_content(ps)
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

        } else if (ps_match(ps, NoteTokenType.TAG, "code")) {
            let attributes = {
                positional: [],
                named: {}
            }
            if (ps.str.charAt(ps.pos) === "[") {
                ps_expect_content(ps, NoteTokenType.OPERATOR, "[")

                while (!ps_match (ps, NoteTokenType.OPERATOR, "]")) {
                    ps_expect_content (ps, NoteTokenType.TEXT, null)
                    let text = ps.value

                    ps_next(ps)
                    if (ps_match (ps, NoteTokenType.OPERATOR, ",")) {
                        attributes.positional.push (text)

                    } else if (ps_match (ps, NoteTokenType.OPERATOR, "=")) {
                        ps_match (ps, NoteTokenType.TEXT, null)
                        let value = ps.value
                        attributes.named[text] = value
                    }
                }
            }

            let code_ctx = push_new_code_context (context_stack, attributes)

            if (ps.str.charAt(ps.pos) === "{") {
                let content = ""
                let brace_level = 1
                ps_expect_content (ps, NoteTokenType.OPERATOR, "{")

                while (!ps.is_eof && brace_level != 0) {
                    ps_next_content (ps)
                    if (ps_match (ps, NoteTokenType.OPERATOR, "{")) {
                        brace_level++
                    } else if (ps_match (ps, NoteTokenType.OPERATOR, "}")) {
                        brace_level--
                    }

                    // Avoid appending the closing }
                    if (brace_level != 0) {
                        code_ctx.code += html_escape(ps_literal_token(ps))
                    }
                }

                pop_context(context_stack)
            }

        } else if (ps_match(ps, NoteTokenType.TAG, "note")) {
            ps_expect_content (ps, NoteTokenType.OPERATOR, "{")

            let note_title = ""
            tok = ps_next_content(ps)
            while (!ps.is_eof && !ps.error && !ps_match(ps, NoteTokenType.OPERATOR, "}")) {
                note_title += tok.value
                tok = ps_next_content(ps)
            }

            let link_element = document.createElement("a")
            link_element.setAttribute("onclick", "return open_note('" + note_title_to_id[note_title] + "');")
            link_element.setAttribute("href", "#")
            link_element.classList.add("note-link")
            link_element.innerHTML = note_title

            let curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.appendChild(link_element)

        } else {
            let curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.innerHTML += ps_literal_token(ps)
        }
    }
}

let BlockType = {
    ROOT: 0,
    LIST: 1,
    LIST_ITEM: 2,

    HEADING: 3,
    PARAGRAPH: 4,
    CODE: 5
}

function Block (type, margin, block_content, inline_content, list_marker)
{
    this.type = type;
    this.margin = margin;

    this.block_content = block_content;
    this.inline_content = inline_content;

    this.heading_number = 1;

    ///////
    // List
    this.list_type = null;

    // TODO: I think this useless now??... No one seems to implement custom
    // numbering of lists.
    //
    // 1. Numbered
    // 3. Third item
    this.list_marker = list_marker; 

    // Number of characters from the start of a list marker to the start of the
    // content. Includes al characters of the list marker.
    // "    -    C" -> 5
    // "   10.   C" -> 6
    this.content_start = 0;
}

function leaf_block_new(type, margin, inline_content)
{
    return new Block(type, margin, null, inline_content, null);
}

function container_block_new(type, margin, list_marker=null, block_content=[])
{
    return new Block(type, margin, block_content, null, list_marker);
}

function push_block (block_stack, new_block)
{
    let curr_block = array_end(block_stack);

    console.assert (curr_block.block_content != null, "Pushing nested block to non-container block.");
    curr_block.block_content.push(new_block);

    block_stack.push(new_block);
    return new_block;
}

function block_tree_to_dom (block, dom_element)
{
    if (block.type === BlockType.PARAGRAPH) {
        let new_dom_element = document.createElement("p");
        dom_element.appendChild(new_dom_element);
        block_content_parse_text (new_dom_element, block.inline_content);
        new_dom_element.innerHTML = new_dom_element.innerHTML.trim();

    } else if (block.type === BlockType.HEADING) {
        let new_dom_element = document.createElement("h" + block.heading_number);
        dom_element.appendChild(new_dom_element);
        block_content_parse_text (new_dom_element, block.inline_content);
        new_dom_element.innerHTML.trim();

    } else if (block.type === BlockType.CODE) {
        let new_dom_element = document.createElement("code");
        new_dom_element.classList.add("code-block")
        new_dom_element.style.paddingLeft = "1em"

        // If we use line numbers, this should be the padding WRT the
        // column containing the numbers.
        // code_dom.style.paddingLeft = "0.25em"

        if (block.min_leading_spaces != Infinity && block.min_leading_spaces > 0) {
            // Remove the most leading spaces we can remove. I call this
            // automatic space normalization.
            curr_ctx.code = curr_ctx.code.substr(curr_ctx.min_leading_spaces)
            curr_ctx.code = curr_ctx.code.replaceAll("\n" + " ".repeat(curr_ctx.min_leading_spaces), "\n")
        }

        dom_element.appendChild(new_dom_element);

        let preformatted_dom = document.createElement("pre")
        preformatted_dom.innerHTML = block.inline_content;
        new_dom_element.appendChild(preformatted_dom)

    } else if (block.type === BlockType.ROOT) {
        block.block_content.forEach (function(sub_block) {
            block_tree_to_dom(sub_block, dom_element);
        });

    } else if (block.type === BlockType.LIST) {
        let new_dom_element = document.createElement("ul");
        if (block.list_type === NoteTokenType.NUMBERED_LIST) {
            new_dom_element = document.createElement("ol");
        }
        dom_element.appendChild(new_dom_element);

        block.block_content.forEach (function(sub_block) {
            block_tree_to_dom(sub_block, new_dom_element);
        });

    } else if (block.type === BlockType.LIST_ITEM) {
        let new_dom_element = document.createElement("li");
        dom_element.appendChild(new_dom_element);

        block.block_content.forEach (function(sub_block) {
            block_tree_to_dom(sub_block, new_dom_element);
        });
    }
}

function note_text_to_element_block (container, id, note_text, x)
{
    let ps = new ParserState(note_text)

    let root_block = new container_block_new(BlockType.ROOT, 0);

    // Expect a title as the start of the note, fail if no title is found.
    let tok = ps_next_block_peek (ps)
    if (tok.type !== NoteTokenType.TITLE) {
        ps.error = true;
        ps.error_msg = sprintf("Notes must start with a Heading 1 title.");
    }


    // Parse note's content
    let block_stack = [root_block];
    while (!ps.is_eof && !ps.error) {
        let curr_block_idx = 0;
        let tok = ps_next_block(ps);

        // Match the indentation of the received token.
        let next_stack_block = block_stack[curr_block_idx+1]
        while (curr_block_idx+1 < block_stack.length &&
               next_stack_block.type === BlockType.LIST &&
               (tok.margin > next_stack_block.margin ||
                   tok.type === next_stack_block.list_type && tok.margin === next_stack_block.margin)) {
            curr_block_idx++;
            next_stack_block = block_stack[curr_block_idx+1];
        }

        // Pop all blocks after the current index
        block_stack.length = curr_block_idx+1;

        if (ps_match(ps, NoteTokenType.TITLE, null)) {
            let prnt = block_stack[curr_block_idx];
            let heading_block = push_block (block_stack, leaf_block_new(BlockType.HEADING, tok.margin, tok.value));
            heading_block.heading_number = tok.heading_number;

        } else if (ps_match(ps, NoteTokenType.PARAGRAPH, null)) {
            let prnt = block_stack[curr_block_idx];
            if (prnt.type == BlockType.PARAGRAPH) {
                prnt.inline_content += " " + tok.value;
            } else {
                let new_paragraph = push_block (block_stack, leaf_block_new(BlockType.PARAGRAPH, tok.margin, tok.value));
                
                // Append all paragraph continuation lines. This ensures all paragraphs
                // found at the beginning of the iteration followed an empty line.
                let tok_peek = ps_next_block_peek(ps);
                while (tok_peek.is_eol && tok_peek.type == NoteTokenType.PARAGRAPH) {
                    new_paragraph.inline_content += tok_peek.value;
                    ps_next_block(ps);

                    tok_peek = ps_next_block_peek(ps);
                }
            }

        } else if (ps_match(ps, NoteTokenType.CODE_HEADER, null)) {
            let new_code_block = push_block (block_stack, leaf_block_new(BlockType.CODE, tok.margin, ""));

            let min_leading_spaces = Infinity;
            let is_start = true; // Used to strip trailing empty lines.
            let tok_peek = ps_next_block_peek(ps);
            while (tok_peek.is_eol && tok_peek.type == NoteTokenType.CODE_LINE) {
                ps_next_block(ps);

                if (!is_start || !is_empty_line(tok_peek.value)) {
                    is_start = false;

                    min_leading_spaces = Math.min (min_leading_spaces, tok_peek.value.search(/\S/))
                    new_code_block.inline_content += html_escape(tok_peek.value);
                }

                tok_peek = ps_next_block_peek(ps);
            }

            // Remove the most leading spaces we can remove. I call this
            // automatic space normalization.
            if (min_leading_spaces != Infinity && min_leading_spaces > 0) {
                new_code_block.inline_content = new_code_block.inline_content.substr(min_leading_spaces)
                new_code_block.inline_content = new_code_block.inline_content.replaceAll("\n" + " ".repeat(min_leading_spaces), "\n")
            }


        } else if (ps_match(ps, NoteTokenType.BULLET_LIST, null) || ps_match(ps, NoteTokenType.NUMBERED_LIST, null)) {
            let prnt = block_stack[curr_block_idx];
            if (prnt.type != BlockType.LIST ||
                tok.margin >= prnt.margin + prnt.content_start) {
                let list_block = push_block (block_stack, container_block_new(BlockType.LIST, tok.margin, tok.value));
                list_block.content_start = tok.content_start;
                list_block.list_type = tok.type;
            }

            push_block(block_stack, container_block_new(BlockType.LIST_ITEM, tok.margin, tok.value));

            let tok_peek = ps_next_block_peek(ps);
            if (tok_peek.is_eol && tok_peek.type == NoteTokenType.PARAGRAPH) {
                ps_next_block(ps);

                // Use list's margin... maybe this will never be read?...
                let new_paragraph = push_block(block_stack, leaf_block_new(BlockType.PARAGRAPH, tok.margin, tok_peek.value));

                // Append all paragraph continuation lines. This ensures all paragraphs
                // found at the beginning of the iteration followed an empty line.
                tok_peek = ps_next_block_peek(ps);
                while (tok_peek.is_eol && tok_peek.type == NoteTokenType.PARAGRAPH) {
                    new_paragraph.inline_content += tok_peek.value;
                    ps_next_block(ps);

                    tok_peek = ps_next_block_peek(ps);
                }
            }
        }

    }

    let new_expanded_note = note_element_new (id, x)
    new_expanded_note.classList.add("expanded")

    // NOTE: We append this at the beginning so we get the correct client and
    // scroll height at the end.
    // :element_size_computation
    container.appendChild(new_expanded_note)

    block_tree_to_dom(root_block, new_expanded_note);

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
            //// If the note has a scrollbar then move the button to the left.
            x_pos -= max_scrollbar_width
        }
        new_edit_button.style.left = x_pos + "px"

        new_edit_button.appendChild(new_edit_button_icon)

        // Insert at the start
        new_expanded_note.insertBefore(new_edit_button, new_expanded_note.firstChild)
    }

    return new_expanded_note;
}

function note_text_to_element (container, id, note_text, x)
{
    let new_expanded_note = note_element_new (id, x)
    new_expanded_note.classList.add("expanded")

    // NOTE: We append this at the beginning so we get the correct client and
    // scroll height at the end.
    // :element_size_computation
    container.appendChild(new_expanded_note)

    let root_block = new Block(BlockType.ROOT, 0)

    let ps = new ParserState(note_text)

    // Expect a title as the start of the note, fail if no title is found.
    ps_expect (ps, NoteTokenType.TITLE, null)

    // Parse note's content
    let FLAG = true
    let context_type = ContextType.CONSUME_SPACES_AND_NEWLINE
    let newline_count = 0
    let paragraph = ""
    let list_element = null
    let block_stack = [root_block]
    let context_stack = [new ParseContext(ContextType.ROOT, 0, new_expanded_note)]
    while (!ps.is_eof && !ps.error) {
        if (!FLAG) {
            ps_next (ps)
        } else {
            FLAG = false
        }

        if (ps_match(ps, NoteTokenType.TITLE, null)) {
            let curr_ctx = array_end (context_stack)
            while (context_stack.length > 0 && curr_ctx.type !== ContextType.ROOT) {
                curr_ctx = pop_context (context_stack)
            }

            let title_element = document.createElement("h" + ps.margin)
            new_expanded_note.appendChild(title_element)
            block_content_parse_text (title_element, ps.value)

        } else if (ps_match(ps, NoteTokenType.PARAGRAPH, null)) {
            start_paragraph (ps, context_stack)

        } else if (ps_match(ps, NoteTokenType.BULLET_LIST, null)) {
            let curr_ctx = array_end (context_stack)
            while (context_stack.length > 0 &&
                   (curr_ctx.type !== ContextType.LIST && curr_ctx.type !== ContextType.ROOT)) {
                curr_ctx = pop_context (context_stack)
            }

            if (curr_ctx.type != ContextType.LIST) {
                let list_context = push_new_context (context_stack, ContextType.LIST, ps.margin, "ul")
                list_context.list_type = NoteTokenType.BULLET_LIST
            }

            push_new_context (context_stack, ContextType.LIST_ITEM, ps.margin, "li")
            push_new_context (context_stack, ContextType.PARAGRAPH, ps.margin, "p")

        } else if (ps_match(ps, NoteTokenType.TEXT, null) || ps_match(ps, NoteTokenType.SPACE, null)) {
            let curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.innerHTML += ps.value

        } else if (ps_match(ps, NoteTokenType.TAG, "link")) {
            ps_expect (ps, NoteTokenType.OPERATOR, "{")

            let content = ""
            ps_next(ps)
            while (!ps.is_eof && !ps.error && !ps_match(ps, NoteTokenType.OPERATOR, "}")) {
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

        } else if (ps_match(ps, NoteTokenType.TAG, "note")) {
            ps_expect (ps, NoteTokenType.OPERATOR, "{")

            let note_title = ""
            ps_next(ps)
            while (!ps.is_eof && !ps.error && !ps_match(ps, NoteTokenType.OPERATOR, "}")) {
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

        } else if (ps_match(ps, NoteTokenType.TAG, "code")) {
            let attributes = {
                positional: [],
                named: {}
            }
            if (ps.str.charAt(ps.pos) === "[") {
                ps_expect(ps, NoteTokenType.OPERATOR, "[")

                while (!ps_match (ps, NoteTokenType.OPERATOR, "]")) {
                    ps_expect (ps, NoteTokenType.TEXT, null)
                    let text = ps.value

                    ps_next(ps)
                    if (ps_match (ps, NoteTokenType.OPERATOR, ",")) {
                        attributes.positional.push (text)

                    } else if (ps_match (ps, NoteTokenType.OPERATOR, "=")) {
                        ps_match (ps, NoteTokenType.TEXT, null)
                        let value = ps.value
                        attributes.named[text] = value
                    }
                }
            }

            let code_ctx = push_new_code_context (context_stack, attributes)

            if (ps.str.charAt(ps.pos) === "{") {
                let content = ""
                let brace_level = 1
                ps_expect (ps, NoteTokenType.OPERATOR, "{")

                while (!ps.is_eof && brace_level != 0) {
                    ps_next (ps)
                    if (ps_match (ps, NoteTokenType.OPERATOR, "{")) {
                        brace_level++
                    } else if (ps_match (ps, NoteTokenType.OPERATOR, "}")) {
                        brace_level--
                    }

                    // Avoid appending the closing }
                    if (brace_level != 0) {
                        code_ctx.code += html_escape(ps_literal_token(ps))
                    }
                }

                pop_context(context_stack)
            }

        } else if (ps_match(ps, NoteTokenType.CODE_LINE, null)) {
            let curr_ctx = context_stack[context_stack.length - 1]
            if (curr_ctx.type !== ContextType.CODE) {
                start_paragraph (ps, context_stack)
                curr_ctx = push_new_code_context (context_stack)
            }

            curr_ctx.is_block = true

            // NOTE: This conditional strips all trailing empty lines in the
            // code block.
            if (ps.value.trim() !== "") {
                curr_ctx.empty = false
            }

            if (!curr_ctx.empty) {
                curr_ctx.min_leading_spaces = Math.min (curr_ctx.min_leading_spaces, ps.value.search(/\S/))
                // Here we don't need ps_literal_token() because the tokenizer
                // considered the whole line as a single token.
                curr_ctx.code += html_escape(ps.value) + "\n"
            }

        } else {
            let curr_ctx = context_stack[context_stack.length - 1]
            curr_ctx.dom_element.innerHTML += ps_literal_token(ps)
        }
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
            note_text_to_element_block(note_container, note_id, response, opened_notes.length*collapsed_note_width)
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
                      note_text_to_element_block(note_container, note_id, response, opened_notes.length*collapsed_note_width)
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

            note_text_to_element_block(note_container, expanded_note_id, response, i*collapsed_note_width)
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
