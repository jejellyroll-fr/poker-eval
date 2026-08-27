/*
 * pe_tree_outline.h - a collapsible, indented view over a tree being edited.
 *
 * The canvas shows a betting tree as a map, which stops being readable a few
 * streets in. What a tree is actually navigated with in a desktop editor is
 * an indented outline that folds: you
 * open the line you are working on and leave the rest closed.
 *
 * This file is that outline, with no GUI in it. It turns a pe_tree_editor_t
 * into a flat array of visible rows, and it carries the two things a node
 * needs to be worked on:
 *
 *   - a path key, the canonical action line that reaches the node. This is
 *     the node's stable identity: an index moves when the tree is edited, a
 *     path does not, so it is what a selection should be remembered by.
 *
 *   - the chip context at that point -- pot, stacks, what each player has
 *     committed, and the resulting SPR. The editor model stores topology
 *     only; these are derived here by walking the path from the root.
 *
 * A tree may be a DAG once imported: two lines can arrive at the same node.
 * A revisited node is shown where it appears and marked, never re-expanded,
 * so an outline is always finite.
 */

#ifndef PE_TREE_OUTLINE_H
#define PE_TREE_OUTLINE_H

#include "pe_tree_editor_model.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A row per visible node. A fully expanded tree cannot exceed the node count
   plus the revisit rows, which are bounded by the total number of actions. */
#define PE_TREE_OUTLINE_MAX_ROWS   (PE_TREE_EDITOR_MAX_NODES * 2)
#define PE_TREE_OUTLINE_PATH_MAX   256
#define PE_TREE_OUTLINE_LABEL_MAX  96
#define PE_TREE_OUTLINE_MAX_SEATS  10

/*
 * Chip state at a node, derived by replaying the path from the root.
 *
 * Sizing convention, stated because solvers differ: a raise of `s` puts in
 * what it costs to call, plus `s` times the pot as it stands once that call
 * is made. `s = 1` on an unopened pot is therefore a pot-sized bet. Any
 * amount above a player's stack is an all-in for the remainder.
 */
typedef struct
{
    double pot;                                  /* dead money + committed  */
    double stacks[PE_TREE_OUTLINE_MAX_SEATS];    /* remaining behind        */
    double committed[PE_TREE_OUTLINE_MAX_SEATS]; /* put in so far           */
    double to_call;                              /* for the player to act   */
    double spr;                                  /* effective stack / pot   */
    int all_in;                                  /* someone is all-in       */
} pe_tree_chip_state_t;

typedef struct
{
    int node_index;
    int parent_row;      /* -1 at the root                                  */
    int depth;
    int child_count;     /* actions leaving this node                       */
    int expanded;        /* meaningful only when child_count > 0            */
    int revisit;         /* already shown higher up; not expandable         */
    int acting_player;   /* -1 when the node does not act                   */
    mpf_tree_node_type_t type;
    mpf_street_t street;
    /* The action that reached this row, empty at the root. */
    char label[PE_TREE_OUTLINE_LABEL_MAX];
    char path_key[PE_TREE_OUTLINE_PATH_MAX];
    pe_tree_chip_state_t chips;
} pe_tree_outline_row_t;

typedef struct
{
    double starting_stack;
    double starting_pot;   /* blinds, antes, dead money already in          */
    int player_count;
} pe_tree_outline_config_t;

typedef struct
{
    pe_tree_outline_row_t rows[PE_TREE_OUTLINE_MAX_ROWS];
    int row_count;
    int truncated;         /* the tree did not fit; rows are still valid    */
    /* Fold state lives per node, not per row, so it survives a rebuild and
       so the same node folded in one place is folded everywhere. */
    unsigned char expanded[PE_TREE_EDITOR_MAX_NODES];
    pe_tree_outline_config_t config;
} pe_tree_outline_t;

/** Default chip context: 100 big blinds each, 1.5 posted. */
pe_tree_outline_config_t pe_tree_outline_default_config(int player_count);

/**
 * Prepare an outline for `editor`. Every node starts expanded, which is what
 * a freshly created tree wants; collapse is what the user does next.
 */
void pe_tree_outline_init(pe_tree_outline_t *outline,
                          const pe_tree_outline_config_t *config);

/**
 * Rebuild the visible rows from the current tree and fold state.
 * @return the number of rows, or 0 when there is nothing to show.
 */
int pe_tree_outline_build(pe_tree_outline_t *outline,
                          const pe_tree_editor_t *editor);

/** Fold state, addressed by row. Returns 0 when the row cannot fold. */
int pe_tree_outline_toggle(pe_tree_outline_t *outline, int row);
int pe_tree_outline_set_expanded(pe_tree_outline_t *outline, int row,
                                 int expanded);
void pe_tree_outline_expand_all(pe_tree_outline_t *outline);
void pe_tree_outline_collapse_all(pe_tree_outline_t *outline,
                                  const pe_tree_editor_t *editor);

/**
 * Open every ancestor of `node_index` so that it becomes visible, then
 * rebuild. This is what selecting a node from elsewhere -- the canvas, a
 * search -- has to do before the outline can show it.
 * @return the row the node landed on, or -1 when it is not in the tree.
 */
int pe_tree_outline_reveal(pe_tree_outline_t *outline,
                           const pe_tree_editor_t *editor, int node_index);

/** Row showing `node_index`, or -1. The first row wins for a revisited node. */
int pe_tree_outline_row_of_node(const pe_tree_outline_t *outline,
                                int node_index);

/** Row whose path key matches exactly, or -1. */
int pe_tree_outline_row_of_path(const pe_tree_outline_t *outline,
                                const char *path_key);

/**
 * Render the outline the way it is read: one line per visible row, indented,
 * with a fold marker. Follows snprintf semantics.
 */
int pe_tree_outline_render(const pe_tree_outline_t *outline, char *buffer,
                           size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* PE_TREE_OUTLINE_H */
