#include "lua_html.h"
#include "lauxlib.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "lexbor/html/parser.h"
#include "lexbor/html/interfaces/document.h"
#include "lexbor/html/serialize.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"
#include "lexbor/dom/interfaces/attr.h"
#include "lexbor/css/parser.h"
#include "lexbor/css/selectors/selectors.h"
#include "lexbor/selectors/selectors.h"

#define NOVELA_DOC_MT      "NoveLA.HTML.Document"
#define NOVELA_ELEMENT_MT  "NoveLA.HTML.Element"
#define NOVELA_ELEMENTS_MT "NoveLA.HTML.Elements"

/* Wrapper structure for HTML Document */
typedef struct {
    lxb_html_document_t *doc;
    lxb_css_parser_t    *css_parser;
    lxb_selectors_t     *selectors;
} lua_lxb_doc_t;

/* Wrapper structure for HTML Element */
typedef struct {
    lxb_dom_node_t *node;
} lua_lxb_elem_t;

/* Context for collecting matched nodes */
typedef struct {
    lua_State      *L;
    int            elements_table_idx;
    int            doc_idx;
    int            count;
    bool           first_only;
} selector_find_ctx_t;

/* Forward declarations */
static void push_element_userdata(lua_State *L, lxb_dom_node_t *node, int doc_idx);
static void create_elements_list(lua_State *L);

/* ── Selectors Callback ─────────────────────────────────────────────────── */

static lxb_status_t selector_match_cb(lxb_dom_node_t *node,
                                      lxb_css_selector_specificity_t spec,
                                      void *ctx_ptr)
{
    selector_find_ctx_t *ctx = (selector_find_ctx_t *)ctx_ptr;
    lua_State *L = ctx->L;

    ctx->count++;
    push_element_userdata(L, node, ctx->doc_idx);
    lua_rawseti(L, ctx->elements_table_idx, ctx->count);

    if (ctx->first_only) {
        return LXB_STATUS_STOP;
    }
    return LXB_STATUS_OK;
}

/* Helper to execute CSS query */
static int execute_css_query(lua_State *L, lxb_dom_node_t *root_node,
                             lua_lxb_doc_t *doc_wrap, int doc_idx,
                             const char *query, size_t query_len,
                             bool first_only)
{
    if (!doc_wrap || !doc_wrap->doc || !doc_wrap->css_parser || !doc_wrap->selectors) {
        if (first_only) {
            lua_pushnil(L);
            return 1;
        } else {
            create_elements_list(L);
            return 1;
        }
    }

    doc_idx = lua_absindex(L, doc_idx);

    lxb_css_selector_list_t *list = lxb_css_selectors_parse(
        doc_wrap->css_parser, (const lxb_char_t *)query, query_len
    );

    if (!list) {
        if (first_only) {
            lua_pushnil(L);
            return 1;
        } else {
            create_elements_list(L);
            return 1;
        }
    }

    if (!first_only) {
        create_elements_list(L);
    }

    selector_find_ctx_t ctx;
    ctx.L = L;
    ctx.doc_idx = doc_idx;
    ctx.count = 0;
    ctx.first_only = first_only;

    if (first_only) {
        /* Create temporary 1-element table to hold result */
        lua_newtable(L);
        ctx.elements_table_idx = lua_gettop(L);
    } else {
        ctx.elements_table_idx = lua_gettop(L);
    }

    lxb_selectors_find(doc_wrap->selectors, root_node, list, selector_match_cb, &ctx);

    if (first_only) {
        if (ctx.count > 0) {
            lua_rawgeti(L, ctx.elements_table_idx, 1);
            lua_remove(L, ctx.elements_table_idx);
        } else {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
    }

    lxb_css_parser_erase(doc_wrap->css_parser);
    return 1;
}

/* ── Document Implementation ────────────────────────────────────────────── */

static int l_html_parse(lua_State *L) {
    size_t len = 0;
    const char *html = luaL_checklstring(L, 1, &len);
    if (!html) {
        lua_pushnil(L);
        return 1;
    }

    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)lua_newuserdatauv(L, sizeof(lua_lxb_doc_t), 0);
    doc_wrap->doc = lxb_html_document_create();
    if (!doc_wrap->doc) {
        lua_pushnil(L);
        return 1;
    }

    lxb_status_t status = lxb_html_document_parse(doc_wrap->doc, (const lxb_char_t *)html, len);
    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc_wrap->doc);
        doc_wrap->doc = NULL;
        lua_pushnil(L);
        return 1;
    }

    doc_wrap->css_parser = lxb_css_parser_create();
    lxb_css_parser_init(doc_wrap->css_parser, NULL);

    doc_wrap->selectors = lxb_selectors_create();
    lxb_selectors_init(doc_wrap->selectors);

    luaL_getmetatable(L, NOVELA_DOC_MT);
    lua_setmetatable(L, -2);
    return 1;
}

static int l_doc_gc(lua_State *L) {
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, 1, NOVELA_DOC_MT);
    if (doc_wrap) {
        if (doc_wrap->selectors) {
            lxb_selectors_destroy(doc_wrap->selectors, true);
            doc_wrap->selectors = NULL;
        }
        if (doc_wrap->css_parser) {
            if (doc_wrap->css_parser->memory) {
                lxb_css_memory_destroy(doc_wrap->css_parser->memory, true);
                doc_wrap->css_parser->memory = NULL;
            }
            lxb_css_parser_destroy(doc_wrap->css_parser, true);
            doc_wrap->css_parser = NULL;
        }
        if (doc_wrap->doc) {
            lxb_html_document_destroy(doc_wrap->doc);
            doc_wrap->doc = NULL;
        }
    }
    return 0;
}

static int l_doc_select(lua_State *L) {
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, 1, NOVELA_DOC_MT);
    size_t qlen = 0;
    const char *query = luaL_checklstring(L, 2, &qlen);

    lxb_dom_node_t *root = lxb_dom_interface_node(doc_wrap->doc);
    return execute_css_query(L, root, doc_wrap, 1, query, qlen, false);
}

static int l_doc_select_first(lua_State *L) {
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, 1, NOVELA_DOC_MT);
    size_t qlen = 0;
    const char *query = luaL_checklstring(L, 2, &qlen);

    lxb_dom_node_t *root = lxb_dom_interface_node(doc_wrap->doc);
    return execute_css_query(L, root, doc_wrap, 1, query, qlen, true);
}

static int l_doc_title(lua_State *L) {
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, 1, NOVELA_DOC_MT);
    size_t len = 0;
    const lxb_char_t *title = lxb_html_document_title(doc_wrap->doc, &len);
    if (title && len > 0) {
        lua_pushlstring(L, (const char *)title, len);
    } else {
        lua_pushliteral(L, "");
    }
    return 1;
}

static int l_doc_body(lua_State *L) {
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, 1, NOVELA_DOC_MT);
    lxb_html_body_element_t *body = lxb_html_document_body_element(doc_wrap->doc);
    if (body) {
        push_element_userdata(L, lxb_dom_interface_node(body), 1);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_doc_head(lua_State *L) {
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, 1, NOVELA_DOC_MT);
    lxb_html_head_element_t *head = lxb_html_document_head_element(doc_wrap->doc);
    if (head) {
        push_element_userdata(L, lxb_dom_interface_node(head), 1);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_doc_index(lua_State *L) {
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, 1, NOVELA_DOC_MT);
    const char *key = luaL_checkstring(L, 2);

    /* 1. Property access */
    if (strcmp(key, "title") == 0) {
        return l_doc_title(L);
    }
    if (strcmp(key, "body") == 0) {
        return l_doc_body(L);
    }
    if (strcmp(key, "head") == 0) {
        return l_doc_head(L);
    }
    if (strcmp(key, "text") == 0) {
        lxb_html_body_element_t *body = lxb_html_document_body_element(doc_wrap->doc);
        lxb_dom_node_t *node = body ? lxb_dom_interface_node(body) : lxb_dom_interface_node(doc_wrap->doc);
        size_t len = 0;
        lxb_char_t *text = lxb_dom_node_text_content(node, &len);
        if (text) {
            lua_pushlstring(L, (const char *)text, len);
            lxb_dom_document_destroy_text(&doc_wrap->doc->dom_document, text);
        } else {
            lua_pushliteral(L, "");
        }
        return 1;
    }
    if (strcmp(key, "html") == 0) {
        lexbor_str_t str = {0};
        lxb_status_t status = lxb_html_serialize_deep_str(lxb_dom_interface_node(doc_wrap->doc), &str);
        if (status == LXB_STATUS_OK && str.data) {
            lua_pushlstring(L, (const char *)str.data, str.length);
            lexbor_str_destroy(&str, doc_wrap->doc->dom_document.text, false);
        } else {
            lua_pushliteral(L, "");
        }
        return 1;
    }

    /* 2. Check methods in NOVELA_DOC_MT */
    luaL_getmetatable(L, NOVELA_DOC_MT);
    lua_getfield(L, -1, key);
    if (!lua_isnil(L, -1)) {
        return 1;
    }
    lua_pop(L, 2);

    lua_pushnil(L);
    return 1;
}

/* ── Element Implementation ─────────────────────────────────────────────── */

static void push_element_userdata(lua_State *L, lxb_dom_node_t *node, int doc_idx) {
    doc_idx = lua_absindex(L, doc_idx);
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)lua_newuserdatauv(L, sizeof(lua_lxb_elem_t), 1);
    elem->node = node;

    /* Associate Document userdata as user value 1 (retains document in GC) */
    lua_pushvalue(L, doc_idx);
    lua_setiuservalue(L, -2, 1);

    luaL_getmetatable(L, NOVELA_ELEMENT_MT);
    lua_setmetatable(L, -2);
}

static int l_elem_gc(lua_State *L) {
    (void)L;
    return 0;
}

static int l_elem_select(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    size_t qlen = 0;
    const char *query = luaL_checklstring(L, 2, &qlen);

    lua_getiuservalue(L, 1, 1);
    int doc_idx = lua_gettop(L);
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, doc_idx, NOVELA_DOC_MT);

    int res = execute_css_query(L, elem->node, doc_wrap, doc_idx, query, qlen, false);
    lua_remove(L, doc_idx);
    return res;
}

static int l_elem_select_first(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    size_t qlen = 0;
    const char *query = luaL_checklstring(L, 2, &qlen);

    lua_getiuservalue(L, 1, 1);
    int doc_idx = lua_gettop(L);
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, doc_idx, NOVELA_DOC_MT);

    int res = execute_css_query(L, elem->node, doc_wrap, doc_idx, query, qlen, true);
    lua_remove(L, doc_idx);
    return res;
}

static int l_elem_text(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    size_t len = 0;
    lxb_char_t *text = lxb_dom_node_text_content(elem->node, &len);
    if (text) {
        lua_pushlstring(L, (const char *)text, len);
        lxb_dom_document_destroy_text(elem->node->owner_document, text);
    } else {
        lua_pushliteral(L, "");
    }
    return 1;
}

static int l_elem_own_text(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    lxb_dom_node_t *child = elem->node ? elem->node->first_child : NULL;
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            size_t tlen = 0;
            lxb_char_t *text = lxb_dom_node_text_content(child, &tlen);
            if (text && tlen > 0) {
                luaL_addlstring(&b, (const char *)text, tlen);
                lxb_dom_document_destroy_text(child->owner_document, text);
            }
        }
        child = child->next;
    }

    luaL_pushresult(&b);
    return 1;
}

static int l_elem_html(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    if (!elem || !elem->node) {
        lua_pushliteral(L, "");
        return 1;
    }

    lexbor_str_t str = {0};
    /* Inner HTML: serialize children */
    lxb_status_t status = lxb_html_serialize_deep_str(elem->node, &str);
    if (status == LXB_STATUS_OK && str.data) {
        lua_pushlstring(L, (const char *)str.data, str.length);
        lexbor_str_destroy(&str, elem->node->owner_document->text, false);
    } else {
        lua_pushliteral(L, "");
    }
    return 1;
}

static int l_elem_outer_html(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    if (!elem || !elem->node) {
        lua_pushliteral(L, "");
        return 1;
    }

    lexbor_str_t str = {0};
    /* Outer HTML: serialize node and descendants */
    lxb_status_t status = lxb_html_serialize_str(elem->node, &str);
    if (status == LXB_STATUS_OK && str.data) {
        lua_pushlstring(L, (const char *)str.data, str.length);
        lexbor_str_destroy(&str, elem->node->owner_document->text, false);
    } else {
        lua_pushliteral(L, "");
    }
    return 1;
}

static int l_elem_attr(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    size_t name_len = 0;
    const char *name = luaL_checklstring(L, 2, &name_len);

    if (elem->node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        lua_pushliteral(L, "");
        return 1;
    }

    lxb_dom_element_t *el = lxb_dom_interface_element(elem->node);
    lxb_dom_attr_t *attr = lxb_dom_element_attr_by_name(el, (const lxb_char_t *)name, name_len);
    if (attr) {
        size_t vlen = 0;
        const lxb_char_t *val = lxb_dom_attr_value(attr, &vlen);
        if (val) {
            lua_pushlstring(L, (const char *)val, vlen);
            return 1;
        }
    }
    lua_pushliteral(L, "");
    return 1;
}

static int l_elem_has_attr(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    size_t name_len = 0;
    const char *name = luaL_checklstring(L, 2, &name_len);

    if (elem->node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        lua_pushboolean(L, 0);
        return 1;
    }

    lxb_dom_element_t *el = lxb_dom_interface_element(elem->node);
    lxb_dom_attr_t *attr = lxb_dom_element_attr_by_name(el, (const lxb_char_t *)name, name_len);
    lua_pushboolean(L, attr != NULL);
    return 1;
}

static int l_elem_has_class(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    size_t cls_len = 0;
    const char *target_class = luaL_checklstring(L, 2, &cls_len);

    if (elem->node->type != LXB_DOM_NODE_TYPE_ELEMENT || cls_len == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }

    lxb_dom_element_t *el = lxb_dom_interface_element(elem->node);
    lxb_dom_attr_t *attr = lxb_dom_element_attr_by_name(el, (const lxb_char_t *)"class", 5);
    if (!attr) {
        lua_pushboolean(L, 0);
        return 1;
    }

    size_t vlen = 0;
    const lxb_char_t *val = lxb_dom_attr_value(attr, &vlen);
    if (!val || vlen == 0) {
        lua_pushboolean(L, 0);
        return 1;
    }

    /* Check if target_class exists in space-separated class list */
    const char *str = (const char *)val;
    const char *end = str + vlen;
    bool found = false;

    while (str < end) {
        while (str < end && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) str++;
        const char *word_start = str;
        while (str < end && *str != ' ' && *str != '\t' && *str != '\n' && *str != '\r') str++;
        size_t word_len = str - word_start;
        if (word_len == cls_len && memcmp(word_start, target_class, cls_len) == 0) {
            found = true;
            break;
        }
    }

    lua_pushboolean(L, found);
    return 1;
}

static int l_elem_tag_name(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    if (elem->node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *el = lxb_dom_interface_element(elem->node);
        size_t len = 0;
        const lxb_char_t *name = lxb_dom_element_qualified_name(el, &len);
        if (name && len > 0) {
            lua_pushlstring(L, (const char *)name, len);
            return 1;
        }
    }
    lua_pushliteral(L, "");
    return 1;
}

static int l_elem_parent(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    lxb_dom_node_t *parent = elem->node ? elem->node->parent : NULL;
    if (parent && parent->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lua_getiuservalue(L, 1, 1);
        push_element_userdata(L, parent, -1);
        lua_remove(L, -2);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_elem_children(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    lua_getiuservalue(L, 1, 1);
    int doc_idx = lua_gettop(L);

    create_elements_list(L);
    int tbl_idx = lua_gettop(L);

    lxb_dom_node_t *child = elem->node ? elem->node->first_child : NULL;
    int count = 0;
    while (child) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            count++;
            push_element_userdata(L, child, doc_idx);
            lua_rawseti(L, tbl_idx, count);
        }
        child = child->next;
    }
    lua_remove(L, doc_idx);
    return 1;
}

static int l_elem_next_sibling(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    lxb_dom_node_t *next = elem->node ? elem->node->next : NULL;
    while (next && next->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        next = next->next;
    }
    if (next) {
        lua_getiuservalue(L, 1, 1);
        push_element_userdata(L, next, -1);
        lua_remove(L, -2);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_elem_prev_sibling(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    lxb_dom_node_t *prev = elem->node ? elem->node->prev : NULL;
    while (prev && prev->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        prev = prev->prev;
    }
    if (prev) {
        lua_getiuservalue(L, 1, 1);
        push_element_userdata(L, prev, -1);
        lua_remove(L, -2);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_elem_remove(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    lxb_dom_node_remove(elem->node);
    return 0;
}

/* Dynamic property and method dispatcher for Element */
static int l_elem_index(lua_State *L) {
    lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_checkudata(L, 1, NOVELA_ELEMENT_MT);
    const char *key = luaL_checkstring(L, 2);

    /* 1. Shosetsu / Jsoup Property Aliases */
    if (strcmp(key, "text") == 0) {
        return l_elem_text(L);
    }
    if (strcmp(key, "html") == 0) {
        return l_elem_html(L);
    }
    if (strcmp(key, "outerHtml") == 0 || strcmp(key, "outer_html") == 0) {
        return l_elem_outer_html(L);
    }
    if (strcmp(key, "href") == 0) {
        lua_pushstring(L, "href");
        return l_elem_attr(L);
    }
    if (strcmp(key, "src") == 0) {
        lua_pushstring(L, "src");
        return l_elem_attr(L);
    }
    if (strcmp(key, "id") == 0) {
        lua_pushstring(L, "id");
        return l_elem_attr(L);
    }
    if (strcmp(key, "class") == 0) {
        lua_pushstring(L, "class");
        return l_elem_attr(L);
    }
    if (strcmp(key, "title") == 0) {
        lua_pushstring(L, "title");
        return l_elem_attr(L);
    }

    /* 2. Check if key is a method in NOVELA_ELEMENT_MT */
    luaL_getmetatable(L, NOVELA_ELEMENT_MT);
    lua_getfield(L, -1, key);
    if (!lua_isnil(L, -1)) {
        return 1; /* Found method */
    }
    lua_pop(L, 2);

    lua_pushnil(L);
    return 1;
}

/* ── Elements List Implementation ───────────────────────────────────────── */

static void create_elements_list(lua_State *L) {
    lua_newtable(L);
    luaL_getmetatable(L, NOVELA_ELEMENTS_MT);
    lua_setmetatable(L, -2);
}

static int l_elems_size(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushinteger(L, (lua_Integer)luaL_len(L, 1));
    return 1;
}

static int l_elems_is_empty(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushboolean(L, luaL_len(L, 1) == 0);
    return 1;
}

static int l_elems_first(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawgeti(L, 1, 1);
    return 1;
}

static int l_elems_last(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_Integer len = luaL_len(L, 1);
    if (len > 0) {
        lua_rawgeti(L, 1, len);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_elems_get(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_Integer idx = luaL_checkinteger(L, 2);
    lua_rawgeti(L, 1, idx);
    return 1;
}

static int l_elems_attr(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawgeti(L, 1, 1);
    if (lua_isnil(L, -1)) {
        lua_pushliteral(L, "");
        return 1;
    }
    /* Move element to arg 1 position, attr name is at 2 */
    lua_replace(L, 1);
    return l_elem_attr(L);
}

static int l_elems_text(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    lua_Integer len = luaL_len(L, 1);
    for (lua_Integer i = 1; i <= len; i++) {
        lua_rawgeti(L, 1, i);
        if (lua_isuserdata(L, -1)) {
            lua_lxb_elem_t *elem = (lua_lxb_elem_t *)luaL_testudata(L, -1, NOVELA_ELEMENT_MT);
            if (elem) {
                size_t tlen = 0;
                lxb_char_t *text = lxb_dom_node_text_content(elem->node, &tlen);
                if (text && tlen > 0) {
                    if (i > 1) luaL_addchar(&b, ' ');
                    luaL_addlstring(&b, (const char *)text, tlen);
                    lxb_dom_document_destroy_text(elem->node->owner_document, text);
                }
            }
        }
        lua_pop(L, 1);
    }
    luaL_pushresult(&b);
    return 1;
}

static int l_elems_for_each(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_Integer len = luaL_len(L, 1);
    for (lua_Integer i = 1; i <= len; i++) {
        lua_pushvalue(L, 2); /* Push function */
        lua_rawgeti(L, 1, i); /* Arg 1: element */
        lua_pushinteger(L, i); /* Arg 2: index */
        lua_call(L, 2, 0);
    }
    return 0;
}

/* ── Global Functional HTML API ─────────────────────────────────────────── */

static int l_global_html_select(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        create_elements_list(L);
        return 1;
    }

    if (lua_isuserdata(L, 1)) {
        if (luaL_testudata(L, 1, NOVELA_ELEMENT_MT)) {
            return l_elem_select(L);
        }
        if (luaL_testudata(L, 1, NOVELA_DOC_MT)) {
            return l_doc_select(L);
        }
    }

    size_t qlen = 0;
    const char *query = luaL_checklstring(L, 2, &qlen);

    /* Otherwise parse string HTML */
    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 1, &hlen);
    lua_pushcfunction(L, l_html_parse);
    lua_pushlstring(L, html, hlen);
    lua_call(L, 1, 1);
    if (lua_isnil(L, -1)) {
        create_elements_list(L);
        return 1;
    }

    int doc_idx = lua_gettop(L);
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, doc_idx, NOVELA_DOC_MT);
    lxb_dom_node_t *root = lxb_dom_interface_node(doc_wrap->doc);
    return execute_css_query(L, root, doc_wrap, doc_idx, query, qlen, false);
}

static int l_global_html_select_first(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        lua_pushnil(L);
        return 1;
    }

    if (lua_isuserdata(L, 1)) {
        if (luaL_testudata(L, 1, NOVELA_ELEMENT_MT)) {
            return l_elem_select_first(L);
        }
        if (luaL_testudata(L, 1, NOVELA_DOC_MT)) {
            return l_doc_select_first(L);
        }
    }

    size_t qlen = 0;
    const char *query = luaL_checklstring(L, 2, &qlen);

    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 1, &hlen);
    lua_pushcfunction(L, l_html_parse);
    lua_pushlstring(L, html, hlen);
    lua_call(L, 1, 1);
    if (lua_isnil(L, -1)) {
        lua_pushnil(L);
        return 1;
    }

    int doc_idx = lua_gettop(L);
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, doc_idx, NOVELA_DOC_MT);
    lxb_dom_node_t *root = lxb_dom_interface_node(doc_wrap->doc);
    return execute_css_query(L, root, doc_wrap, doc_idx, query, qlen, true);
}

static int l_global_html_attr(lua_State *L) {
    int n = lua_gettop(L);
    if (n < 2) {
        lua_pushliteral(L, "");
        return 1;
    }

    lua_lxb_elem_t *elem = NULL;
    const char *attr_name = NULL;
    size_t attr_len = 0;

    if (n == 2) {
        /* html_attr(element, attr_name) */
        elem = (lua_lxb_elem_t *)luaL_testudata(L, 1, NOVELA_ELEMENT_MT);
        attr_name = luaL_checklstring(L, 2, &attr_len);
    } else {
        /* html_attr(html_or_element, selector, attr_name) */
        attr_name = luaL_checklstring(L, 3, &attr_len);
        lua_pushcfunction(L, l_global_html_select_first);
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 2);
        lua_call(L, 2, 1);

        if (lua_isnil(L, -1)) {
            lua_pushliteral(L, "");
            return 1;
        }
        elem = (lua_lxb_elem_t *)luaL_testudata(L, -1, NOVELA_ELEMENT_MT);
    }

    if (!elem || !elem->node || elem->node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        lua_pushliteral(L, "");
        return 1;
    }

    lxb_dom_element_t *el = lxb_dom_interface_element(elem->node);
    lxb_dom_attr_t *attr = lxb_dom_element_attr_by_name(el, (const lxb_char_t *)attr_name, attr_len);
    if (attr) {
        size_t vlen = 0;
        const lxb_char_t *val = lxb_dom_attr_value(attr, &vlen);
        if (val) {
            lua_pushlstring(L, (const char *)val, vlen);
            return 1;
        }
    }

    lua_pushliteral(L, "");
    return 1;
}

static int l_global_html_text(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        lua_pushliteral(L, "");
        return 1;
    }
    if (lua_isuserdata(L, 1)) {
        void *ud = luaL_testudata(L, 1, NOVELA_ELEMENT_MT);
        if (ud) {
            return l_elem_text(L);
        }
    }
    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 1, &hlen);
    lua_pushcfunction(L, l_html_parse);
    lua_pushlstring(L, html, hlen);
    lua_call(L, 1, 1);
    if (lua_isnil(L, -1)) {
        lua_pushliteral(L, "");
        return 1;
    }
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, -1, NOVELA_DOC_MT);
    lxb_html_body_element_t *body = lxb_html_document_body_element(doc_wrap->doc);
    lxb_dom_node_t *node = body ? lxb_dom_interface_node(body) : lxb_dom_interface_node(doc_wrap->doc);
    size_t len = 0;
    lxb_char_t *text = lxb_dom_node_text_content(node, &len);
    if (text) {
        lua_pushlstring(L, (const char *)text, len);
        lxb_dom_document_destroy_text(node->owner_document, text);
    } else {
        lua_pushliteral(L, "");
    }
    return 1;
}

static int l_global_html_remove(lua_State *L) {
    int n = lua_gettop(L);
    if (n < 1) {
        lua_pushliteral(L, "");
        return 1;
    }

    if (lua_isuserdata(L, 1)) {
        void *ud = luaL_testudata(L, 1, NOVELA_ELEMENT_MT);
        if (ud) {
            lua_lxb_elem_t *elem = (lua_lxb_elem_t *)ud;
            for (int i = 2; i <= n; i++) {
                size_t qlen = 0;
                const char *query = lua_tolstring(L, i, &qlen);
                if (query && qlen > 0) {
                    lua_pushcfunction(L, l_global_html_select);
                    lua_pushvalue(L, 1);
                    lua_pushvalue(L, i);
                    lua_call(L, 2, 1);
                    int count = (int)luaL_len(L, -1);
                    for (int j = 1; j <= count; j++) {
                        lua_rawgeti(L, -1, j);
                        lua_lxb_elem_t *sub = (lua_lxb_elem_t *)luaL_testudata(L, -1, NOVELA_ELEMENT_MT);
                        if (sub && sub->node) {
                            lxb_dom_node_remove(sub->node);
                        }
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);
                }
            }
            return l_elem_html(L);
        }
    }

    /* Otherwise string HTML */
    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 1, &hlen);
    lua_pushcfunction(L, l_html_parse);
    lua_pushlstring(L, html, hlen);
    lua_call(L, 1, 1);
    if (lua_isnil(L, -1)) {
        lua_pushlstring(L, html, hlen);
        return 1;
    }

    int doc_idx = lua_gettop(L);
    lua_lxb_doc_t *doc_wrap = (lua_lxb_doc_t *)luaL_checkudata(L, doc_idx, NOVELA_DOC_MT);

    for (int i = 2; i <= n; i++) {
        size_t qlen = 0;
        const char *query = lua_tolstring(L, i, &qlen);
        if (query && qlen > 0) {
            lua_pushcfunction(L, l_global_html_select);
            lua_pushvalue(L, doc_idx);
            lua_pushvalue(L, i);
            lua_call(L, 2, 1);
            int count = (int)luaL_len(L, -1);
            for (int j = 1; j <= count; j++) {
                lua_rawgeti(L, -1, j);
                lua_lxb_elem_t *sub = (lua_lxb_elem_t *)luaL_testudata(L, -1, NOVELA_ELEMENT_MT);
                if (sub && sub->node) {
                    lxb_dom_node_remove(sub->node);
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
    }

    /* Serialize cleaned document body */
    lxb_html_body_element_t *body = lxb_html_document_body_element(doc_wrap->doc);
    lxb_dom_node_t *body_node = body ? lxb_dom_interface_node(body) : lxb_dom_interface_node(doc_wrap->doc);

    lexbor_str_t str = {0};
    lxb_status_t status = lxb_html_serialize_deep_str(body_node, &str);
    if (status == LXB_STATUS_OK && str.data) {
        lua_pushlstring(L, (const char *)str.data, str.length);
        lexbor_str_destroy(&str, doc_wrap->doc->dom_document.text, false);
    } else {
        lua_pushliteral(L, "");
    }
    return 1;
}

/* ── Registration ────────────────────────────────────────────────────────── */

int luaopen_html(lua_State *L) {
    /* 1. Document Metatable */
    if (luaL_newmetatable(L, NOVELA_DOC_MT)) {
        lua_pushcfunction(L, l_doc_index);
        lua_setfield(L, -2, "__index");

        lua_pushcfunction(L, l_doc_gc);
        lua_setfield(L, -2, "__gc");

        lua_pushcfunction(L, l_doc_select);
        lua_setfield(L, -2, "select");

        lua_pushcfunction(L, l_doc_select_first);
        lua_setfield(L, -2, "selectFirst");
        lua_pushcfunction(L, l_doc_select_first);
        lua_setfield(L, -2, "select_first");

        lua_pushcfunction(L, l_doc_title);
        lua_setfield(L, -2, "title");

        lua_pushcfunction(L, l_doc_body);
        lua_setfield(L, -2, "body");

        lua_pushcfunction(L, l_doc_head);
        lua_setfield(L, -2, "head");
    }
    lua_pop(L, 1);

    /* 2. Element Metatable */
    if (luaL_newmetatable(L, NOVELA_ELEMENT_MT)) {
        lua_pushcfunction(L, l_elem_index);
        lua_setfield(L, -2, "__index");

        lua_pushcfunction(L, l_elem_gc);
        lua_setfield(L, -2, "__gc");

        lua_pushcfunction(L, l_elem_outer_html);
        lua_setfield(L, -2, "__tostring");

        lua_pushcfunction(L, l_elem_select);
        lua_setfield(L, -2, "select");

        lua_pushcfunction(L, l_elem_select_first);
        lua_setfield(L, -2, "selectFirst");
        lua_pushcfunction(L, l_elem_select_first);
        lua_setfield(L, -2, "select_first");

        lua_pushcfunction(L, l_elem_text);
        lua_setfield(L, -2, "text");

        lua_pushcfunction(L, l_elem_own_text);
        lua_setfield(L, -2, "ownText");
        lua_pushcfunction(L, l_elem_own_text);
        lua_setfield(L, -2, "own_text");

        lua_pushcfunction(L, l_elem_html);
        lua_setfield(L, -2, "html");

        lua_pushcfunction(L, l_elem_outer_html);
        lua_setfield(L, -2, "outerHtml");
        lua_pushcfunction(L, l_elem_outer_html);
        lua_setfield(L, -2, "outer_html");

        lua_pushcfunction(L, l_elem_attr);
        lua_setfield(L, -2, "attr");

        lua_pushcfunction(L, l_elem_has_attr);
        lua_setfield(L, -2, "hasAttr");
        lua_pushcfunction(L, l_elem_has_attr);
        lua_setfield(L, -2, "has_attr");

        lua_pushcfunction(L, l_elem_has_class);
        lua_setfield(L, -2, "hasClass");
        lua_pushcfunction(L, l_elem_has_class);
        lua_setfield(L, -2, "has_class");

        lua_pushcfunction(L, l_elem_tag_name);
        lua_setfield(L, -2, "tagName");
        lua_pushcfunction(L, l_elem_tag_name);
        lua_setfield(L, -2, "tag_name");

        lua_pushcfunction(L, l_elem_parent);
        lua_setfield(L, -2, "parent");

        lua_pushcfunction(L, l_elem_children);
        lua_setfield(L, -2, "children");

        lua_pushcfunction(L, l_elem_next_sibling);
        lua_setfield(L, -2, "nextElementSibling");
        lua_pushcfunction(L, l_elem_next_sibling);
        lua_setfield(L, -2, "next_sibling");

        lua_pushcfunction(L, l_elem_prev_sibling);
        lua_setfield(L, -2, "previousElementSibling");
        lua_pushcfunction(L, l_elem_prev_sibling);
        lua_setfield(L, -2, "prev_sibling");

        lua_pushcfunction(L, l_elem_remove);
        lua_setfield(L, -2, "remove");
    }
    lua_pop(L, 1);

    /* 3. Elements List Metatable */
    if (luaL_newmetatable(L, NOVELA_ELEMENTS_MT)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");

        lua_pushcfunction(L, l_elems_size);
        lua_setfield(L, -2, "size");
        lua_pushcfunction(L, l_elems_size);
        lua_setfield(L, -2, "length");

        lua_pushcfunction(L, l_elems_is_empty);
        lua_setfield(L, -2, "isEmpty");
        lua_pushcfunction(L, l_elems_is_empty);
        lua_setfield(L, -2, "is_empty");

        lua_pushcfunction(L, l_elems_first);
        lua_setfield(L, -2, "first");

        lua_pushcfunction(L, l_elems_last);
        lua_setfield(L, -2, "last");

        lua_pushcfunction(L, l_elems_get);
        lua_setfield(L, -2, "get");

        lua_pushcfunction(L, l_elems_attr);
        lua_setfield(L, -2, "attr");

        lua_pushcfunction(L, l_elems_text);
        lua_setfield(L, -2, "text");

        lua_pushcfunction(L, l_elems_for_each);
        lua_setfield(L, -2, "forEach");
        lua_pushcfunction(L, l_elems_for_each);
        lua_setfield(L, -2, "each");
    }
    lua_pop(L, 1);

    /* Global HTML functions */
    lua_pushcfunction(L, l_html_parse);
    lua_setglobal(L, "html_parse");

    lua_pushcfunction(L, l_global_html_select);
    lua_setglobal(L, "html_select");

    lua_pushcfunction(L, l_global_html_select_first);
    lua_setglobal(L, "html_select_first");

    lua_pushcfunction(L, l_global_html_attr);
    lua_setglobal(L, "html_attr");

    lua_pushcfunction(L, l_global_html_text);
    lua_setglobal(L, "html_text");

    lua_pushcfunction(L, l_global_html_remove);
    lua_setglobal(L, "html_remove");

    return 0;
}
