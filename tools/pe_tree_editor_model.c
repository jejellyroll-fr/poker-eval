#include "pe_tree_editor_model.h"
#include <poker_eval/core/safe_format.h>

#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
} pe_tree_editor_buffer_t;

static int buffer_reserve(pe_tree_editor_buffer_t *buffer, size_t extra)
{
    size_t required;
    size_t capacity;
    char *data;
    if (!buffer || extra > SIZE_MAX - buffer->length - 1u)
        return 0;
    required = buffer->length + extra + 1u;
    if (required <= buffer->capacity)
        return 1;
    capacity = buffer->capacity ? buffer->capacity : 512u;
    while (capacity < required)
    {
        if (capacity > SIZE_MAX / 2u)
            return 0;
        capacity *= 2u;
    }
    data = (char *)realloc(buffer->data, capacity);
    if (!data)
        return 0;
    buffer->data = data;
    buffer->capacity = capacity;
    return 1;
}

static int buffer_appendf(pe_tree_editor_buffer_t *buffer, const char *format, ...)
{
    va_list args;
    va_list copy;
    int length;
    if (!buffer || !format)
        return 0;
    va_start(args, format);
    va_copy(copy, args);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    length = (int)pe_safe_vformat(NULL, 0u, format, copy);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    va_end(copy);
    if (length < 0 || !buffer_reserve(buffer, (size_t)length))
    {
        va_end(args);
        return 0;
    }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    (void)pe_safe_vformat(buffer->data + buffer->length,
                          buffer->capacity - buffer->length, format, args);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    va_end(args);
    buffer->length += (size_t)length;
    return 1;
}

static int buffer_append_json_string(pe_tree_editor_buffer_t *buffer,
                                     const char *value)
{
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");
    if (!buffer_appendf(buffer, "\""))
        return 0;
    while (*cursor)
    {
        if (*cursor == '\\' || *cursor == '"')
        {
            if (!buffer_appendf(buffer, "\\%c", *cursor))
                return 0;
        }
        else if (*cursor == '\n')
        {
            if (!buffer_appendf(buffer, "\\n"))
                return 0;
        }
        else if (*cursor == '\r')
        {
            if (!buffer_appendf(buffer, "\\r"))
                return 0;
        }
        else if (*cursor == '\t')
        {
            if (!buffer_appendf(buffer, "\\t"))
                return 0;
        }
        else if (*cursor < 0x20u)
        {
            if (!buffer_appendf(buffer, "\\u%04x", (unsigned int)*cursor))
                return 0;
        }
        else if (!buffer_appendf(buffer, "%c", *cursor))
        {
            return 0;
        }
        ++cursor;
    }
    return buffer_appendf(buffer, "\"");
}

static void clear_node(pe_tree_editor_node_t *node)
{
    if (node)
        memset(node, 0, sizeof(*node));
}

static void init_node(pe_tree_editor_node_t *node, const char *id,
                      mpf_tree_node_type_t type, mpf_street_t street,
                      int acting_player)
{
    clear_node(node);
    snprintf(node->id, sizeof(node->id), "%s", id ? id : "node");
    node->type = type;
    node->street = street;
    node->acting_player = acting_player;
    node->use_pot_sizing = 0;
}

static int append_node(pe_tree_editor_t *editor, mpf_tree_node_type_t type,
                       mpf_street_t street, int acting_player,
                       int *out_index)
{
    pe_tree_editor_node_t *node;
    int index;
    if (!editor || editor->node_count >= PE_TREE_EDITOR_MAX_NODES)
        return 0;
    index = editor->node_count++;
    node = &editor->nodes[index];
    {
        char id[64];
        snprintf(id, sizeof(id), "node_%d", index);
        init_node(node, id, type, street, acting_player);
    }
    if (out_index)
        *out_index = index;
    return 1;
}

static int append_raw_action(pe_tree_editor_node_t *node,
                             mpf_tree_action_type_t type, int size_index,
                             double weight, int next_index)
{
    pe_tree_editor_action_t *action;
    if (!node || node->action_count >= PE_TREE_EDITOR_MAX_ACTIONS)
        return 0;
    action = &node->actions[node->action_count++];
    action->type = type;
    action->size_index = size_index;
    action->weight = weight;
    action->next_index = next_index;
    return 1;
}

static int add_default_terminal_actions(pe_tree_editor_t *editor, int node_index)
{
    pe_tree_editor_node_t *node;
    int child;
    if (!editor || node_index < 0 || node_index >= editor->node_count)
        return 0;
    node = &editor->nodes[node_index];
    if (!append_node(editor, MPF_TREE_NODE_TERMINAL, node->street, -1, &child))
        return 0;
    if (!append_raw_action(node, MPF_TREE_ACTION_FOLD, -1, 1.0, child))
        return 0;
    if (!append_node(editor, MPF_TREE_NODE_TERMINAL, node->street, -1, &child))
        return 0;
    return append_raw_action(node, MPF_TREE_ACTION_CALL, -1, 1.0, child);
}

void pe_tree_editor_init(pe_tree_editor_t *editor, int player_count,
                         mpf_street_t street)
{
    int root;
    if (!editor)
        return;
    memset(editor, 0, sizeof(*editor));
    editor->version = 1;
    editor->player_count = player_count >= 2 && player_count <= MPF_MAX_PLAYERS
        ? player_count : 2;
    editor->root_index = -1;
    if (!append_node(editor, MPF_TREE_NODE_PLAYER, street, 0, &root))
        return;
    editor->root_index = root;
    editor->nodes[root].bet_sizes[0] = 0.5;
    editor->nodes[root].bet_sizes[1] = 1.0;
    editor->nodes[root].bet_sizes[2] = 2.0;
    editor->nodes[root].bet_size_count = 3;
    (void)add_default_terminal_actions(editor, root);
}

int pe_tree_editor_import(pe_tree_editor_t *editor, const mpf_tree_def_t *tree)
{
    int *index_map;
    int player_count = 2;
    if (!editor || !tree || !tree->nodes || tree->node_count <= 0 ||
        tree->node_count > PE_TREE_EDITOR_MAX_NODES)
        return 0;
    memset(editor, 0, sizeof(*editor));
    editor->version = tree->version > 0 ? tree->version : 1;
    editor->root_index = tree->root_index;
    index_map = (int *)malloc((size_t)tree->node_count * sizeof(*index_map));
    if (!index_map)
        return 0;
    for (int i = 0; i < tree->node_count; ++i)
    {
        const mpf_tree_node_t *source = &tree->nodes[i];
        pe_tree_editor_node_t *target = &editor->nodes[i];
        if (source->action_count > PE_TREE_EDITOR_MAX_ACTIONS ||
            source->bet_size_count > PE_TREE_EDITOR_MAX_SIZES)
        {
            free(index_map);
            return 0;
        }
        init_node(target, source->id, source->type, source->street,
                  source->acting_player);
        target->bet_size_count = source->bet_size_count < PE_TREE_EDITOR_MAX_SIZES
            ? source->bet_size_count : PE_TREE_EDITOR_MAX_SIZES;
        for (int size = 0; size < target->bet_size_count; ++size)
            target->bet_sizes[size] = source->bet_sizes[size];
        target->use_pot_sizing = source->use_pot_sizing;
        target->action_count = source->action_count < PE_TREE_EDITOR_MAX_ACTIONS
            ? source->action_count : PE_TREE_EDITOR_MAX_ACTIONS;
        for (int action = 0; action < target->action_count; ++action)
        {
            target->actions[action].type = source->actions[action].type;
            target->actions[action].size_index = source->actions[action].size_index;
            target->actions[action].weight = source->actions[action].weight;
            target->actions[action].next_index = source->actions[action].next_index;
        }
        if (source->acting_player >= 0 && source->acting_player + 1 > player_count)
            player_count = source->acting_player + 1;
        index_map[i] = i;
    }
    editor->node_count = tree->node_count;
    editor->player_count = player_count <= MPF_MAX_PLAYERS ? player_count : MPF_MAX_PLAYERS;
    for (int i = 0; i < editor->node_count; ++i)
    {
        for (int action = 0; action < editor->nodes[i].action_count; ++action)
        {
            int next = editor->nodes[i].actions[action].next_index;
            if (next < 0 && tree->nodes[i].actions[action].next_id)
            {
                for (int candidate = 0; candidate < editor->node_count; ++candidate)
                    if (strcmp(editor->nodes[candidate].id,
                               tree->nodes[i].actions[action].next_id) == 0)
                    {
                        next = index_map[candidate];
                        break;
                    }
            }
            editor->nodes[i].actions[action].next_index = next;
        }
    }
    free(index_map);
    return editor->root_index >= 0 && editor->root_index < editor->node_count;
}

int pe_tree_editor_add_action(pe_tree_editor_t *editor, int node_index,
                              mpf_tree_action_type_t type, double bet_size,
                              int *out_child_index)
{
    pe_tree_editor_node_t *node;
    int original_node_count;
    int original_action_count;
    int original_bet_size_count;
    int child = -1;
    int size_index = -1;
    int child_player;
    if (!editor || node_index < 0 || node_index >= editor->node_count ||
        type < MPF_TREE_ACTION_FOLD || type > MPF_TREE_ACTION_CHANCE)
        return 0;
    node = &editor->nodes[node_index];
    if (node->type != MPF_TREE_NODE_PLAYER ||
        node->action_count >= PE_TREE_EDITOR_MAX_ACTIONS)
        return 0;
    /* A player node should have at most one branch for each action.  The
       default editor tree already contains Fold and Call/Check, so accepting
       another quick-click for either action creates an ambiguous duplicate
       branch instead of extending the tree. */
    for (int i = 0; i < node->action_count; ++i)
    {
        const pe_tree_editor_action_t *existing = &node->actions[i];
        if (existing->type != type)
            continue;
        if (type != MPF_TREE_ACTION_RAISE ||
            (existing->size_index >= 0 &&
             existing->size_index < node->bet_size_count &&
             fabs(node->bet_sizes[existing->size_index] - bet_size) < 1e-12))
            return 0;
    }
    original_node_count = editor->node_count;
    original_action_count = node->action_count;
    original_bet_size_count = node->bet_size_count;
    if (type == MPF_TREE_ACTION_RAISE)
    {
        if (bet_size <= 0.0)
            return 0;
        for (int i = 0; i < node->bet_size_count; ++i)
            if (fabs(node->bet_sizes[i] - bet_size) < 1e-12)
                size_index = i;
        if (size_index < 0)
        {
            if (node->bet_size_count >= PE_TREE_EDITOR_MAX_SIZES)
                return 0;
            size_index = node->bet_size_count++;
            node->bet_sizes[size_index] = bet_size;
        }
        child_player = editor->player_count > 1
            ? (node->acting_player + 1) % editor->player_count : node->acting_player;
        if (!append_node(editor, MPF_TREE_NODE_PLAYER, node->street,
                         child_player, &child) ||
            !add_default_terminal_actions(editor, child))
            goto rollback;
    }
    else if (type == MPF_TREE_ACTION_CHANCE)
    {
        if (!append_node(editor, MPF_TREE_NODE_CHANCE, node->street, -1, &child))
            return 0;
        {
            int next_player;
            int next_node;
            next_player = editor->player_count > 1
                ? (node->acting_player + 1) % editor->player_count : 0;
            if (!append_node(editor, MPF_TREE_NODE_PLAYER, node->street,
                             next_player, &next_node) ||
                !append_raw_action(&editor->nodes[child], MPF_TREE_ACTION_CHANCE,
                                   -1, 1.0, next_node) ||
                !add_default_terminal_actions(editor, next_node))
                goto rollback;
        }
    }
    else if (!append_node(editor, MPF_TREE_NODE_TERMINAL, node->street, -1, &child))
    {
        goto rollback;
    }
    if (!append_raw_action(node, type, size_index, 1.0, child))
        goto rollback;
    if (out_child_index)
        *out_child_index = child;
    return 1;

rollback:
    editor->node_count = original_node_count;
    node->action_count = original_action_count;
    node->bet_size_count = original_bet_size_count;
    return 0;
}

int pe_tree_editor_remove_action(pe_tree_editor_t *editor, int node_index,
                                 int action_index)
{
    pe_tree_editor_node_t *node;
    if (!editor || node_index < 0 || node_index >= editor->node_count)
        return 0;
    node = &editor->nodes[node_index];
    if (node->type != MPF_TREE_NODE_PLAYER ||
        action_index < 0 || action_index >= node->action_count ||
        (node->type == MPF_TREE_NODE_PLAYER && node->action_count <= 1))
        return 0;
    memmove(&node->actions[action_index], &node->actions[action_index + 1],
            (size_t)(node->action_count - action_index - 1) * sizeof(node->actions[0]));
    --node->action_count;
    return 1;
}

static int reachable_walk(const pe_tree_editor_t *editor, int node_index,
                          unsigned char *visited)
{
    if (!editor || !visited || node_index < 0 || node_index >= editor->node_count ||
        visited[node_index])
        return 1;
    visited[node_index] = 1u;
    for (int action = 0; action < editor->nodes[node_index].action_count; ++action)
        if (!reachable_walk(editor, editor->nodes[node_index].actions[action].next_index,
                            visited))
            return 0;
    return 1;
}

int pe_tree_editor_reachable(const pe_tree_editor_t *editor, int node_index)
{
    unsigned char *visited;
    int result;
    if (!editor || node_index < 0 || node_index >= editor->node_count)
        return 0;
    visited = (unsigned char *)calloc((size_t)editor->node_count, sizeof(*visited));
    if (!visited)
        return 0;
    (void)reachable_walk(editor, editor->root_index, visited);
    result = visited[node_index] != 0u;
    free(visited);
    return result;
}

const char *pe_tree_editor_action_name(mpf_tree_action_type_t type)
{
    switch (type)
    {
    case MPF_TREE_ACTION_FOLD: return "Fold";
    case MPF_TREE_ACTION_CALL: return "Call / Check";
    case MPF_TREE_ACTION_RAISE: return "Raise";
    case MPF_TREE_ACTION_CHANCE: return "Chance";
    case MPF_TREE_ACTION_TERMINAL: return "Terminal";
    default: return "Unknown";
    }
}

const char *pe_tree_editor_node_type_name(mpf_tree_node_type_t type)
{
    switch (type)
    {
    case MPF_TREE_NODE_PLAYER: return "Player";
    case MPF_TREE_NODE_CHANCE: return "Chance";
    case MPF_TREE_NODE_TERMINAL: return "Terminal";
    default: return "Unknown";
    }
}

static const char *street_json_name(mpf_street_t street)
{
    switch (street)
    {
    case MPF_STREET_PREFLOP: return "PREFLOP";
    case MPF_STREET_FLOP: return "FLOP";
    case MPF_STREET_TURN: return "TURN";
    case MPF_STREET_RIVER: return "RIVER";
    case MPF_STREET_SHOWDOWN: return "RIVER";
    default: return "RIVER";
    }
}

static const char *action_json_name(mpf_tree_action_type_t type)
{
    switch (type)
    {
    case MPF_TREE_ACTION_FOLD: return "fold";
    case MPF_TREE_ACTION_CALL: return "call";
    case MPF_TREE_ACTION_RAISE: return "raise";
    case MPF_TREE_ACTION_CHANCE: return "chance";
    case MPF_TREE_ACTION_TERMINAL: return "terminal";
    default: return "terminal";
    }
}

static int append_node_json(pe_tree_editor_buffer_t *buffer,
                            const pe_tree_editor_t *editor, int index)
{
    const pe_tree_editor_node_t *node = &editor->nodes[index];
    const char *type = node->type == MPF_TREE_NODE_PLAYER ? "player" :
                       node->type == MPF_TREE_NODE_CHANCE ? "chance" : "terminal";
    if (!buffer_appendf(buffer, "{\"id\":" ) ||
        !buffer_append_json_string(buffer, node->id) ||
        !buffer_appendf(buffer, ",\"type\":\"%s\",\"street\":\"%s\"",
                        type, street_json_name(node->street)))
        return 0;
    if (node->type == MPF_TREE_NODE_PLAYER &&
        !buffer_appendf(buffer, ",\"player\":%d", node->acting_player))
        return 0;
    if (node->bet_size_count > 0 &&
        !buffer_appendf(buffer, ",\"bet_sizes\":["))
        return 0;
    for (int size = 0; size < node->bet_size_count; ++size)
        if (!buffer_appendf(buffer, "%s%.17g", size == 0 ? "" : ",",
                            node->bet_sizes[size]))
            return 0;
    if (node->bet_size_count > 0 && !buffer_appendf(buffer, "]"))
        return 0;
    if (!buffer_appendf(buffer, ",\"pot_sizing\":%s",
                        node->use_pot_sizing ? "true" : "false"))
        return 0;
    if (node->action_count > 0 && !buffer_appendf(buffer, ",\"actions\":["))
        return 0;
    for (int action = 0; action < node->action_count; ++action)
    {
        const pe_tree_editor_action_t *entry = &node->actions[action];
        if (!buffer_appendf(buffer, "%s{\"type\":\"%s\"",
                            action == 0 ? "" : ",", action_json_name(entry->type)))
            return 0;
        if (entry->type == MPF_TREE_ACTION_RAISE &&
            !buffer_appendf(buffer, ",\"size_index\":%d", entry->size_index))
            return 0;
        if (entry->next_index >= 0 && entry->next_index < editor->node_count &&
            (!buffer_appendf(buffer, ",\"next\":") ||
             !buffer_append_json_string(buffer, editor->nodes[entry->next_index].id)))
            return 0;
        if (!buffer_appendf(buffer, "}"))
            return 0;
    }
    if (node->action_count > 0 && !buffer_appendf(buffer, "]"))
        return 0;
    return buffer_appendf(buffer, "}");
}

int pe_tree_editor_serialize_json(const pe_tree_editor_t *editor,
                                  char **out_json, size_t *out_len)
{
    pe_tree_editor_buffer_t buffer = {0, 0, 0};
    if (!editor || !out_json || editor->root_index < 0 ||
        editor->root_index >= editor->node_count)
        return 0;
    *out_json = NULL;
    if (!buffer_appendf(&buffer, "{\"version\":%d,\"root\":",
                        editor->version > 0 ? editor->version : 1) ||
        !buffer_append_json_string(&buffer, editor->nodes[editor->root_index].id) ||
        !buffer_appendf(&buffer, ",\"betProfiles\":[],\"rangeProfiles\":[],\"nodes\":["))
        goto fail;
    for (int node = 0; node < editor->node_count; ++node)
        if (!append_node_json(&buffer, editor, node) ||
            (node + 1 < editor->node_count && !buffer_appendf(&buffer, ",")))
            goto fail;
    if (!buffer_appendf(&buffer, "]}"))
        goto fail;
    *out_json = buffer.data;
    if (out_len)
        *out_len = buffer.length;
    return 1;
fail:
    free(buffer.data);
    return 0;
}

int pe_tree_editor_validate(const pe_tree_editor_t *editor, char *error,
                            size_t error_capacity)
{
    char *json = NULL;
    size_t length = 0;
    mpf_tree_error_t tree_error = {0};
    mpf_tree_def_t *tree;
    int valid;
    if (error && error_capacity > 0)
        error[0] = '\0';
    if (!pe_tree_editor_serialize_json(editor, &json, &length))
        return 0;
    tree = mpf_tree_load_json(json, length, &tree_error);
    free(json);
    valid = tree != NULL && mpf_tree_validate(tree, &tree_error);
    if (!valid && error && error_capacity > 0)
        snprintf(error, error_capacity, "%s",
                 tree_error.message[0] ? tree_error.message : "invalid tree");
    mpf_tree_free(tree);
    return valid;
}

int pe_tree_editor_write_json(const pe_tree_editor_t *editor, const char *path,
                              char *error, size_t error_capacity)
{
    char *json = NULL;
    size_t length = 0;
    FILE *file;
    size_t written;
    if (error && error_capacity > 0)
        error[0] = '\0';
    if (!path || !*path || !pe_tree_editor_serialize_json(editor, &json, &length))
    {
        if (error && error_capacity > 0)
            snprintf(error, error_capacity, "could not serialize tree");
        return 0;
    }
    file = fopen(path, "wb");
    if (!file)
    {
        if (error && error_capacity > 0)
            snprintf(error, error_capacity, "could not open %s", path);
        free(json);
        return 0;
    }
    written = fwrite(json, 1u, length, file);
    if (fclose(file) != 0 || written != length)
    {
        if (error && error_capacity > 0)
            snprintf(error, error_capacity, "could not write %s", path);
        free(json);
        return 0;
    }
    free(json);
    return 1;
}

static int render_walk(const pe_tree_editor_t *editor, int node_index,
                       int depth, unsigned char *visited, char *buffer,
                       size_t capacity, size_t *used)
{
    const pe_tree_editor_node_t *node;
    if (!editor || !visited || !buffer || !used || node_index < 0 ||
        node_index >= editor->node_count || visited[node_index] || depth > editor->node_count)
        return 1;
    visited[node_index] = 1u;
    node = &editor->nodes[node_index];
    if (*used < capacity)
    {
        int written = node->type == MPF_TREE_NODE_PLAYER
            ? snprintf(buffer + *used, capacity - *used,
                       "%*s[%d] %s  %s  P%d\n", depth * 3, "", node_index,
                       node->id, pe_tree_editor_node_type_name(node->type),
                       node->acting_player + 1)
            : snprintf(buffer + *used, capacity - *used,
                       "%*s[%d] %s  %s\n", depth * 3, "", node_index,
                       node->id, pe_tree_editor_node_type_name(node->type));
        if (written > 0)
            *used += (size_t)written < capacity - *used ? (size_t)written : capacity - *used;
    }
    for (int action = 0; action < node->action_count; ++action)
    {
        const pe_tree_editor_action_t *entry = &node->actions[action];
        if (*used < capacity)
        {
            int written = entry->type == MPF_TREE_ACTION_RAISE &&
                          entry->size_index >= 0 &&
                          entry->size_index < node->bet_size_count
                ? snprintf(buffer + *used, capacity - *used, "%*s- %s %.3gx pot -> node %d\n",
                           depth * 3 + 2, "", pe_tree_editor_action_name(entry->type),
                           node->bet_sizes[entry->size_index], entry->next_index)
                : snprintf(buffer + *used, capacity - *used, "%*s- %s -> node %d\n",
                           depth * 3 + 2, "", pe_tree_editor_action_name(entry->type),
                           entry->next_index);
            if (written > 0)
                *used += (size_t)written < capacity - *used ? (size_t)written : capacity - *used;
        }
        (void)render_walk(editor, entry->next_index, depth + 1, visited,
                          buffer, capacity, used);
    }
    return 1;
}

int pe_tree_editor_render(const pe_tree_editor_t *editor, char *buffer,
                          size_t capacity)
{
    unsigned char *visited;
    size_t used = 0;
    if (!editor || !buffer || capacity == 0 || editor->node_count <= 0)
        return 0;
    buffer[0] = '\0';
    visited = (unsigned char *)calloc((size_t)editor->node_count, sizeof(*visited));
    if (!visited)
        return 0;
    (void)render_walk(editor, editor->root_index, 0, visited, buffer, capacity, &used);
    free(visited);
    buffer[capacity - 1u] = '\0';
    return (int)used;
}
