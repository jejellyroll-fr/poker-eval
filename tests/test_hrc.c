#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/economics/hrc.h>

static int terminal_value(const pe_hrc_tree_t *tree, int node,
                          const pe_hrc_profile_t *profile,
                          const uint16_t *path, size_t path_length,
                          double *out, void *user)
{
    (void)tree; (void)node; (void)user;
    memset(out, 0, PE_HRC_MAX_PLAYERS * sizeof(*out));
    if (path_length >= 1 &&
        ((path[0] == 1 && profile->combo_index[0] == 0) ||
         (path[0] == 0 && profile->combo_index[0] == 1))) {
        out[0] = 1.0;
        out[1] = -1.0;
    }
    return 0;
}

int main(void)
{
    pe_combo_t p0_combos[2], p1_combo;
    pe_range_view_t ranges[2];
    pe_hrc_node_t nodes[4] = {0};
    pe_hrc_config_t config;
    pe_hrc_result_t result;
    StdDeck_CardMask_RESET(p0_combos[0].hand);
    StdDeck_CardMask_OR(p0_combos[0].hand, StdDeck_MASK(12), StdDeck_MASK(25));
    p0_combos[0].weight = 1.0;
    StdDeck_CardMask_RESET(p0_combos[1].hand);
    StdDeck_CardMask_OR(p0_combos[1].hand, StdDeck_MASK(11), StdDeck_MASK(24));
    p0_combos[1].weight = 1.0;
    StdDeck_CardMask_RESET(p1_combo.hand);
    StdDeck_CardMask_OR(p1_combo.hand, StdDeck_MASK(0), StdDeck_MASK(13));
    p1_combo.weight = 1.0;
    ranges[0].combos = p0_combos; ranges[0].count = 2;
    ranges[1].combos = &p1_combo; ranges[1].count = 1;
    nodes[0].player_to_act = 0;
    nodes[0].action_count = 2;
    nodes[0].actions[0].child_index = 1;
    nodes[0].actions[1].child_index = 2;
    nodes[1].terminal = 1;
    nodes[2].player_to_act = 1;
    nodes[2].action_count = 1;
    nodes[2].actions[0].child_index = 3;
    nodes[3].terminal = 1;
    memset(&config, 0, sizeof(config));
    config.tree.nodes = nodes;
    config.tree.node_count = 4;
    config.tree.root_index = 0;
    config.tree.num_players = 2;
    config.ranges[0] = ranges[0];
    config.ranges[1] = ranges[1];
    config.iterations = 100;
    config.max_profiles = 8;
    config.terminal_value = terminal_value;
    assert(pe_hrc_solve(&config, &result) == PE_HRC_OK);
    assert(result.profile_count == 2);
    assert(result.action_probability[0][1] > 0.49 &&
           result.action_probability[0][1] < 0.51);
    assert(pe_hrc_result_combo_probability(&result, 0, 0, 0, 1) > 0.99);
    assert(pe_hrc_result_combo_probability(&result, 0, 0, 1, 0) > 0.99);
    assert(result.ev[0] > 0.98 && result.ev[0] <= 1.0);
    pe_hrc_result_free(&result);

    /* An exact fit must be accepted; only a real third profile is overflow. */
    config.max_profiles = 2;
    assert(pe_hrc_solve(&config, &result) == PE_HRC_OK);
    assert(result.profile_count == 2);
    pe_hrc_result_free(&result);
    config.max_profiles = 1;
    assert(pe_hrc_solve(&config, &result) == PE_HRC_ERR_PROFILE_LIMIT);

    /* A later range combo can be fully blocked without producing another
     * profile. It must not consume capacity before the terminal case. */
    StdDeck_CardMask_RESET(p1_combo.hand);
    StdDeck_CardMask_OR(p1_combo.hand, StdDeck_MASK(11), StdDeck_MASK(24));
    assert(pe_hrc_solve(&config, &result) == PE_HRC_OK);
    assert(result.profile_count == 1);
    pe_hrc_result_free(&result);
    StdDeck_CardMask_RESET(p1_combo.hand);
    StdDeck_CardMask_OR(p1_combo.hand, StdDeck_MASK(0), StdDeck_MASK(13));
    config.max_profiles = 8;

    /* The evaluator uses a fixed path buffer; reject a tree that needs an
     * additional decision frame instead of accepting it and failing later. */
    {
        pe_hrc_node_t deep_nodes[PE_HRC_MAX_DEPTH + 2] = {0};
        config.tree.nodes = deep_nodes;
        config.tree.node_count = PE_HRC_MAX_DEPTH + 2;
        config.tree.root_index = 0;
        for (int node = 0; node <= PE_HRC_MAX_DEPTH; ++node) {
            deep_nodes[node].player_to_act = 0;
            deep_nodes[node].action_count = 1;
            deep_nodes[node].actions[0].child_index = node + 1;
        }
        deep_nodes[PE_HRC_MAX_DEPTH + 1].terminal = 1;
        assert(pe_hrc_validate(&config) == PE_HRC_ERR_INVALID_TREE);
    }

    {
        pe_hrc_node_t invalid_nodes[5] = {0};
        memcpy(invalid_nodes, nodes, sizeof(nodes));
        invalid_nodes[4].player_to_act = -1;
        invalid_nodes[4].action_count = 0;
        config.tree.nodes = invalid_nodes;
        config.tree.node_count = 5;
        assert(pe_hrc_validate(&config) == PE_HRC_ERR_INVALID_TREE);
    }
    puts("HRC multiway range-tree tests passed");
    return 0;
}
