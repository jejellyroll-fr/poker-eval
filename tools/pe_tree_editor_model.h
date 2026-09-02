#ifndef PE_TREE_EDITOR_MODEL_H
#define PE_TREE_EDITOR_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#define PE_TREE_EDITOR_MAX_NODES 512
#define PE_TREE_EDITOR_MAX_ACTIONS MPF_TREE_ACTION_MAX
#define PE_TREE_EDITOR_MAX_SIZES 16

typedef struct
{
    mpf_tree_action_type_t type;
    int size_index;
    double weight;
    int next_index;
} pe_tree_editor_action_t;

typedef struct
{
    char id[64];
    mpf_tree_node_type_t type;
    mpf_street_t street;
    int acting_player;
    double bet_sizes[PE_TREE_EDITOR_MAX_SIZES];
    int bet_size_count;
    int use_pot_sizing;
    pe_tree_editor_action_t actions[PE_TREE_EDITOR_MAX_ACTIONS];
    int action_count;
} pe_tree_editor_node_t;

typedef struct
{
    int version;
    int player_count;
    int root_index;
    int node_count;
    pe_tree_editor_node_t nodes[PE_TREE_EDITOR_MAX_NODES];
} pe_tree_editor_t;

void pe_tree_editor_init(pe_tree_editor_t *editor, int player_count,
                         mpf_street_t street);
int pe_tree_editor_import(pe_tree_editor_t *editor, const mpf_tree_def_t *tree);
int pe_tree_editor_add_action(pe_tree_editor_t *editor, int node_index,
                              mpf_tree_action_type_t type, double bet_size,
                              int *out_child_index);
int pe_tree_editor_remove_action(pe_tree_editor_t *editor, int node_index,
                                 int action_index);
int pe_tree_editor_reachable(const pe_tree_editor_t *editor, int node_index);
int pe_tree_editor_serialize_json(const pe_tree_editor_t *editor,
                                  char **out_json, size_t *out_len);
int pe_tree_editor_write_json(const pe_tree_editor_t *editor, const char *path,
                              char *error, size_t error_capacity);
int pe_tree_editor_validate(const pe_tree_editor_t *editor, char *error,
                            size_t error_capacity);
int pe_tree_editor_render(const pe_tree_editor_t *editor, char *buffer,
                          size_t capacity);
const char *pe_tree_editor_action_name(mpf_tree_action_type_t type);
const char *pe_tree_editor_node_type_name(mpf_tree_node_type_t type);

#endif
