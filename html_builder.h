/*
 * Copyright (C) 2021 Santiago León O.
 */

BINARY_TREE_NEW(attribute_map, string_t, string_t, strcmp(str_data(&a),str_data(&b)))

struct html_element_t {
    string_t tag;
    struct attribute_map_tree_t attributes;

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

#define mem_pool_variable_ensure(name) \
{                                      \
    if (name->pool == NULL) {          \
        name->pool = &name->_pool;     \
    }                                  \
}

struct html_element_t* html_new_node (struct html_t *html)
{
    mem_pool_variable_ensure (html);

    struct html_element_t *new_element = NULL;
    if (html->element_fl != NULL) {
        new_element = LINKED_LIST_POP (html->element_fl);
    } else {
        new_element = mem_pool_push_struct (html->pool, struct html_element_t);
    }

    *new_element = ZERO_INIT (struct html_element_t);

    return new_element;
}

struct html_element_t* html_new_element (struct html_t *html, char *tag_name)
{
    struct html_element_t *new_element = html_new_node (html);

    str_set (&new_element->tag, tag_name);

    if (html->root == NULL) {
        html->root = new_element;
    }

    return new_element;
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

void html_element_append_text (struct html_t *html, struct html_element_t *html_element, char *text)
{
    struct html_element_t *new_text_node = html_new_node (html);
    str_set (&new_text_node->text, text);
    LINKED_LIST_APPEND (html_element->children, new_text_node);
}

void html_element_attribute_set (struct html_t *html, struct html_element_t *html_element, char *attribute, char *value)
{
    mem_pool_variable_ensure (html);

    string_t attribute_str = {0};
    str_set (&attribute_str, attribute);
    //str_pool (html->pool, &attribute_str);

    string_t value_str = {0};
    str_set (&value_str, value);
    //str_pool (html->pool, &value_str);

    attribute_map_tree_insert (&html_element->attributes, attribute_str, value_str);
}

// NOTE: Do not pass multiple comma-separated classes as value, instead call
// this function multiple times.
void html_element_class_add (struct html_t *html, struct html_element_t *html_element, char *value)
{
    mem_pool_variable_ensure (html);

    string_t attr = {0};
    str_set (&attr, "class");

    struct attribute_map_tree_node_t *node;
    attribute_map_tree_lookup (&html_element->attributes, attr, &node);
    if (node == NULL) {
        html_element_attribute_set (html, html_element, str_data(&attr), value);

    } else {
        str_cat_printf (&node->value, ",%s", value);
    }
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

char* html_to_str (struct html_t *html, mem_pool_t *pool, int indent)
{
    string_t result = {0};

    str_cat_html_element (&result, html->root, indent, 0);
    char *res = pom_strdup(pool, str_data(&result));

    str_free (&result);

    return res;
}
