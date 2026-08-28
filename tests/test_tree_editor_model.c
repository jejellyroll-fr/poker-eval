#include "../tools/pe_tree_editor_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(int condition, const char *message)
{
    if (!condition)
        fprintf(stderr, "tree editor model: %s\n", message);
    return condition;
}

int main(void)
{
    pe_tree_editor_t editor;
    char *json = NULL;
    size_t json_length = 0u;
    char error[128];
    int raise_child = -1;
    int chance_child = -1;
    mpf_tree_def_t *round_trip;
    mpf_tree_error_t tree_error = {0};
    int root_action_count;

    pe_tree_editor_init(&editor, 2, MPF_STREET_RIVER);
    if (!check(editor.root_index == 0 && editor.node_count == 3,
               "new tree has a root and two terminal children"))
        return 1;
    if (!check(!pe_tree_editor_add_action(&editor, editor.root_index,
                                          MPF_TREE_ACTION_FOLD, 0.0, NULL),
               "duplicate fold action is rejected"))
        return 1;
    if (!check(editor.nodes[editor.root_index].action_count == 2,
               "duplicate fold did not mutate the root"))
        return 1;
    if (!check(pe_tree_editor_add_action(&editor, editor.root_index,
                                         MPF_TREE_ACTION_RAISE, 0.75,
                                         &raise_child),
               "raise action can be added"))
        return 1;
    if (!check(raise_child >= 0 && editor.nodes[raise_child].action_count == 2,
               "raise creates an editable child player node"))
        return 1;
    if (!check(pe_tree_editor_add_action(&editor, editor.root_index,
                                         MPF_TREE_ACTION_CHANCE, 0.0,
                                         &chance_child),
               "chance action can be added"))
        return 1;
    if (!check(pe_tree_editor_reachable(&editor, chance_child),
               "new child is reachable"))
        return 1;
    if (!check(pe_tree_editor_serialize_json(&editor, &json, &json_length),
               "tree serializes"))
        return 1;
    round_trip = mpf_tree_load_json(json, json_length, &tree_error);
    if (!check(round_trip != NULL, tree_error.message[0]
               ? tree_error.message : "serialized tree loads"))
    {
        free(json);
        return 1;
    }
    if (!check(mpf_tree_validate(round_trip, &tree_error),
               tree_error.message[0] ? tree_error.message : "serialized tree validates"))
    {
        mpf_tree_free(round_trip);
        free(json);
        return 1;
    }
    root_action_count = editor.nodes[editor.root_index].action_count;
    if (!check(pe_tree_editor_remove_action(&editor, editor.root_index, root_action_count - 1),
               "root action can be removed"))
    {
        mpf_tree_free(round_trip);
        free(json);
        return 1;
    }
    if (!check(pe_tree_editor_remove_action(&editor, editor.root_index, 0),
               "root action can be removed again"))
    {
        mpf_tree_free(round_trip);
        free(json);
        return 1;
    }
    if (!check(pe_tree_editor_remove_action(&editor, editor.root_index, 0),
               "root can be reduced to one action"))
    {
        mpf_tree_free(round_trip);
        free(json);
        return 1;
    }
    if (!check(!pe_tree_editor_remove_action(&editor, editor.root_index, 0),
               "the final root action cannot be removed"))
    {
        mpf_tree_free(round_trip);
        free(json);
        return 1;
    }
    if (!check(pe_tree_editor_validate(&editor, error, sizeof(error)),
               error[0] ? error : "edited tree validates"))
    {
        mpf_tree_free(round_trip);
        free(json);
        return 1;
    }
    mpf_tree_free(round_trip);
    free(json);
    return 0;
}
