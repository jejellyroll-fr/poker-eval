#ifndef PE_TREE_LAYOUT_H
#define PE_TREE_LAYOUT_H

/*
 * Bounded BFS layout over a parsed mpf tree, shared by the desktop trainer
 * tree view and its unit tests. The full tree can hold thousands of nodes;
 * the view renders only the first `max_per_row` nodes of each depth up to
 * `max_depth`, which keeps the layout total below max_per_row*(max_depth+1).
 * Cycles are handled with a visited mark, so malformed or self-referencing
 * trees terminate.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "pe_tree_json.h"

#define PE_TREE_LAYOUT_MAX_NODES 65536u

typedef struct {
    int node_index;         /* index into pe_tree_json_t.nodes */
    int depth;              /* 0 for the root */
    int slot;               /* position within its depth row */
    int row_width;          /* number of rendered nodes at this depth */
    int action_from_parent; /* action index taken to reach this node, -1 root */
    int parent_entry;       /* layout entry index of the parent, -1 root */
} pe_tree_layout_entry_t;

typedef struct {
    const pe_tree_layout_entry_t *entries;
    size_t count;
    int depth_count; /* max rendered depth + 1 */
    int truncated;   /* 1 when nodes were dropped by the caps */
} pe_tree_layout_t;

/* Find a node by id; NULL when absent. */
static const pe_tree_json_node_t *pe_tree_json_find_node(
    const pe_tree_json_t *tree, const char *id)
{
    size_t i;
    if (!tree || !id || !id[0])
        return NULL;
    for (i = 0u; i < tree->count; ++i)
        if (strcmp(tree->nodes[i].id, id) == 0)
            return &tree->nodes[i];
    return NULL;
}

/*
 * Compute the layout. `entries` must hold at least
 * (max_depth + 1) * max_per_row elements. Returns 0 on success, -1 on
 * invalid arguments or oversized trees.
 */
static int pe_tree_layout_bfs(const pe_tree_json_t *tree, int max_depth,
                              int max_per_row, pe_tree_layout_entry_t *entries,
                              size_t entry_capacity, pe_tree_layout_t *out)
{
    unsigned char *visited = NULL;
    size_t queue_start = 0u;
    size_t queue_end = 0u;
    int depth;
    const pe_tree_json_node_t *root;
    int root_index;

    if (!tree || !entries || !out || max_depth < 0 || max_per_row <= 0 ||
        tree->count == 0u || tree->count > PE_TREE_LAYOUT_MAX_NODES)
        return -1;
    if (entry_capacity < (size_t)(max_depth + 1) * (size_t)max_per_row)
        return -1;
    root = pe_tree_json_find_node(tree, tree->root_id);
    if (!root)
        root = &tree->nodes[0];
    root_index = (int)(root - tree->nodes);

    visited = (unsigned char *)calloc(tree->count, sizeof(*visited));
    if (!visited)
        return -1;

    memset(out, 0, sizeof(*out));
    out->entries = entries;

    entries[0].node_index = root_index;
    entries[0].depth = 0;
    entries[0].slot = 0;
    entries[0].row_width = 1;
    entries[0].action_from_parent = -1;
    entries[0].parent_entry = -1;
    visited[root_index] = 1u;
    queue_end = 1u;
    out->count = 1u;

    for (depth = 0; depth < max_depth; ++depth)
    {
        size_t level_start = queue_start;
        size_t level_end = queue_end;
        size_t row_slots = 0u;
        size_t row_first = queue_end;
        size_t i;
        if (level_start == level_end)
            break;
        for (i = level_start; i < level_end && row_slots < (size_t)max_per_row; ++i)
        {
            const pe_tree_layout_entry_t *parent = &entries[i];
            const pe_tree_json_node_t *node = &tree->nodes[parent->node_index];
            int action;
            for (action = 0; action < node->action_count &&
                            row_slots < (size_t)max_per_row;
                 ++action)
            {
                const pe_tree_json_node_t *child =
                    pe_tree_json_find_node(tree, node->action_next[action]);
                int child_index;
                if (!child)
                    continue;
                child_index = (int)(child - tree->nodes);
                if (visited[child_index])
                    continue;
                visited[child_index] = 1u;
                if (queue_end >= entry_capacity)
                    break;
                entries[queue_end].node_index = child_index;
                entries[queue_end].depth = depth + 1;
                entries[queue_end].slot = (int)row_slots;
                entries[queue_end].row_width = 0; /* patched below */
                entries[queue_end].action_from_parent = action;
                entries[queue_end].parent_entry = (int)i;
                ++queue_end;
                ++row_slots;
            }
        }
        /* Record the final row width on every entry of the new row. */
        for (i = row_first; i < queue_end; ++i)
            entries[i].row_width = (int)row_slots;
        if (row_slots == 0u)
            break;
        out->count = queue_end;
        queue_start = level_end;
    }

    /* Truncation: some node (or action target) was not rendered. */
    if (out->count < tree->count)
        out->truncated = 1;
    {
        int max_seen_depth = 0;
        size_t i;
        for (i = 0u; i < out->count; ++i)
            if (entries[i].depth > max_seen_depth)
                max_seen_depth = entries[i].depth;
        out->depth_count = max_seen_depth + 1;
    }
    free(visited);
    return 0;
}

#endif /* PE_TREE_LAYOUT_H */
