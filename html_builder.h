/*
 * Copyright (C) 2021 Santiago León O.
 */

BINARY_TREE_NEW(attribute_map, string_t, string_t, strcmp(str_data(&a),str_data(&b)))

struct html_element_t {
    string_t tag;
    struct attribute_map_t attributes;

    struct html_element_t *next;

    string_t text;
    struct html_element_t *children;
    struct html_element_t *children_end;
};

struct html_t {
    mem_pool_t _pool;
    mem_pool_t *pool;

    struct html_element_t *element_fl;

    struct html_element_t *root;
};

void html_destroy (struct html_t *html)
{
    mem_pool_destroy (html->pool);
}

void html_escape (string_t *str)
{
    str_replace (str, "<", "&lt;", NULL);
    str_replace (str, ">", "&gt;", NULL);
}

struct html_element_t* html_new_node (struct html_t *html)
{
    mem_pool_variable_ensure (html);

    struct html_element_t *new_element = NULL;
    if (html->element_fl != NULL) {
        new_element = LINKED_LIST_POP (html->element_fl);

        // TODO: This isn't implemented in binary_tree.c
        // attribute_map_clear (&new_element->attributes);
        str_set (&new_element->tag, "");
        str_set (&new_element->text, "");

    } else {
        new_element = mem_pool_push_struct (html->pool, struct html_element_t);
        *new_element = ZERO_INIT (struct html_element_t);
        new_element->attributes.pool = html->pool;
        str_pool (html->pool, &new_element->tag);
        str_pool (html->pool, &new_element->text);
    }

    return new_element;
}

#define html_new_element(html,tag_name) html_new_element_strn (html,strlen(tag_name),tag_name)
struct html_element_t* html_new_element_strn (struct html_t *html, ssize_t len, char *tag_name)
{
    struct html_element_t *new_element = html_new_node (html);

    strn_set (&new_element->tag, tag_name, len);

    if (html->root == NULL) {
        html->root = new_element;
    }

    return new_element;
}

struct html_t* html_new (mem_pool_t *pool, char *root_tag)
{
    assert (pool != NULL && root_tag != NULL);

    struct html_t *html = mem_pool_push_struct (pool, struct html_t);
    *html = ZERO_INIT (struct html_t);
    html->pool = pool;

    struct html_element_t *root = html_new_element (html, root_tag);
    html->root = root;

    return html;
}

void html_element_append_child (struct html_t *html, struct html_element_t *html_element, struct html_element_t *child)
{
    LINKED_LIST_APPEND (html_element->children, child);
}

void html_element_set_text (struct html_t *html, struct html_element_t *html_element, char *text)
{
    struct html_element_t *new_text_node = html_new_node (html);
    str_set (&new_text_node->text, text);

    if (html_element->children != NULL) {
        html_element->children_end->next = html->element_fl;
        html->element_fl = html_element->children;

        html_element->children_end = new_text_node;
        html_element->children = new_text_node;
    }
}

#define html_element_append_cstr(html,html_element,cstr) html_element_append_strn(html, html_element, strlen(cstr), cstr);
void html_element_append_strn (struct html_t *html, struct html_element_t *html_element, size_t len, char *text)
{
    struct html_element_t *new_text_node = html_new_node (html);
    strn_set (&new_text_node->text, text, len);
    html_escape (&new_text_node->text);
    LINKED_LIST_APPEND (html_element->children, new_text_node);
}

#define html_element_append_no_escape_cstr(html,html_element,cstr) html_element_append_no_escape_strn(html, html_element, strlen(cstr), cstr);
void html_element_append_no_escape_strn (struct html_t *html, struct html_element_t *html_element, size_t len, char *text)
{
    struct html_element_t *new_text_node = html_new_node (html);
    strn_set (&new_text_node->text, text, len);
    LINKED_LIST_APPEND (html_element->children, new_text_node);
}

// TODO: Make this receive printf parameters and format string.
void html_element_attribute_set (struct html_t *html, struct html_element_t *html_element, char *attribute, char *value)
{
    mem_pool_variable_ensure (html);

    string_t *attribute_str = str_new_pooled (html->pool, attribute);

    struct attribute_map_node_t *node;
    attribute_map_lookup (&html_element->attributes, *attribute_str, &node);
    if (node == NULL) {
        string_t *value_str = str_new_pooled (html->pool, value);
        attribute_map_insert (&html_element->attributes, *attribute_str, *value_str);

    } else {
        str_set (&node->value, value);
    }
}

// NOTE: Don't pass multiple space-separated classes as value, instead call this
// function multiple times.
void html_element_class_add (struct html_t *html, struct html_element_t *html_element, char *value)
{
    mem_pool_variable_ensure (html);

    string_t attr = {0};
    str_set (&attr, "class");

    struct attribute_map_node_t *node;
    attribute_map_lookup (&html_element->attributes, attr, &node);
    if (node == NULL) {
        html_element_attribute_set (html, html_element, str_data(&attr), value);

    } else {
        str_cat_printf (&node->value, " %s", value);
    }
    str_free (&attr);
}

static inline
bool html_element_is_text_node (struct html_element_t *element)
{
    return str_len (&element->text) > 0;
}

static inline
bool html_is_inline_tag (struct html_element_t *element)
{
    char *inline_tags[] = {"pre", "p", "i", "b", "a"};
    for (int i=0; i<ARRAY_SIZE(inline_tags); i++) {
        if (strcmp(str_data(&element->tag), inline_tags[i]) == 0) return true;
    }
    return false;
}

static inline
bool html_is_void_element (struct html_element_t *element)
{
    char *void_elemens[] = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"};
    for (int i=0; i<ARRAY_SIZE(void_elemens); i++) {
        if (strcmp(str_data(&element->tag), void_elemens[i]) == 0) return true;
    }
    return false;
}

static inline
void html_maybe_cat_tag_end (string_t *str, struct html_element_t *element, int curr_indent)
{
    if (!html_is_void_element (element)) {
        str_cat_indented_printf (str, curr_indent, "</%s>", str_data(&element->tag));
    }
}

void str_cat_html_element (string_t *str, struct html_element_t *element, int indent, int curr_indent)
{
    if (html_element_is_text_node (element)) {
        str_cat (str, &element->text);

    } else {
        str_cat_indented_printf (str, curr_indent, "<%s", str_data(&element->tag));

        BINARY_TREE_FOR(attribute_map, &element->attributes, attr_node)
        {
            if (str_len(&attr_node->value) > 0) {
                str_cat_printf (str, " %s=\"%s\"", str_data(&attr_node->key), str_data(&attr_node->value));
            } else {
                str_cat_printf (str, " %s", str_data(&attr_node->key));
            }
        }

        str_cat_printf (str, ">");

        if (element->children != NULL) {
            bool was_inlined = true;
            LINKED_LIST_FOR (struct html_element_t*, curr_child, element->children)
            {
                if (!html_element_is_text_node (curr_child) && !html_is_inline_tag(element)) {
                    was_inlined = false;
                    str_cat_c (str, "\n");
                    str_cat_html_element (str, curr_child, indent, curr_indent+indent);

                } else {
                    str_cat_html_element (str, curr_child, indent, 0);
                }
            }

            if (!was_inlined) {
                str_cat_c (str, "\n");
                html_maybe_cat_tag_end (str, element, curr_indent);

            } else {
                html_maybe_cat_tag_end (str, element, 0);
            }

        } else {
            html_maybe_cat_tag_end (str, element, 0);
        }
    }
}

static inline
void str_cat_html(string_t *str, struct html_t *html, int indent)
{
    str_cat_html_element (str,html->root,indent,0);
    str_cat_c (str, "\n");
}

char* html_to_str (struct html_t *html, mem_pool_t *pool, int indent)
{
    string_t result = {0};

    str_cat_html (&result, html, indent);
    char *res = pom_strdup(pool, str_data(&result));

    str_free (&result);

    return res;
}
