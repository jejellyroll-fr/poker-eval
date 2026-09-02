#ifndef PE_TREE_JSON_H
#define PE_TREE_JSON_H

/*
 * Minimal tolerant parser for mpf tree JSON (the format written by
 * mpf_tree_serialize_json and read by mpf_tree_load_json). Shared by the
 * desktop trainer's tree view and its unit tests. It extracts only what the
 * view needs: node id/type/player and per-action type/next pairs. It is not
 * a general JSON parser.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PE_TREE_JSON_MAX_ACTIONS 16
#define PE_TREE_JSON_MAX_KEY_LENGTH 128

typedef struct {
    char id[64];
    char type[16]; /* "player" | "chance" | "terminal" */
    int player;    /* acting player, -1 when the node has none */
    int action_count;
    char action_type[PE_TREE_JSON_MAX_ACTIONS][24];
    char action_next[PE_TREE_JSON_MAX_ACTIONS][64];
} pe_tree_json_node_t;

typedef struct {
    pe_tree_json_node_t *nodes;
    size_t count;
    char root_id[64];
} pe_tree_json_t;

/* Find key (including its quotes and colon) inside [begin, end). */
static const char *pe_tree_json_find(const char *begin, const char *end,
                                     const char *key)
{
    size_t key_length = strnlen(key, PE_TREE_JSON_MAX_KEY_LENGTH);
    const char *p;
    if (begin == NULL || end <= begin || key_length == 0u)
        return NULL;
    for (p = begin; p + key_length <= end; ++p)
        if (memcmp(p, key, key_length) == 0)
            return p + key_length;
    return NULL;
}

static int pe_tree_json_extract_string(const char *begin, const char *end,
                                       const char *key, char *out,
                                       size_t out_size)
{
    const char *at = pe_tree_json_find(begin, end, key);
    size_t used = 0u;
    if (out == NULL || out_size == 0u)
        return -1;
    out[0] = '\0';
    if (at == NULL)
        return -1;
    while (at < end && (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r'))
        ++at;
    if (at >= end || *at != '"')
        return -1;
    ++at;
    while (at < end && *at != '"') {
        char character = *at++;
        if (character == '\\' && at < end)
            character = *at++;
        if (used + 1u < out_size)
            out[used++] = character;
    }
    out[used] = '\0';
    return (at < end) ? 0 : -1;
}

static int pe_tree_json_extract_int(const char *begin, const char *end,
                                    const char *key, int *out)
{
    const char *at = pe_tree_json_find(begin, end, key);
    char *parsed_end = NULL;
    long value;
    if (at == NULL || out == NULL)
        return -1;
    while (at < end && (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r'))
        ++at;
    if (at >= end)
        return -1;
    value = strtol(at, &parsed_end, 10);
    if (parsed_end == at)
        return -1;
    *out = (int)value;
    return 0;
}

/*
 * Advance the cursor across one object inside [array_begin, end) starting at
 * the opening brace; returns the end of the object (exclusive) or NULL.
 * Braces inside JSON strings are skipped.
 */
static const char *pe_tree_json_object_end(const char *object_begin,
                                           const char *end)
{
    const char *cursor = object_begin;
    int depth = 0;
    int in_string = 0;
    if (cursor == NULL || cursor >= end || *cursor != '{')
        return NULL;
    while (cursor < end) {
        char character = *cursor;
        if (in_string) {
            if (character == '\\')
                ++cursor; /* skip escaped character */
            else if (character == '"')
                in_string = 0;
        } else if (character == '"') {
            in_string = 1;
        } else if (character == '{') {
            ++depth;
        } else if (character == '}') {
            --depth;
            if (depth == 0)
                return cursor + 1;
        }
        ++cursor;
    }
    return NULL;
}

static void pe_tree_json_parse_actions(const char *begin, const char *end,
                                       pe_tree_json_node_t *node)
{
    const char *at = pe_tree_json_find(begin, end, "\"actions\":");
    const char *cursor;
    node->action_count = 0;
    if (at == NULL)
        return;
    while (at < end && *at != '[')
        ++at;
    if (at >= end)
        return;
    cursor = at + 1;
    while (cursor < end && *cursor != ']' &&
           node->action_count < PE_TREE_JSON_MAX_ACTIONS) {
        const char *object;
        const char *object_end;
        while (cursor < end && *cursor != '{' && *cursor != ']')
            ++cursor;
        if (cursor >= end || *cursor != '{')
            break;
        object = cursor;
        object_end = pe_tree_json_object_end(object, end);
        if (object_end == NULL)
            break;
        pe_tree_json_extract_string(object, object_end, "\"type\":",
                                    node->action_type[node->action_count],
                                    sizeof(node->action_type[0]));
        pe_tree_json_extract_string(object, object_end, "\"next\":",
                                    node->action_next[node->action_count],
                                    sizeof(node->action_next[0]));
        ++node->action_count;
        cursor = object_end;
    }
}

/*
 * Parse a whole tree document. Nodes beyond max_nodes are dropped, keeping
 * the view bounded on huge trees. Returns 0 when at least the node array was
 * found.
 */
static int pe_tree_json_parse(const char *text, size_t length,
                              pe_tree_json_t *out, size_t max_nodes)
{
    const char *end;
    const char *nodes_at;
    const char *cursor;
    if (text == NULL || out == NULL || max_nodes == 0u)
        return -1;
    memset(out, 0, sizeof(*out));
    end = text + length;
    pe_tree_json_extract_string(text, end, "\"root\":", out->root_id,
                                sizeof(out->root_id));
    nodes_at = pe_tree_json_find(text, end, "\"nodes\":");
    if (nodes_at == NULL)
        return -1;
    while (nodes_at < end && *nodes_at != '[')
        ++nodes_at;
    if (nodes_at >= end)
        return -1;
    out->nodes = (pe_tree_json_node_t *)calloc(max_nodes, sizeof(*out->nodes));
    if (out->nodes == NULL)
        return -1;
    cursor = nodes_at + 1;
    while (cursor < end && *cursor != ']' && out->count < max_nodes) {
        const char *object_end;
        pe_tree_json_node_t *node;
        while (cursor < end && *cursor != '{' && *cursor != ']')
            ++cursor;
        if (cursor >= end || *cursor != '{')
            break;
        object_end = pe_tree_json_object_end(cursor, end);
        if (object_end == NULL)
            break;
        node = &out->nodes[out->count];
        node->player = -1;
        pe_tree_json_extract_string(cursor, object_end, "\"id\":", node->id,
                                    sizeof(node->id));
        pe_tree_json_extract_string(cursor, object_end, "\"type\":",
                                    node->type, sizeof(node->type));
        pe_tree_json_extract_int(cursor, object_end, "\"player\":",
                                 &node->player);
        pe_tree_json_parse_actions(cursor, object_end, node);
        ++out->count;
        cursor = object_end;
    }
    return 0;
}

static void pe_tree_json_free(pe_tree_json_t *tree)
{
    if (tree == NULL)
        return;
    free(tree->nodes);
    tree->nodes = NULL;
    tree->count = 0u;
    tree->root_id[0] = '\0';
}

#endif /* PE_TREE_JSON_H */
