/*
 * pe_tree_outline.c - the collapsible outline. See pe_tree_outline.h.
 *
 * The build is one depth-first walk that appends a row per visible node and
 * stops descending where a node is folded or already shown. Chip state is
 * carried down the walk rather than recomputed per row: a node's pot depends
 * on the path taken to reach it, so it is a property of the row, not of the
 * node.
 */

#include "pe_tree_outline.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/solver/domain/finite_double.h"

pe_tree_outline_config_t pe_tree_outline_default_config(int player_count)
{
    pe_tree_outline_config_t config;
    config.starting_stack = 100.0;
    config.starting_pot = 1.5;
    config.player_count = player_count > 0 ? player_count : 2;
    if (config.player_count > PE_TREE_OUTLINE_MAX_SEATS)
        config.player_count = PE_TREE_OUTLINE_MAX_SEATS;
    return config;
}

void pe_tree_outline_init(pe_tree_outline_t *outline,
                          const pe_tree_outline_config_t *config)
{
    if (outline == NULL)
        return;
    memset(outline, 0, sizeof(*outline));
    outline->config = config != NULL ? *config
                                     : pe_tree_outline_default_config(2);
    if (!pe_finite_double(outline->config.starting_stack) ||
        outline->config.starting_stack < 0.0)
        outline->config.starting_stack = 100.0;
    if (!pe_finite_double(outline->config.starting_pot) ||
        outline->config.starting_pot < 0.0)
        outline->config.starting_pot = 1.5;
    if (outline->config.player_count < 1)
        outline->config.player_count = 1;
    if (outline->config.player_count > PE_TREE_OUTLINE_MAX_SEATS)
        outline->config.player_count = PE_TREE_OUTLINE_MAX_SEATS;
    memset(outline->expanded, 1, sizeof(outline->expanded));
}

/* ------------------------------------------------------------------ *
 * Chips
 * ------------------------------------------------------------------ */

static double chip_max_committed(const pe_tree_chip_state_t *chips, int seats)
{
    double best = 0.0;
    int seat;
    for (seat = 0; seat < seats; ++seat)
        if (chips->committed[seat] > best)
            best = chips->committed[seat];
    return best;
}

static void chip_refresh(pe_tree_chip_state_t *chips,
                         const pe_tree_outline_config_t *config,
                         int acting_player)
{
    int seats = config->player_count;
    double committed_total = 0.0;
    double effective = -1.0;
    int seat;

    for (seat = 0; seat < seats; ++seat)
    {
        committed_total += chips->committed[seat];
        if (chips->stacks[seat] <= 0.0)
            chips->all_in = 1;
    }
    chips->pot = config->starting_pot + committed_total;
    if (acting_player >= 0 && acting_player < seats)
        chips->to_call = chip_max_committed(chips, seats) -
                         chips->committed[acting_player];
    else
        chips->to_call = 0.0;
    if (chips->to_call < 0.0)
        chips->to_call = 0.0;
    /* Effective stack is the smallest one still behind: that is what can
       actually be won or lost from here, and so what SPR must be built on. */
    for (seat = 0; seat < seats; ++seat)
        if (effective < 0.0 || chips->stacks[seat] < effective)
            effective = chips->stacks[seat];
    if (effective < 0.0)
        effective = 0.0;
    chips->spr = chips->pot > 0.0 ? effective / chips->pot : 0.0;
}

static void chip_start(pe_tree_chip_state_t *chips,
                       const pe_tree_outline_config_t *config,
                       int acting_player)
{
    int seat;
    memset(chips, 0, sizeof(*chips));
    for (seat = 0; seat < config->player_count; ++seat)
        chips->stacks[seat] = config->starting_stack;
    chip_refresh(chips, config, acting_player);
}

/* Put `amount` in for `seat`, never more than the seat has left. */
static void chip_commit(pe_tree_chip_state_t *chips, int seat, double amount)
{
    if (seat < 0 || seat >= PE_TREE_OUTLINE_MAX_SEATS || amount <= 0.0)
        return;
    if (amount > chips->stacks[seat])
        amount = chips->stacks[seat];
    chips->stacks[seat] -= amount;
    chips->committed[seat] += amount;
}

/*
 * Apply one action, from the acting player's seat.
 *
 * The raise convention is the one stated in the header: call first, then
 * `size` times the pot as it stands after that call.
 */
static void chip_apply(pe_tree_chip_state_t *chips,
                       const pe_tree_outline_config_t *config,
                       int seat, mpf_tree_action_type_t type, double size,
                       int next_actor)
{
    double to_call;

    if (!pe_finite_double(size) || size < 0.0)
        size = 0.0;

    if (seat >= 0 && seat < config->player_count)
    {
        to_call = chip_max_committed(chips, config->player_count) -
                  chips->committed[seat];
        if (to_call < 0.0)
            to_call = 0.0;
        switch (type)
        {
        case MPF_TREE_ACTION_CALL:
            chip_commit(chips, seat, to_call);
            break;
        case MPF_TREE_ACTION_RAISE:
        {
            double pot_after_call = config->starting_pot + to_call;
            int other;
            for (other = 0; other < config->player_count; ++other)
                pot_after_call += chips->committed[other];
            chip_commit(chips, seat, to_call + size * pot_after_call);
            break;
        }
        case MPF_TREE_ACTION_FOLD:
        case MPF_TREE_ACTION_CHANCE:
        case MPF_TREE_ACTION_TERMINAL:
        default:
            break;
        }
    }
    chip_refresh(chips, config, next_actor);
}

/* ------------------------------------------------------------------ *
 * Labels
 * ------------------------------------------------------------------ */

static void action_label(const pe_tree_editor_node_t *node, int action,
                         char *label, size_t label_size,
                         char *key, size_t key_size)
{
    const pe_tree_editor_action_t *edge = &node->actions[action];
    double size = 0.0;

    if (edge->type == MPF_TREE_ACTION_RAISE && edge->size_index >= 0 &&
        edge->size_index < node->bet_size_count)
        size = node->bet_sizes[edge->size_index];
    switch (edge->type)
    {
    case MPF_TREE_ACTION_FOLD:
        snprintf(label, label_size, "Fold");
        snprintf(key, key_size, "f");
        break;
    case MPF_TREE_ACTION_CALL:
        snprintf(label, label_size, "Call / Check");
        snprintf(key, key_size, "c");
        break;
    case MPF_TREE_ACTION_RAISE:
        snprintf(label, label_size, "Raise %.4gx pot", size);
        /* The key has to survive round-tripping and comparison, so it uses a
           fixed precision rather than %g's variable one. */
        snprintf(key, key_size, "r%.4f", size);
        break;
    case MPF_TREE_ACTION_CHANCE:
        snprintf(label, label_size, "Chance");
        snprintf(key, key_size, "n");
        break;
    case MPF_TREE_ACTION_TERMINAL:
        snprintf(label, label_size, "Terminal");
        snprintf(key, key_size, "t");
        break;
    default:
        snprintf(label, label_size, "?");
        snprintf(key, key_size, "?");
        break;
    }
}

/* ------------------------------------------------------------------ *
 * Build
 * ------------------------------------------------------------------ */

typedef struct
{
    pe_tree_outline_t *outline;
    const pe_tree_editor_t *editor;
    unsigned char seen[PE_TREE_EDITOR_MAX_NODES];
} pe_outline_walk_t;

static void outline_walk(pe_outline_walk_t *walk, int node_index,
                         int parent_row, int depth, const char *path,
                         const char *label,
                         const pe_tree_chip_state_t *chips)
{
    pe_tree_outline_t *outline = walk->outline;
    const pe_tree_editor_node_t *node;
    pe_tree_outline_row_t *row;
    int row_index;
    int action;
    int revisit;

    if (node_index < 0 || node_index >= walk->editor->node_count)
        return;
    if (outline->row_count >= PE_TREE_OUTLINE_MAX_ROWS)
    {
        outline->truncated = 1;
        return;
    }
    node = &walk->editor->nodes[node_index];
    revisit = walk->seen[node_index];
    walk->seen[node_index] = 1;

    row_index = outline->row_count++;
    row = &outline->rows[row_index];
    memset(row, 0, sizeof(*row));
    row->node_index = node_index;
    row->parent_row = parent_row;
    row->depth = depth;
    row->child_count = node->action_count;
    row->revisit = revisit;
    row->type = node->type;
    row->street = node->street;
    row->acting_player = node->type == MPF_TREE_NODE_PLAYER
        ? node->acting_player : -1;
    row->chips = *chips;
    snprintf(row->label, sizeof(row->label), "%s", label != NULL ? label : "");
    snprintf(row->path_key, sizeof(row->path_key), "%s",
             path != NULL ? path : "");
    /* A revisited node is a reference, not a second copy of the subtree:
       expanding it would either duplicate everything below or, on a cycle,
       never end. */
    row->expanded = revisit ? 0
        : (node->action_count > 0 && outline->expanded[node_index] != 0);
    if (!row->expanded)
        return;

    for (action = 0; action < node->action_count; ++action)
    {
        char child_label[PE_TREE_OUTLINE_LABEL_MAX];
        char action_key[32];
        char child_path[PE_TREE_OUTLINE_PATH_MAX];
        pe_tree_chip_state_t child_chips = *chips;
        const pe_tree_editor_node_t *child;
        int next = node->actions[action].next_index;
        int next_actor;

        if (next < 0 || next >= walk->editor->node_count)
            continue;
        action_label(node, action, child_label, sizeof(child_label),
                     action_key, sizeof(action_key));
        if (path == NULL || path[0] == '\0')
            snprintf(child_path, sizeof(child_path), "%s", action_key);
        else
            snprintf(child_path, sizeof(child_path), "%s:%s", path,
                     action_key);
        child = &walk->editor->nodes[next];
        next_actor = child->type == MPF_TREE_NODE_PLAYER
            ? child->acting_player : -1;
        {
            const pe_tree_editor_action_t *edge = &node->actions[action];
            double size = 0.0;
            if (edge->type == MPF_TREE_ACTION_RAISE && edge->size_index >= 0 &&
                edge->size_index < node->bet_size_count)
                size = node->bet_sizes[edge->size_index];
            chip_apply(&child_chips, &outline->config, row->acting_player,
                       edge->type, size, next_actor);
        }
        outline_walk(walk, next, row_index, depth + 1, child_path,
                     child_label, &child_chips);
    }
}

int pe_tree_outline_build(pe_tree_outline_t *outline,
                          const pe_tree_editor_t *editor)
{
    pe_outline_walk_t walk;
    pe_tree_chip_state_t chips;
    const pe_tree_editor_node_t *root;

    if (outline == NULL || editor == NULL)
        return 0;
    outline->row_count = 0;
    outline->truncated = 0;
    if (editor->node_count <= 0 || editor->root_index < 0 ||
        editor->root_index >= editor->node_count)
        return 0;
    memset(&walk, 0, sizeof(walk));
    walk.outline = outline;
    walk.editor = editor;
    root = &editor->nodes[editor->root_index];
    chip_start(&chips, &outline->config,
               root->type == MPF_TREE_NODE_PLAYER ? root->acting_player : -1);
    outline_walk(&walk, editor->root_index, -1, 0, "", "", &chips);
    return outline->row_count;
}

/* ------------------------------------------------------------------ *
 * Fold state
 * ------------------------------------------------------------------ */

int pe_tree_outline_set_expanded(pe_tree_outline_t *outline, int row,
                                 int expanded)
{
    int node_index;

    if (outline == NULL || row < 0 || row >= outline->row_count)
        return 0;
    if (outline->rows[row].child_count <= 0 || outline->rows[row].revisit)
        return 0;
    node_index = outline->rows[row].node_index;
    if (node_index < 0 || node_index >= PE_TREE_EDITOR_MAX_NODES)
        return 0;
    outline->expanded[node_index] = expanded ? 1u : 0u;
    outline->rows[row].expanded = expanded ? 1 : 0;
    return 1;
}

int pe_tree_outline_toggle(pe_tree_outline_t *outline, int row)
{
    if (outline == NULL || row < 0 || row >= outline->row_count)
        return 0;
    return pe_tree_outline_set_expanded(outline, row,
                                        !outline->rows[row].expanded);
}

void pe_tree_outline_expand_all(pe_tree_outline_t *outline)
{
    if (outline == NULL)
        return;
    memset(outline->expanded, 1, sizeof(outline->expanded));
}

void pe_tree_outline_collapse_all(pe_tree_outline_t *outline,
                                  const pe_tree_editor_t *editor)
{
    if (outline == NULL)
        return;
    memset(outline->expanded, 0, sizeof(outline->expanded));
    /* The root stays open: an outline collapsed to nothing shows no tree at
       all, which reads as a bug rather than as a fold. */
    if (editor != NULL && editor->root_index >= 0 &&
        editor->root_index < PE_TREE_EDITOR_MAX_NODES)
        outline->expanded[editor->root_index] = 1u;
}

/* ------------------------------------------------------------------ *
 * Lookup
 * ------------------------------------------------------------------ */

int pe_tree_outline_row_of_node(const pe_tree_outline_t *outline,
                                int node_index)
{
    int row;
    if (outline == NULL)
        return -1;
    for (row = 0; row < outline->row_count; ++row)
        if (outline->rows[row].node_index == node_index)
            return row;
    return -1;
}

int pe_tree_outline_row_of_path(const pe_tree_outline_t *outline,
                                const char *path_key)
{
    int row;
    if (outline == NULL || path_key == NULL)
        return -1;
    for (row = 0; row < outline->row_count; ++row)
        if (strcmp(outline->rows[row].path_key, path_key) == 0)
            return row;
    return -1;
}

/*
 * Open the ancestors of a node.
 *
 * The ancestors are not known without walking, and the walk only descends
 * through open nodes, so this expands everything, finds the row, then closes
 * back down to just the path. Cheaper schemes exist; none of them survive a
 * tree that is a DAG, where "the" parent is not unique.
 */
int pe_tree_outline_reveal(pe_tree_outline_t *outline,
                           const pe_tree_editor_t *editor, int node_index)
{
    unsigned char wanted[PE_TREE_EDITOR_MAX_NODES];
    int row;
    int cursor;

    if (outline == NULL || editor == NULL)
        return -1;
    pe_tree_outline_expand_all(outline);
    if (pe_tree_outline_build(outline, editor) <= 0)
        return -1;
    row = pe_tree_outline_row_of_node(outline, node_index);
    if (row < 0)
        return -1;

    memset(wanted, 0, sizeof(wanted));
    for (cursor = row; cursor >= 0; cursor = outline->rows[cursor].parent_row)
    {
        int index = outline->rows[cursor].node_index;
        if (index >= 0 && index < PE_TREE_EDITOR_MAX_NODES)
            wanted[index] = 1u;
        if (outline->rows[cursor].parent_row == cursor)
            break;
    }
    for (size_t i = 0u; i < sizeof(outline->expanded); ++i)
        outline->expanded[i] = wanted[i];
    if (pe_tree_outline_build(outline, editor) <= 0)
        return -1;
    return pe_tree_outline_row_of_node(outline, node_index);
}

/* ------------------------------------------------------------------ *
 * Text
 * ------------------------------------------------------------------ */

int pe_tree_outline_render(const pe_tree_outline_t *outline, char *buffer,
                           size_t capacity)
{
    size_t used = 0u;
    int row;

    if (buffer == NULL || capacity == 0u)
        return 0;
    buffer[0] = '\0';
    if (outline == NULL)
        return 0;
    for (row = 0; row < outline->row_count; ++row)
    {
        const pe_tree_outline_row_t *entry = &outline->rows[row];
        const char *marker;
        char indent[64];
        int pad = entry->depth * 2;
        int written;

        if (pad > (int)sizeof(indent) - 1)
            pad = (int)sizeof(indent) - 1;
        memset(indent, ' ', (size_t)pad);
        indent[pad] = '\0';
        marker = entry->revisit ? "->"
               : entry->child_count == 0 ? "  "
               : entry->expanded ? "[-]" : "[+]";
        if (used >= capacity)
            break;
        {
            char actor[16];
            if (entry->acting_player >= 0)
                snprintf(actor, sizeof(actor), "  P%d",
                         entry->acting_player + 1);
            else
                actor[0] = '\0';
            written = snprintf(buffer + used, capacity - used,
                               "%s%s %s%s  pot %.2f  spr %.2f%s\n",
                               indent, marker,
                               entry->label[0] != '\0' ? entry->label
                                                       : "(root)",
                               actor, entry->chips.pot, entry->chips.spr,
                               entry->revisit ? "  (already shown)" : "");
        }
        if (written < 0)
            break;
        used += (size_t)written;
        if (used >= capacity)
        {
            used = capacity - 1u;
            break;
        }
    }
    buffer[capacity - 1u] = '\0';
    return (int)used;
}
