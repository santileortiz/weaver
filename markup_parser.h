/*
 * Copyright (C) 2021 Santiago León O.
 */

char* markup_to_html (mem_pool_t *pool, char *markup, char *id, int x);

BINARY_TREE_NEW(attribute_map, string_t, string_t, strcmp(str_data(&a),str_data(&b)))

struct html_element_t {
    string_t tag;
    struct attribute_map_tree_t attributes;

    struct html_element_t *next;

    string_t text;
    struct html_element_t *children;
    struct html_element_t *children_end;
};

#if defined(MARKUP_PARSER_IMPL)

struct html_builder_t {
    mem_pool_t pool;

    struct html_element_t *element_fl;

    struct html_element_t *root;
};

struct html_element_t* html_builder_new_node (struct html_builder_t *html)
{
    struct html_element_t *new_element = NULL;
    if (html->element_fl != NULL) {
        new_element = LINKED_LIST_POP (html->element_fl);
    } else {
        new_element = mem_pool_push_struct (&html->pool, struct html_element_t);
    }

    *new_element = ZERO_INIT (struct html_element_t);

    return new_element;
}

struct html_element_t* html_builder_new_element (struct html_builder_t *html, char *tag_name)
{
    struct html_element_t *new_element = html_builder_new_node (html);

    str_set (&new_element->tag, tag_name);

    if (html->root == NULL) {
        html->root = new_element;
    }

    return new_element;
}

void html_builder_element_append_child (struct html_builder_t *html, struct html_element_t *html_element, struct html_element_t *child)
{
    LINKED_LIST_APPEND (html_element->children, child);
}

void html_builder_element_set_text (struct html_builder_t *html, struct html_element_t *html_element, char *text)
{
    struct html_element_t *new_text_node = html_builder_new_node (html);
    str_set (&new_text_node->text, text);

    if (html_element->children != NULL) {
        html_element->children_end->next = html->element_fl;
        html->element_fl = html_element->children;

        html_element->children_end = new_text_node;
        html_element->children = new_text_node;
    }
}

void html_builder_element_append_text (struct html_builder_t *html, struct html_element_t *html_element, char *text)
{
    struct html_element_t *new_text_node = html_builder_new_node (html);
    str_set (&new_text_node->text, text);
    LINKED_LIST_APPEND (html_element->children, new_text_node);
}

void html_builder_element_set_attribute (struct html_builder_t *html, struct html_element_t *html_element, char *attribute, char *value)
{
    string_t attribute_str = {0};
    str_set (&attribute_str, attribute);
    str_pool (&html->pool, &attribute_str);

    string_t value_str = {0};
    str_set (&value_str, value);
    str_pool (&html->pool, &value_str);

    attribute_map_tree_insert (&html_element->attributes, attribute_str, value_str);
}

static inline
bool html_element_is_text_node (struct html_element_t *element)
{
    return str_len (&element->text) > 0;
}

void str_cat_html_element (string_t *str, struct html_element_t *element, int indent, int curr_indent)
{
    if (html_element_is_text_node (element)) {
        str_cat (str, &element->text);

    } else {
        str_cat_indented_printf (str, curr_indent, "<%s", str_data(&element->tag));

        BINARY_TREE_FOR(attribute_map, &element->attributes, attr_node)
        {
            str_cat_printf (str, " %s=\"%s\"", str_data(&attr_node->key), str_data(&attr_node->value));
        }

        str_cat_printf (str, ">");

        if (element->children != NULL) {
            bool was_text_node = false;
            LINKED_LIST_FOR (struct html_element_t*, curr_child, element->children)
            {
                if (!html_element_is_text_node (curr_child)) {
                    was_text_node = true;
                    str_cat_indented_printf (str, curr_indent, "\n");
                }
                str_cat_html_element (str, curr_child, indent, curr_indent+indent);
            }

            if (was_text_node) {
                str_cat_indented_printf (str, curr_indent, "\n");
                str_cat_indented_printf (str, curr_indent, "</%s>", str_data(&element->tag));

            } else {
                str_cat_printf (str, "</%s>", str_data(&element->tag));
            }

        } else {
            str_cat_printf (str, "</%s>", str_data(&element->tag));
        }
    }
}

char* html_builder_to_str (struct html_builder_t *html, mem_pool_t *pool, int indent)
{
    string_t result = {0};
    str_pool (pool, &result);

    str_cat_html_element (&result, html->root, indent, 0);

    char *res = pom_strdup(pool, str_data(&result));
    str_free (&result);

    return res;
}



struct user_tag_t {
    string_t tag_name;
};

//function UserTag (tag_name, callback, user_data) {
//    this.name = tag_name;
//    this.callback = callback;
//    this.user_data = user_data;
//}
//
//function new_user_tag (tag_name, callback)
//{
//    __g_user_tags[tag_name] = new UserTag(tag_name, callback);
//}
//
//function note_element_new(id, x)
//{
//    let new_note = document.createElement("div")
//    new_note.id = id
//    new_note.classList.add("note")
//    new_note.style.left = x + "px"
//
//    return new_note;
//}
//
//let NoteTokenType = {
//    UNKNOWN: 0,
//    TITLE: 1,
//    PARAGRAPH: 2,
//    BULLET_LIST: 3,
//    NUMBERED_LIST: 4,
//    TAG: 5,
//    OPERATOR: 6,
//    SPACE: 7,
//    TEXT: 8,
//    CODE_HEADER: 9,
//    CODE_LINE: 10,
//    BLANK_LINE: 11,
//    EOF: 12
//}
//
//token_type_names = []
//for (let e in NoteTokenType) {
//    if (NoteTokenType.hasOwnProperty(e)) {
//        token_type_names.push('' + e)
//    }
//}
//
//function Token () {
//    this.value = '';
//    this.type = NoteTokenType.UNKNOWN;
//    this.margin = 0
//    this.content_start = 0
//    this.heading_number = 0
//    this.is_eol = false;
//}
//
//function ParserState (str) {
//    this.error = false
//    this.error_msg = false
//    this.is_eof = false
//    this.str = str;
//
//    this.pos = 0;
//    this.pos_peek = 0;
//
//    this.token = null;
//    this.token_peek = null;
//}
//
//let pos_is_eof = function(ps) {
//    return ps.pos >= ps.str.length
//}
//
//let pos_is_operator = function(ps) {
//    return !pos_is_eof(ps) && char_in_str(ps.str.charAt(ps.pos), ",=[]{}\n\\")
//}
//
//let pos_is_digit = function(ps) {
//    return !pos_is_eof(ps) && char_in_str(ps.str.charAt(ps.pos), "1234567890")
//}
//
//let pos_is_space = function(ps) {
//    return !pos_is_eof(ps) && char_in_str(ps.str.charAt(ps.pos), " \t")
//}
//
//let is_empty_line = function(line) {
//    let pos = 0;
//    while (char_in_str(line.charAt(pos), " \t")) {
//        pos++;
//    }
//
//    return line.charAt(pos) === "\n" || pos+1 === line.length-1;
//}
//
//// NOTE: chars.search() doesn't work if chars contains \ because it assumes we
//// are passing an incomplete regex, sigh..
//function char_in_str (c, chars)
//{
//    for (let i=0; i<chars.length; i++) {
//        if (chars.charAt(i) === c) {
//            return true
//        }
//    }
//
//    return false
//}
//
//function str_at(haystack, pos, needle)
//{
//    let found = true;
//    let i = 0;
//    while (i < needle.length) {
//        if (haystack.charAt(pos+i) !== needle.charAt(i)) {
//            found = false;
//            break;
//        }
//        i++;
//    }
//
//    return found;
//}
//
//function advance_line(ps)
//{
//    let start = ps.pos;
//    while (!pos_is_eof(ps) && ps.str.charAt(ps.pos) !== "\n") {
//        ps.pos++;
//    }
//
//    // Advance over the \n character. We leave \n characters because they are
//    // handled by the inline parser. Sometimes they are ignored (between URLs),
//    // and sometimes they are replaced to space (multiline paragraphs).
//    ps.pos++; 
//
//    return ps.str.substr(start, ps.pos - start);
//}
//
//function ps_parse_tag_attributes (ps)
//{
//    let attributes = {
//        positional: [],
//        named: {}
//    }
//    if (ps.str.charAt(ps.pos) === "[") {
//        while (!ps.is_eof && ps.str.charAt(ps.pos) !== "]") {
//            ps.pos++;
//
//            let start = ps.pos;
//            if (ps.str.charAt(ps.pos) !== "\"") {
//                // TODO: Allow quote strings for values. This is to allow
//                // values containing "," or "]".
//            }
//
//            while (ps.str.charAt(ps.pos) !== "," &&
//                   ps.str.charAt(ps.pos) !== "=" &&
//                   ps.str.charAt(ps.pos) !== "]") {
//                ps.pos++;
//            }
//
//            if (ps.str.charAt(ps.pos) === "," || ps.str.charAt(ps.pos) === "]") {
//                let value = ps.str.substr(start, ps.pos - start).trim();
//                attributes.positional.push (value)
//
//            } else {
//                let name = ps.str.substr(start, ps.pos - start).trim();
//
//                ps.pos++;
//                let value_start = ps.pos;
//                while (ps.str.charAt(ps.pos) !== "," &&
//                       ps.str.charAt(ps.pos) !== "]") {
//                    ps.pos++;
//                }
//
//                let value = ps.str.substr(value_start, ps.pos - value_start).trim();
//
//                attributes.named[name] = value
//            }
//
//            if (ps.str.charAt(ps.pos) !== "]") {
//                ps.pos++;
//            }
//        }
//
//        ps.pos++;
//    }
//
//    return attributes;
//}
//
//function ps_parse_tag (ps)
//{
//    let tag = {
//        attributes: null,
//        content: null,
//    };
//
//    let attributes = ps_parse_tag_attributes(ps);
//    let content = "";
//
//    ps_expect_content (ps, NoteTokenType.OPERATOR, "{")
//    tok = ps_next_content(ps)
//    while (!ps.is_eof && !ps.error && !ps_match(ps, NoteTokenType.OPERATOR, "}")) {
//        content += tok.value
//        tok = ps_next_content(ps)
//    }
//
//    if (!ps.error) {
//        tag.attributes = attributes;
//        tag.content = content;
//    }
//
//    return tag;
//}
//
//function ps_parse_tag_balanced_braces (ps)
//{
//    let tag = {
//        attributes: null,
//        content: null,
//    };
//
//    let attributes = ps_parse_tag_attributes(ps);
//    let content = parse_balanced_brace_block(ps)
//
//    if (!ps.error) {
//        tag.attributes = attributes;
//        tag.content = content;
//    }
//
//    return tag;
//}
//
//function ps_next_peek(ps)
//{
//    // TODO: Forbid calling peek twice in a row. ps_next() must be called in
//    // between because we don't support arbitrarily deep peeking, if we did, we
//    // would have to implement this as a stack with push and pop.
//
//    let tok = new Token();
//
//    let backup_pos = ps.pos;
//    while (pos_is_space(ps)) {
//        ps.pos++
//    }
//
//    tok.value = null;
//    tok.type = NoteTokenType.PARAGRAPH;
//    let non_space_pos = ps.pos;
//    if (pos_is_eof(ps)) {
//        ps.is_eof = true;
//        ps.is_eol = true;
//        tok.type = NoteTokenType.EOF;
//
//    } else if (str_at(ps.str, ps.pos, "- ") || str_at(ps.str, ps.pos, "* ")) {
//        ps.pos++;
//        while (pos_is_space(ps)) {
//            ps.pos++;
//        }
//
//        tok.type = NoteTokenType.BULLET_LIST;
//        tok.value = ps.str.charAt(non_space_pos);
//        tok.margin = non_space_pos - backup_pos;
//        tok.content_start = ps.pos - non_space_pos;
//
//    } else if (pos_is_digit(ps)) {
//        while (pos_is_digit(ps)) {
//            ps.pos++;
//        }
//
//        let number_end = ps.pos;
//        if (str_at(ps.str, ps.pos, ". ") && ps.pos - non_space_pos <= 9) {
//            ps.pos++;
//            while (pos_is_space(ps)) {
//                ps.pos++;
//            }
//
//            tok.type = NoteTokenType.NUMBERED_LIST;
//            tok.value = ps.str.substr(non_space_pos, number_end - non_space_pos);
//            tok.margin = non_space_pos - backup_pos;
//            tok.content_start = ps.pos - non_space_pos;
//
//        } else {
//            // It wasn't a numbered list marker, restore position.
//            ps.pos = backup_pos;
//        }
//
//    } else if (ps.str.charAt(ps.pos) === "#") {
//        let heading_number = 1;
//        ps.pos++
//        while (ps.str.charAt(ps.pos) === "#") {
//            heading_number++;
//            ps.pos++
//        }
//
//        if (pos_is_space (ps) && heading_number <= 6) {
//            while (pos_is_space(ps)) {
//                ps.pos++;
//            }
//
//            tok.value = advance_line (ps).trim();
//            tok.is_eol = true;
//            tok.margin = non_space_pos - backup_pos;
//            tok.type = NoteTokenType.TITLE;
//            tok.heading_number = heading_number;
//        }
//
//    } else if (str_at(ps.str, ps.pos, "\\code")) {
//        ps.pos += "\\code".length;
//        let attributes = ps_parse_tag_attributes(ps);
//
//        if (ps.str.charAt(ps.pos) !== "{") {
//            tok.type = NoteTokenType.CODE_HEADER;
//            while (pos_is_space(ps) || ps.str.charAt(ps.pos) === "\n") {
//                ps.pos++;
//            }
//
//        } else {
//            // False alarm, this was an inline code block, restore position.
//            ps.pos = backup_pos;
//        }
//
//    } else if (ps.str.charAt(ps.pos) === "|") {
//        ps.pos++;
//        tok.value = advance_line (ps);
//        tok.is_eol = true;
//        tok.type = NoteTokenType.CODE_LINE;
//
//    } else if (ps.str.charAt(ps.pos) === "\n") {
//        ps.pos++;
//        tok.type = NoteTokenType.BLANK_LINE;
//    }
//
//    if (tok.type === NoteTokenType.PARAGRAPH) {
//        tok.value = advance_line (ps);
//        tok.is_eol = true;
//    }
//
//
//    // Because we are only peeking, restore the old position and store the
//    // resulting one in a separate variable.
//    ps.token_peek = tok;
//    ps.pos_peek = ps.pos;
//    ps.pos = backup_pos;
//
//    return ps.token_peek;
//}
//
//function ps_next(ps) {
//    if (ps.token_peek === null) {
//        ps_next_peek(ps);
//    }
//
//    ps.pos = ps.pos_peek;
//    ps.token = ps.token_peek;
//    ps.token_peek = null;
//
//    //console.log (ps.token.type + ": " + "'" + ps.token.value + "'");
//
//    return ps.token;
//}
//
//function ps_match(ps, type, value)
//{
//    let match = false;
//
//    if (type === ps.token.type) {
//        if (value == null) {
//            match = true;
//
//        } else if (ps.token.value === value) {
//                match = true;
//        }
//    }
//
//    return match
//}
//
//let ContextType = {
//    ROOT: 0,
//    HTML: 1,
//}
//
//function ParseContext (type, dom_element) {
//    this.type = type
//    this.dom_element = dom_element
//
//    this.list_type = null
//}
//
//function array_end(array)
//{
//    return array[array.length - 1]
//}
//
//function append_dom_element (context_stack, dom_element)
//{
//    let head_ctx = array_end(context_stack)
//    head_ctx.dom_element.appendChild(dom_element)
//}
//
//function html_escape (str)
//{
//    return str.replaceAll("<", "&lt;").replaceAll(">", "&gt;")
//}
//
//function push_new_html_context (context_stack, tag)
//{
//    let dom_element = document.createElement(tag);
//    append_dom_element (context_stack, dom_element);
//
//    let new_context = new ParseContext(ContextType.HTML, dom_element);
//    context_stack.push(new_context);
//    return new_context;
//}
//
//function pop_context (context_stack)
//{
//    let curr_ctx = context_stack.pop()
//    return array_end(context_stack)
//}
//
//// This function returns the same string that was parsed as the current token.
//// It's used as fallback, for example in the case of ',' and '#' characters in
//// the middle of paragraphs, or unrecognized tag sequences.
////
//// TODO: I feel that using this for operators isn't nice, we should probably
//// have a stateful tokenizer that will consider ',' as part of a TEXT token
//// sometimes and as operators other times. It's probably best to just have an
//// attribute tokenizer/parser, then the inline parser only deals with tags.
//function ps_literal_token (ps)
//{
//    let res = ps.token.value != null ? ps.token.value : ""
//    if (ps_match (ps, NoteTokenType.TAG, null)) {
//        res = "\\" + ps.token.value
//    }
//
//    return res
//}
//
//function ps_next_content(ps)
//{
//    let pos_is_operator = function(ps) {
//        return char_in_str(ps.str.charAt(ps.pos), ",=[]{}\\");
//    }
//
//    let pos_is_space = function(ps) {
//        return char_in_str(ps.str.charAt(ps.pos), " \t");
//    }
//
//    let pos_is_eof = function(ps) {
//        return ps.pos >= ps.str.length;
//    }
//
//    let tok = new Token();
//
//    if (pos_is_eof(ps)) {
//        ps.is_eof = true;
//
//    } else if (ps.str.charAt(ps.pos) === "\\") {
//        // :tag_parsing
//        ps.pos++;
//
//        let start = ps.pos;
//        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps)) {
//            ps.pos++;
//        }
//        tok.value = ps.str.substr(start, ps.pos - start);
//        tok.type = NoteTokenType.TAG;
//
//    } else if (pos_is_operator(ps)) {
//        tok.type = NoteTokenType.OPERATOR;
//        tok.value = ps.str.charAt(ps.pos);
//        ps.pos++;
//
//    } else if (pos_is_space(ps) || ps.str.charAt(ps.pos) === "\n") {
//        // This consumes consecutive spaces into a single one.
//        while (pos_is_space(ps) || ps.str.charAt(ps.pos) === "\n") {
//            ps.pos++;
//        }
//
//        tok.value = " ";
//        tok.type = NoteTokenType.SPACE;
//
//    } else {
//        let start = ps.pos;
//        while (!pos_is_eof(ps) && !pos_is_operator(ps) && !pos_is_space(ps) && ps.str.charAt(ps.pos) !== "\n") {
//            ps.pos++;
//        }
//
//        tok.value = ps.str.substr(start, ps.pos - start);
//        tok.type = NoteTokenType.TEXT;
//    }
//
//    if (pos_is_eof(ps)) {
//        ps.is_eof = true;
//    }
//
//    ps.token = tok;
//
//    //console.log(token_type_names[tok.type] + ": " + tok.value)
//    return tok;
//}
//
//function ps_expect_content(ps, type, value)
//{
//    ps_next_content (ps)
//    if (!ps_match(ps, type, value)) {
//        ps.error = true
//        if (ps.type != type) {
//            if (value == null) {
//                ps.error_msg = sprintf("Expected token of type %s, got '%s' of type %s.",
//                                       token_type_names[type], ps.value, token_type_names[ps.type]);
//            } else {
//                ps.error_msg = sprintf("Expected token '%s' of type %s, got '%s' of type %s.",
//                                       value, token_type_names[type], ps.value, token_type_names[ps.type]);
//            }
//
//        } else {
//            // Value didn't match.
//            ps.error_msg = sprintf("Expected '%s', got '%s'.", value, ps.value);
//        }
//    }
//}
//
//function parse_balanced_brace_block(ps)
//{
//    let content = null
//
//    if (ps.str.charAt(ps.pos) === "{") {
//        content = "";
//        let brace_level = 1;
//        ps_expect_content (ps, NoteTokenType.OPERATOR, "{");
//
//        while (!ps.is_eof && brace_level != 0) {
//            ps_next_content (ps);
//            if (ps_match (ps, NoteTokenType.OPERATOR, "{")) {
//                brace_level++;
//            } else if (ps_match (ps, NoteTokenType.OPERATOR, "}")) {
//                brace_level--;
//            }
//
//            if (brace_level != 0) { // Avoid appending the closing }
//                content += html_escape(ps_literal_token(ps));
//            }
//        }
//    }
//
//    return content;
//}
//
//function compute_media_size (attributes, aspect_ratio, max_width)
//{
//    let a_width = undefined;
//    if (attributes.named.width != undefined) {
//        a_width = Number(attributes.named.width)
//    }
//
//    let a_height = undefined;
//    if (attributes.named.height != undefined) {
//        a_height = Number(attributes.named.height)
//    }
//
//
//    let width, height;
//    if (a_height == undefined &&
//        a_width != undefined && a_width <= max_width) {
//        width = a_width;
//        height = width/aspect_ratio;
//
//    } else if (a_width == undefined &&
//               a_height != undefined && a_height <= max_width/aspect_ratio) {
//        height = a_height;
//        width = height*aspect_ratio;
//
//    } else if (a_width != undefined && a_width <= max_width &&
//               a_height != undefined && a_height <= max_width/aspect_ratio) {
//        width = a_width;
//        height = a_height;
//
//    } else {
//        width = max_width;
//        height = width/aspect_ratio;
//    }
//
//    return [width, height];
//}
//
//// This function parses the content of a block of text. The formatting is
//// limited to tags that affect the formating inline. This parsing function
//// will not add nested blocks like paragraphs, lists, code blocks etc.
////
//// TODO: How do we handle the prescence of nested blocks here?, ignore them and
//// print them or raise an error and stop parsing.
//function block_content_parse_text (container, content)
//{
//    let ps = new ParserState(content);
//
//    let context_stack = [new ParseContext(ContextType.ROOT, container)]
//    while (!ps.is_eof && !ps.error) {
//        let tok = ps_next_content (ps);
//
//        if (ps_match(ps, NoteTokenType.TEXT, null) || ps_match(ps, NoteTokenType.SPACE, null)) {
//            let curr_ctx = array_end(context_stack);
//            curr_ctx.dom_element.innerHTML += tok.value;
//
//        } else if (ps_match(ps, NoteTokenType.OPERATOR, "}") && array_end(context_stack).type !== ContextType.ROOT) {
//            pop_context (context_stack);
//
//        } else if (ps_match(ps, NoteTokenType.TAG, "i") || ps_match(ps, NoteTokenType.TAG, "b")) {
//            let tag = ps.token.value;
//            ps_expect_content (ps, NoteTokenType.OPERATOR, "{");
//            push_new_html_context (context_stack, tag)
//
//        } else if (ps_match(ps, NoteTokenType.TAG, "link")) {
//            let tag = ps_parse_tag (ps);
//
//            // We parse the URL from the title starting at the end. I thinkg is
//            // far less likely to have a non URL encoded > character in the
//            // URL, than a user wanting to use > inside their title.
//            //
//            // TODO: Support another syntax for the rare case of a user that
//            // wants a > character in a URL. Or make sure URLs are URL encoded
//            // from the UI that will be used to edit this.
//            let pos = tag.content.length - 1
//            while (pos > 1 && tag.content.charAt(pos) != ">") {
//                pos--
//            }
//
//            let url = tag.content
//            let title = tag.content
//            if (pos != 1 && tag.content.charAt(pos - 1) === "-") {
//                pos--
//                url = tag.content.substr(pos + 2).trim()
//                title = tag.content.substr(0, pos).trim()
//            }
//
//            let link_element = document.createElement("a")
//            link_element.setAttribute("href", url)
//            link_element.setAttribute("target", "_blank")
//            link_element.innerHTML = title
//
//            append_dom_element(context_stack, link_element);
//
//        } else if (ps_match(ps, NoteTokenType.TAG, "youtube")) {
//            let tag = ps_parse_tag (ps);
//
//            let video_id;
//            let regex = /^.*(youtu.be\/|youtube(-nocookie)?.com\/(v\/|.*u\/\w\/|embed\/|.*v=))([\w-]{11}).*/
//            var match = tag.content.match(regex);
//            if (match) {
//                video_id = match[4];
//            } else {
//                video_id = "";
//            }
//
//            // Assume 16:9 aspect ratio
//            let size = compute_media_size(tag.attributes, 16/9, content_width - 30);
//
//            let dom_element = document.createElement("iframe")
//            dom_element.setAttribute("width", size[0]);
//            dom_element.setAttribute("height", size[1]);
//            dom_element.setAttribute("style", "margin: 0 auto; display: block;");
//            dom_element.setAttribute("src", "https://www.youtube-nocookie.com/embed/" + video_id);
//            dom_element.setAttribute("frameborder", "0");
//            dom_element.setAttribute("allow", "accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture");
//            dom_element.setAttribute("allowfullscreen", "");
//            append_dom_element(context_stack, dom_element);
//
//        } else if (ps_match(ps, NoteTokenType.TAG, "image")) {
//            let tag = ps_parse_tag (ps);
//
//            let img_element = document.createElement("img")
//            img_element.setAttribute("src", "files/" + tag.content)
//            img_element.setAttribute("width", content_width)
//            append_dom_element(context_stack, img_element);
//
//        } else if (ps_match(ps, NoteTokenType.TAG, "code")) {
//            let attributes = ps_parse_tag_attributes(ps);
//            // TODO: Actually do something with the passed language name
//
//            let code_content = parse_balanced_brace_block(ps)
//            if (code_content != null) {
//                let code_element = document.createElement("code");
//                code_element.classList.add("code-inline");
//                code_element.innerHTML = code_content;
//
//                append_dom_element(context_stack, code_element);
//            }
//
//        } else if (ps_match(ps, NoteTokenType.TAG, "note")) {
//            let tag = ps_parse_tag (ps);
//            let note_title = tag.content;
//
//            let link_element = document.createElement("a")
//            link_element.setAttribute("onclick", "return open_note('" + note_title_to_id[note_title] + "');")
//            link_element.setAttribute("href", "#")
//            link_element.classList.add("note-link")
//            link_element.innerHTML = note_title
//
//            append_dom_element(context_stack, link_element);
//
//        } else if (ps_match(ps, NoteTokenType.TAG, "html")) {
//            // TODO: How can we support '}' characters here?. I don't thing
//            // assuming there will be balanced braces is an option here, as it
//            // is in the \code tag. We most likely will need to implement user
//            // defined termintating strings.
//            let tag = ps_parse_tag (ps);
//            let dummy_element = document.createElement("span");
//            dummy_element.innerHTML = tag.content;
//
//            if (dummy_element.firstChild !== null) {
//                append_dom_element(context_stack, dummy_element.firstChild);
//            } else {
//                append_dom_element(context_stack, dummy_element);
//            }
//
//        } else {
//            let head_ctx = array_end(context_stack);
//            head_ctx.dom_element.innerHTML += ps_literal_token(ps);
//        }
//    }
//}
//
//let BlockType = {
//    ROOT: 0,
//    LIST: 1,
//    LIST_ITEM: 2,
//
//    HEADING: 3,
//    PARAGRAPH: 4,
//    CODE: 5
//}
//
//function Block (type, margin, block_content, inline_content, list_marker)
//{
//    this.type = type;
//    this.margin = margin;
//
//    this.block_content = block_content;
//    this.inline_content = inline_content;
//
//    this.heading_number = 1;
//
//    ///////
//    // List
//    this.list_type = null;
//
//    // TODO: I think this useless now??... No one seems to implement custom
//    // numbering of lists.
//    //
//    // 1. Numbered
//    // 3. Third item
//    this.list_marker = list_marker; 
//
//    // Number of characters from the start of a list marker to the start of the
//    // content. Includes al characters of the list marker.
//    // "    -    C" -> 5
//    // "   10.   C" -> 6
//    this.content_start = 0;
//}
//
//function leaf_block_new(type, margin, inline_content)
//{
//    return new Block(type, margin, null, inline_content, null);
//}
//
//function container_block_new(type, margin, list_marker=null, block_content=[])
//{
//    return new Block(type, margin, block_content, null, list_marker);
//}
//
//function push_block (block_stack, new_block)
//{
//    let curr_block = array_end(block_stack);
//
//    console.assert (curr_block.block_content != null, "Pushing nested block to non-container block.");
//    curr_block.block_content.push(new_block);
//
//    block_stack.push(new_block);
//    return new_block;
//}
//
//// TODO: User callbacks will be modifying the tree. It's possible the user
//// messes up and for example adds a cycle into the tree, we should detect such
//// problem and avoid maybe later entering an infinite loop.
//function block_tree_user_callbacks (root, block=null, containing_array=null, index=-1)
//{
//    // Convenience so just a single parameter is required in the initial call
//    if (block === null) block = root;
//
//    if (block.block_content != null) {
//        block.block_content.forEach (function(sub_block, index) {
//            block_tree_user_callbacks (root, sub_block, block.block_content, index);
//        });
//
//    } else {
//        let ps = new ParserState(block.inline_content);
//
//        let string_replacements = [];
//        while (!ps.is_eof && !ps.error) {
//            let start = ps.pos;
//            let tok = ps_next_content (ps);
//            if (ps_match(ps, NoteTokenType.TAG, null) && __g_user_tags[tok.value] !== undefined) {
//                let user_tag = __g_user_tags[tok.value];
//                let result = user_tag.callback (ps, root, block, user_tag.user_data);
//
//                if (result !== null) {
//                    if (typeof(result) === "string") {
//                        string_replacements.push ([start, ps.pos, result])
//                    } else {
//                        containing_array.splice(index, 1, ...result);
//                        break;
//                    }
//                }
//            }
//        }
//
//        for (let i=string_replacements.length - 1; i >= 0; i--) {
//            let replacement = string_replacements [i];
//            block.inline_content = block.inline_content.substring(0, replacement[0]) + replacement[2] + block.inline_content.substring(replacement[1]);
//        }
//
//        // TODO: What will happen if a parse error occurs here?. We should print
//        // some details about which user function failed by checking if there
//        // is an error in ps.
//    }
//}
//
//function block_tree_to_dom (block, dom_element)
//{
//    if (block.type === BlockType.PARAGRAPH) {
//        let new_dom_element = document.createElement("p");
//        dom_element.appendChild(new_dom_element);
//        block_content_parse_text (new_dom_element, block.inline_content);
//        new_dom_element.innerHTML = new_dom_element.innerHTML.trim();
//
//    } else if (block.type === BlockType.HEADING) {
//        let new_dom_element = document.createElement("h" + block.heading_number);
//        dom_element.appendChild(new_dom_element);
//        block_content_parse_text (new_dom_element, block.inline_content);
//        new_dom_element.innerHTML.trim();
//
//    } else if (block.type === BlockType.CODE) {
//        let pre_element = document.createElement("pre");
//        dom_element.appendChild(pre_element);
//
//        let code_element = document.createElement("code")
//        code_element.classList.add("code-block")
//        // If we use line numbers, this should be the padding WRT the
//        // column containing the numbers.
//        // code_dom.style.paddingLeft = "0.25em"
//        code_element.style.display = "table";
//
//        code_element.innerHTML = block.inline_content;
//        pre_element.appendChild(code_element);
//
//        // This is a hack. It's the only way I found to show tight, centered
//        // code blocks if they are smaller than content_width and at the same
//        // time show horizontal scrolling if they are wider.
//        //
//        // We need to do this because "display: table", disables scrolling, but
//        // "display: block" sets width to be the maximum possible. Here we
//        // first create the element with "display: table", compute its width,
//        // then change it to "display: block" but if it was smaller than
//        // content_width, we explicitly set its width.
//        let tight_width = code_element.scrollWidth;
//        code_element.style.display = "block";
//        if (tight_width < content_width) {
//            code_element.style.width = tight_width - code_block_padding*2 + "px";
//        }
//
//    } else if (block.type === BlockType.ROOT) {
//        block.block_content.forEach (function(sub_block) {
//            block_tree_to_dom(sub_block, dom_element);
//        });
//
//    } else if (block.type === BlockType.LIST) {
//        let new_dom_element = document.createElement("ul");
//        if (block.list_type === NoteTokenType.NUMBERED_LIST) {
//            new_dom_element = document.createElement("ol");
//        }
//        dom_element.appendChild(new_dom_element);
//
//        block.block_content.forEach (function(sub_block) {
//            block_tree_to_dom(sub_block, new_dom_element);
//        });
//
//    } else if (block.type === BlockType.LIST_ITEM) {
//        let new_dom_element = document.createElement("li");
//        dom_element.appendChild(new_dom_element);
//
//        block.block_content.forEach (function(sub_block) {
//            block_tree_to_dom(sub_block, new_dom_element);
//        });
//    }
//}
//
//function parse_note_text(note_text)
//{
//    let ps = new ParserState(note_text)
//
//    let root_block = new container_block_new(BlockType.ROOT, 0);
//
//    // Expect a title as the start of the note, fail if no title is found.
//    let tok = ps_next_peek (ps)
//    if (tok.type !== NoteTokenType.TITLE) {
//        ps.error = true;
//        ps.error_msg = sprintf("Notes must start with a Heading 1 title.");
//    }
//
//
//    // Parse note's content
//    let block_stack = [root_block];
//    while (!ps.is_eof && !ps.error) {
//        let curr_block_idx = 0;
//        let tok = ps_next(ps);
//
//        // Match the indentation of the received token.
//        let next_stack_block = block_stack[curr_block_idx+1]
//        while (curr_block_idx+1 < block_stack.length &&
//               next_stack_block.type === BlockType.LIST &&
//               (tok.margin > next_stack_block.margin ||
//                   tok.type === next_stack_block.list_type && tok.margin === next_stack_block.margin)) {
//            curr_block_idx++;
//            next_stack_block = block_stack[curr_block_idx+1];
//        }
//
//        // Pop all blocks after the current index
//        block_stack.length = curr_block_idx+1;
//
//        if (ps_match(ps, NoteTokenType.TITLE, null)) {
//            let prnt = block_stack[curr_block_idx];
//            let heading_block = push_block (block_stack, leaf_block_new(BlockType.HEADING, tok.margin, tok.value));
//            heading_block.heading_number = tok.heading_number;
//
//        } else if (ps_match(ps, NoteTokenType.PARAGRAPH, null)) {
//            let prnt = block_stack[curr_block_idx];
//            let new_paragraph = push_block (block_stack, leaf_block_new(BlockType.PARAGRAPH, tok.margin, tok.value));
//
//            // Append all paragraph continuation lines. This ensures all paragraphs
//            // found at the beginning of the iteration followed an empty line.
//            let tok_peek = ps_next_peek(ps);
//            while (tok_peek.is_eol && tok_peek.type == NoteTokenType.PARAGRAPH) {
//                new_paragraph.inline_content += tok_peek.value;
//                ps_next(ps);
//
//                tok_peek = ps_next_peek(ps);
//            }
//
//        } else if (ps_match(ps, NoteTokenType.CODE_HEADER, null)) {
//            let new_code_block = push_block (block_stack, leaf_block_new(BlockType.CODE, tok.margin, ""));
//
//            let min_leading_spaces = Infinity;
//            let is_start = true; // Used to strip trailing empty lines.
//            let tok_peek = ps_next_peek(ps);
//            while (tok_peek.is_eol && tok_peek.type == NoteTokenType.CODE_LINE) {
//                ps_next(ps);
//
//                if (!is_start || !is_empty_line(tok_peek.value)) {
//                    is_start = false;
//
//                    let space_count = tok_peek.value.search(/\S/)
//                    if (space_count >= 0) { // Ignore empty where we couldn't find any non-whitespace character.
//                        min_leading_spaces = Math.min (min_leading_spaces, space_count)
//                    }
//
//                    new_code_block.inline_content += html_escape(tok_peek.value);
//                }
//
//                tok_peek = ps_next_peek(ps);
//            }
//
//            // Remove the most leading spaces we can remove. I call this
//            // automatic space normalization.
//            if (min_leading_spaces != Infinity && min_leading_spaces > 0) {
//                new_code_block.inline_content = new_code_block.inline_content.substr(min_leading_spaces)
//                new_code_block.inline_content = new_code_block.inline_content.replaceAll("\n" + " ".repeat(min_leading_spaces), "\n")
//            }
//
//
//        } else if (ps_match(ps, NoteTokenType.BULLET_LIST, null) || ps_match(ps, NoteTokenType.NUMBERED_LIST, null)) {
//            let prnt = block_stack[curr_block_idx];
//            if (prnt.type != BlockType.LIST ||
//                tok.margin >= prnt.margin + prnt.content_start) {
//                let list_block = push_block (block_stack, container_block_new(BlockType.LIST, tok.margin, tok.value));
//                list_block.content_start = tok.content_start;
//                list_block.list_type = tok.type;
//            }
//
//            push_block(block_stack, container_block_new(BlockType.LIST_ITEM, tok.margin, tok.value));
//
//            let tok_peek = ps_next_peek(ps);
//            if (tok_peek.is_eol && tok_peek.type == NoteTokenType.PARAGRAPH) {
//                ps_next(ps);
//
//                // Use list's margin... maybe this will never be read?...
//                let new_paragraph = push_block(block_stack, leaf_block_new(BlockType.PARAGRAPH, tok.margin, tok_peek.value));
//
//                // Append all paragraph continuation lines. This ensures all paragraphs
//                // found at the beginning of the iteration followed an empty line.
//                tok_peek = ps_next_peek(ps);
//                while (tok_peek.is_eol && tok_peek.type == NoteTokenType.PARAGRAPH) {
//                    new_paragraph.inline_content += tok_peek.value;
//                    ps_next(ps);
//
//                    tok_peek = ps_next_peek(ps);
//                }
//            }
//        }
//    }
//
//    block_tree_user_callbacks (root_block)
//
//    return root_block;
//}


char* markup_to_html (mem_pool_t *pool, char *markup, char *id, int x)
{
    struct html_builder_t _html = {0};
    struct html_builder_t *html = &_html;

    string_t buff = {0};

    struct html_element_t *root = html_builder_new_element (html, "div");
    html->root = root;
    html_builder_element_set_attribute (html, root, "id", id);
    html_builder_element_set_attribute (html, root, "class", "note,expanded");
    // TODO: This would be more user friendly long term because adding classes
    // is very common
    //html_builder_element_class_add (root, "note");
    //html_builder_element_class_add (root, "expanded");

    // TODO: Make html_builder_element_set_attribute() receive printf parameters
    // and format string.
    str_set_printf (&buff, "%ipx", x);
    html_builder_element_set_attribute (html, root, "style", str_data(&buff));

    struct html_element_t *child = html_builder_new_element (html, "div");
    html_builder_element_append_text (html, child, "Some Text!!");
    html_builder_element_append_child (html, root, child);

    //struct markup_block_t *root_block = parse_note_text(markup);

    //block_tree_to_html(html, root_block);

    char *res = html_builder_to_str (html, pool, 2);
    //mem_pool_destroy (&html->pool);

    return res;
}

#endif
