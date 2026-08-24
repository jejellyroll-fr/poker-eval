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
    (void)tree; (void)node; (void)profile; (void)user;
    memset(out, 0, PE_HRC_MAX_PLAYERS * sizeof(*out));
    if (path_length >= 1 && path[0] == 1) {
        out[0] = 1.0;
        out[1] = -1.0;
    }
    return 0;
}

int main(void)
{
    pe_combo_t p0_combo, p1_combo;
    pe_range_view_t ranges[2];
    pe_hrc_node_t nodes[4] = {0};
    pe_hrc_config_t config;
    pe_hrc_result_t result;
    StdDeck_CardMask_RESET(p0_combo.hand);
    StdDeck_CardMask_OR(p0_combo.hand, StdDeck_MASK(12), StdDeck_MASK(25));
    p0_combo.weight = 1.0;
    StdDeck_CardMask_RESET(p1_combo.hand);
    StdDeck_CardMask_OR(p1_combo.hand, StdDeck_MASK(0), StdDeck_MASK(13));
    p1_combo.weight = 1.0;
    ranges[0].combos = &p0_combo; ranges[0].count = 1;
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
    config.max_profiles = 4;
    config.terminal_value = terminal_value;
    assert(pe_hrc_solve(&config, &result) == PE_HRC_OK);
    assert(result.profile_count == 1);
    assert(result.action_probability[0][1] > 0.99);
    assert(fabs(result.ev[0] - 1.0) < 1e-6);
    puts("HRC multiway range-tree tests passed");
    return 0;
}
