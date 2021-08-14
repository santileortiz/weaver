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
let __g_user_tags = {};
let __g_related_notes = [];

function UserTag (tag_name, callback, user_data) {
    this.name = tag_name;
    this.callback = callback;
    this.user_data = user_data;
}

function new_user_tag (tag_name, callback)
{
    __g_user_tags[tag_name] = new UserTag(tag_name, callback);
}

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

function ps_parse_tag_attributes (ps)
{
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
                let name = ps.str.substr(start, ps.pos - start).trim();

                ps.pos++;
                let value_start = ps.pos;
                while (ps.str.charAt(ps.pos) !== "," &&
                       ps.str.charAt(ps.pos) !== "]") {
                    ps.pos++;
                }

                let value = ps.str.substr(value_start, ps.pos - value_start).trim();

                attributes.named[name] = value
            }

            if (ps.str.charAt(ps.pos) !== "]") {
                ps.pos++;
            }
        }

        ps.pos++;
    }

    return attributes;
}

function ps_parse_tag (ps)
{
    let tag = {
        attributes: null,
        content: null,
    };

    let attributes = ps_parse_tag_attributes(ps);
    let content = "";

    ps_expect_content (ps, NoteTokenType.OPERATOR, "{")
    tok = ps_next_content(ps)
    while (!ps.is_eof && !ps.error && !ps_match(ps, NoteTokenType.OPERATOR, "}")) {
        content += tok.value
        tok = ps_next_content(ps)
    }

    if (!ps.error) {
        tag.attributes = attributes;
        tag.content = content;
    }

    return tag;
}

function ps_parse_tag_balanced_braces (ps)
{
    let tag = {
        attributes: null,
        content: null,
    };

    let attributes = ps_parse_tag_attributes(ps);
    let content = parse_balanced_brace_block(ps)

    if (!ps.error) {
        tag.attributes = attributes;
        tag.content = content;
    }

    return tag;
}

function ps_next_peek(ps)
{
    // TODO: Forbid calling peek twice in a row. ps_next() must be called in
    // between because we don't support arbitrarily deep peeking, if we did, we
    // would have to implement this as a stack with push and pop.

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
        let attributes = ps_parse_tag_attributes(ps);

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

function ps_next(ps) {
    if (ps.token_peek === null) {
        ps_next_peek(ps);
    }

    ps.pos = ps.pos_peek;
    ps.token = ps.token_peek;
    ps.token_peek = null;

    //console.log (ps.token.type + ": " + "'" + ps.token.value + "'");

    return ps.token;
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

let ContextType = {
    ROOT: 0,
    HTML: 1,
}

function ParseContext (type, dom_element) {
    this.type = type
    this.dom_element = dom_element

    this.list_type = null
}

function array_end(array)
{
    return array[array.length - 1]
}

function append_dom_element (context_stack, dom_element)
{
    let head_ctx = array_end(context_stack)
    head_ctx.dom_element.appendChild(dom_element)
}

function html_escape (str)
{
    return str.replaceAll("<", "&lt;").replaceAll(">", "&gt;")
}

function push_new_html_context (context_stack, tag)
{
    let dom_element = document.createElement(tag);
    append_dom_element (context_stack, dom_element);

    let new_context = new ParseContext(ContextType.HTML, dom_element);
    context_stack.push(new_context);
    return new_context;
}

function pop_context (context_stack)
{
    let curr_ctx = context_stack.pop()
    return array_end(context_stack)
}

// This function returns the same string that was parsed as the current token.
// It's used as fallback, for example in the case of ',' and '#' characters in
// the middle of paragraphs, or unrecognized tag sequences.
//
// TODO: I feel that using this for operators isn't nice, we should probably
// have a stateful tokenizer that will consider ',' as part of a TEXT token
// sometimes and as operators other times. It's probably best to just have an
// attribute tokenizer/parser, then the inline parser only deals with tags.
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

function parse_balanced_brace_block(ps)
{
    let content = null

    if (ps.str.charAt(ps.pos) === "{") {
        content = "";
        let brace_level = 1;
        ps_expect_content (ps, NoteTokenType.OPERATOR, "{");

        while (!ps.is_eof && brace_level != 0) {
            ps_next_content (ps);
            if (ps_match (ps, NoteTokenType.OPERATOR, "{")) {
                brace_level++;
            } else if (ps_match (ps, NoteTokenType.OPERATOR, "}")) {
                brace_level--;
            }

            if (brace_level != 0) { // Avoid appending the closing }
                content += html_escape(ps_literal_token(ps));
            }
        }
    }

    return content;
}

function compute_media_size (attributes, aspect_ratio, max_width)
{
    let a_width = undefined;
    if (attributes.named.width != undefined) {
        a_width = Number(attributes.named.width)
    }

    let a_height = undefined;
    if (attributes.named.height != undefined) {
        a_height = Number(attributes.named.height)
    }


    let width, height;
    if (a_height == undefined &&
        a_width != undefined && a_width <= max_width) {
        width = a_width;
        height = width/aspect_ratio;

    } else if (a_width == undefined &&
               a_height != undefined && a_height <= max_width/aspect_ratio) {
        height = a_height;
        width = height*aspect_ratio;

    } else if (a_width != undefined && a_width <= max_width &&
               a_height != undefined && a_height <= max_width/aspect_ratio) {
        width = a_width;
        height = a_height;

    } else {
        width = max_width;
        height = width/aspect_ratio;
    }

    return [width, height];
}

// This function parses the content of a block of text. The formatting is
// limited to tags that affect the formating inline. This parsing function
// will not add nested blocks like paragraphs, lists, code blocks etc.
//
// TODO: How do we handle the prescence of nested blocks here?, ignore them and
// print them or raise an error and stop parsing.
function block_content_parse_text (container, content)
{
    let ps = new ParserState(content);

    let context_stack = [new ParseContext(ContextType.ROOT, container)]
    while (!ps.is_eof && !ps.error) {
        let tok = ps_next_content (ps);

        if (ps_match(ps, NoteTokenType.TEXT, null) || ps_match(ps, NoteTokenType.SPACE, null)) {
            let curr_ctx = array_end(context_stack);
            curr_ctx.dom_element.innerHTML += tok.value;

        } else if (ps_match(ps, NoteTokenType.OPERATOR, "}") && array_end(context_stack).type !== ContextType.ROOT) {
            pop_context (context_stack);

        } else if (ps_match(ps, NoteTokenType.TAG, "i") || ps_match(ps, NoteTokenType.TAG, "b")) {
            let tag = ps.token.value;
            ps_expect_content (ps, NoteTokenType.OPERATOR, "{");
            push_new_html_context (context_stack, tag)

        } else if (ps_match(ps, NoteTokenType.TAG, "link")) {
            let tag = ps_parse_tag (ps);

            // We parse the URL from the title starting at the end. I thinkg is
            // far less likely to have a non URL encoded > character in the
            // URL, than a user wanting to use > inside their title.
            //
            // TODO: Support another syntax for the rare case of a user that
            // wants a > character in a URL. Or make sure URLs are URL encoded
            // from the UI that will be used to edit this.
            let pos = tag.content.length - 1
            while (pos > 1 && tag.content.charAt(pos) != ">") {
                pos--
            }

            let url = tag.content
            let title = tag.content
            if (pos != 1 && tag.content.charAt(pos - 1) === "-") {
                pos--
                url = tag.content.substr(pos + 2).trim()
                title = tag.content.substr(0, pos).trim()
            }

            let link_element = document.createElement("a")
            link_element.setAttribute("href", url)
            link_element.setAttribute("target", "_blank")
            link_element.innerHTML = title

            append_dom_element(context_stack, link_element);

        } else if (ps_match(ps, NoteTokenType.TAG, "youtube")) {
            let tag = ps_parse_tag (ps);

            let video_id;
            let regex = /^.*(youtu.be\/|youtube(-nocookie)?.com\/(v\/|.*u\/\w\/|embed\/|.*v=))([\w-]{11}).*/
            var match = tag.content.match(regex);
            if (match) {
                video_id = match[4];
            } else {
                video_id = "";
            }

            // Assume 16:9 aspect ratio
            let size = compute_media_size(tag.attributes, 16/9, content_width - 30);

            let dom_element = document.createElement("iframe")
            dom_element.setAttribute("width", size[0]);
            dom_element.setAttribute("height", size[1]);
            dom_element.setAttribute("style", "margin: 0 auto; display: block;");
            dom_element.setAttribute("src", "https://www.youtube-nocookie.com/embed/" + video_id);
            dom_element.setAttribute("frameborder", "0");
            dom_element.setAttribute("allow", "accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture");
            dom_element.setAttribute("allowfullscreen", "");
            append_dom_element(context_stack, dom_element);

        } else if (ps_match(ps, NoteTokenType.TAG, "image")) {
            let tag = ps_parse_tag (ps);

            let img_element = document.createElement("img")
            img_element.setAttribute("src", "files/" + tag.content)
            img_element.setAttribute("width", content_width)
            append_dom_element(context_stack, img_element);

        } else if (ps_match(ps, NoteTokenType.TAG, "code")) {
            let attributes = ps_parse_tag_attributes(ps);
            // TODO: Actually do something with the passed language name

            let code_content = parse_balanced_brace_block(ps)
            if (code_content != null) {
                let code_element = document.createElement("code");
                code_element.classList.add("code-inline");
                code_element.innerHTML = code_content;

                append_dom_element(context_stack, code_element);
            }

        } else if (ps_match(ps, NoteTokenType.TAG, "note")) {
            let tag = ps_parse_tag (ps);
            let note_title = tag.content;

            let link_element = document.createElement("a")
            link_element.setAttribute("onclick", "return open_note('" + note_title_to_id[note_title] + "');")
            link_element.setAttribute("href", "#")
            link_element.classList.add("note-link")
            link_element.innerHTML = note_title

            append_dom_element(context_stack, link_element);

        } else if (ps_match(ps, NoteTokenType.TAG, "html")) {
            // TODO: How can we support '}' characters here?. I don't think
            // assuming there will be balanced braces is an option here, as it
            // is in the \code tag. We most likely will need to implement user
            // defined termintating strings.
            let tag = ps_parse_tag (ps);
            let head_ctx = array_end(context_stack);
            head_ctx.dom_element.innerHTML += tag.content;

        } else {
            let head_ctx = array_end(context_stack);
            head_ctx.dom_element.innerHTML += ps_literal_token(ps);
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

// TODO: User callbacks will be modifying the tree. It's possible the user
// messes up and for example adds a cycle into the tree, we should detect such
// problem and avoid maybe later entering an infinite loop.
function block_tree_user_callbacks (root, block=null, containing_array=null, index=-1)
{
    // Convenience so just a single parameter is required in the initial call
    if (block === null) block = root;

    if (block.block_content != null) {
        block.block_content.forEach (function(sub_block, index) {
            block_tree_user_callbacks (root, sub_block, block.block_content, index);
        });

    } else {
        let ps = new ParserState(block.inline_content);

        let string_replacements = [];
        while (!ps.is_eof && !ps.error) {
            let start = ps.pos;
            let tok = ps_next_content (ps);
            if (ps_match(ps, NoteTokenType.TAG, null) && __g_user_tags[tok.value] !== undefined) {
                let user_tag = __g_user_tags[tok.value];
                let result = user_tag.callback (ps, root, block, user_tag.user_data);

                if (result !== null) {
                    if (typeof(result) === "string") {
                        string_replacements.push ([start, ps.pos, result])
                    } else {
                        containing_array.splice(index, 1, ...result);
                        break;
                    }
                }
            }
        }

        for (let i=string_replacements.length - 1; i >= 0; i--) {
            let replacement = string_replacements [i];
            block.inline_content = block.inline_content.substring(0, replacement[0]) + replacement[2] + block.inline_content.substring(replacement[1]);
        }

        // TODO: What will happen if a parse error occurs here?. We should print
        // some details about which user function failed by checking if there
        // is an error in ps.
    }
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
        let pre_element = document.createElement("pre");
        dom_element.appendChild(pre_element);

        let code_element = document.createElement("code")
        code_element.classList.add("code-block")
        // If we use line numbers, this should be the padding WRT the
        // column containing the numbers.
        // code_dom.style.paddingLeft = "0.25em"
        code_element.style.display = "table";

        code_element.innerHTML = block.inline_content;
        pre_element.appendChild(code_element);

        // This is a hack. It's the only way I found to show tight, centered
        // code blocks if they are smaller than content_width and at the same
        // time show horizontal scrolling if they are wider.
        //
        // We need to do this because "display: table", disables scrolling, but
        // "display: block" sets width to be the maximum possible. Here we
        // first create the element with "display: table", compute its width,
        // then change it to "display: block" but if it was smaller than
        // content_width, we explicitly set its width.
        let tight_width = code_element.scrollWidth;
        code_element.style.display = "block";
        if (tight_width < content_width) {
            code_element.style.width = tight_width - code_block_padding*2 + "px";
        }

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

function parse_note_text(note_text)
{
    let ps = new ParserState(note_text)

    let root_block = new container_block_new(BlockType.ROOT, 0);

    // Expect a title as the start of the note, fail if no title is found.
    let tok = ps_next_peek (ps)
    if (tok.type !== NoteTokenType.TITLE) {
        ps.error = true;
        ps.error_msg = sprintf("Notes must start with a Heading 1 title.");
    }


    // Parse note's content
    let block_stack = [root_block];
    while (!ps.is_eof && !ps.error) {
        let curr_block_idx = 0;
        let tok = ps_next(ps);

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
            let new_paragraph = push_block (block_stack, leaf_block_new(BlockType.PARAGRAPH, tok.margin, tok.value));

            // Append all paragraph continuation lines. This ensures all paragraphs
            // found at the beginning of the iteration followed an empty line.
            let tok_peek = ps_next_peek(ps);
            while (tok_peek.is_eol && tok_peek.type == NoteTokenType.PARAGRAPH) {
                new_paragraph.inline_content += tok_peek.value;
                ps_next(ps);

                tok_peek = ps_next_peek(ps);
            }

        } else if (ps_match(ps, NoteTokenType.CODE_HEADER, null)) {
            let new_code_block = push_block (block_stack, leaf_block_new(BlockType.CODE, tok.margin, ""));

            let min_leading_spaces = Infinity;
            let is_start = true; // Used to strip trailing empty lines.
            let tok_peek = ps_next_peek(ps);
            while (tok_peek.is_eol && tok_peek.type == NoteTokenType.CODE_LINE) {
                ps_next(ps);

                if (!is_start || !is_empty_line(tok_peek.value)) {
                    is_start = false;

                    let space_count = tok_peek.value.search(/\S/)
                    if (space_count >= 0) { // Ignore empty where we couldn't find any non-whitespace character.
                        min_leading_spaces = Math.min (min_leading_spaces, space_count)
                    }

                    new_code_block.inline_content += html_escape(tok_peek.value);
                }

                tok_peek = ps_next_peek(ps);
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

            let tok_peek = ps_next_peek(ps);
            if (tok_peek.is_eol && tok_peek.type == NoteTokenType.PARAGRAPH) {
                ps_next(ps);

                // Use list's margin... maybe this will never be read?...
                let new_paragraph = push_block(block_stack, leaf_block_new(BlockType.PARAGRAPH, tok.margin, tok_peek.value));

                // Append all paragraph continuation lines. This ensures all paragraphs
                // found at the beginning of the iteration followed an empty line.
                tok_peek = ps_next_peek(ps);
                while (tok_peek.is_eol && tok_peek.type == NoteTokenType.PARAGRAPH) {
                    new_paragraph.inline_content += tok_peek.value;
                    ps_next(ps);

                    tok_peek = ps_next_peek(ps);
                }
            }
        }
    }

    block_tree_user_callbacks (root_block)

    return root_block;
}

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

// NOTE: This doesn't get the note from the server, it has to have been queried
// previously and added to the related notes global state. By default when
// opening a note, all linked notes will be requested too.
function get_note_by_id (note_id)
{
    return __g_related_notes[note_id];
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

// This is here because it's trying to emulate how users would define custom
// tags. We use it for an internal tag just as test for the API.
function summary_tag (ps, root, block)
{
    let tag = ps_parse_tag (ps);

    let note_id = note_title_to_id[tag.content]

    let result_blocks = null;
    if (note_id !== undefined) {
        let note_content = get_note_by_id (note_id);
        let note_tree = parse_note_text (note_content);

        let heading_block = note_tree.block_content[0];
        // NOTE: We can use inline tags because they are processed at a later step.
        heading_block.inline_content = "\\note{" + heading_block.inline_content + "}"
        // TODO: Don't use fixed heading of 2, instead look at the context
        // where the tag is used and use the highest heading so far. This will
        // be an interesting excercise in making sure this context is easy to
        // access from user defined tags.
        heading_block.heading_number = 2;
        result_blocks = [heading_block];

        if (note_tree.block_content[1].type === BlockType.PARAGRAPH) {
            result_blocks.push(note_tree.block_content[1]);
        }

        // TODO: Add some user defined wrapper that allows setting a different
        // style for blocks of summary. For example if we ever get inline
        // editing from HTML we would like to disable editing in summary blocks
        // and have some UI to modify the target.
    }

    // Force replacement of original block with following sequence
    return result_blocks;
}

new_user_tag ('summary', summary_tag);

function handle_math_tag (ps, display_mode=false)
{
    let tag = ps_parse_tag_balanced_braces (ps);
    var html = katex.renderToString(tag.content, {
        throwOnError: false,
        displayMode: display_mode
    });

    // When katex gets an error it generates HTML with unbalanced braces.
    // Currently \html tag doesn't allow } characters in it, HTML encode them
    // so our parser ignores them.
    return "\\html{" + html.replace("}", "&#125;") + "}"
}

new_user_tag ('math', function summary_tag (ps, root, block)
{
    return handle_math_tag (ps);
}
);

// TODO: Is this a good way to implement display style?. I think capitalizing
// the starting M is easier to memorize than adding an attribute with a name
// that has to be remembered, is it display, displayMode, displat-mode=true?.
// An alternative could be to automatically detect math tags used as whole
// paragraphs and make these automatically display mode equations.
new_user_tag ('Math', function summary_tag (ps, root, block)
{
    return handle_math_tag (ps, true);
}
);

navigate_to_current_url()

