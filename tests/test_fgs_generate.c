#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/economics/fgs.h>
#include <poker_eval/economics/icm.h>

static void fill_hu_symmetric(pe_fgs_scenario_input_t *input, int depth)
{
    memset(input, 0, sizeof(*input));
    input->num_players = 2;
    input->num_payouts = 2;
    input->stacks[0] = 100.0;
    input->stacks[1] = 100.0;
    input->win_probability[0] = 0.5;
    input->win_probability[1] = 0.5;
    input->pot = 20.0;
    input->depth = depth;
    input->payouts[0] = 70.0;
    input->payouts[1] = 30.0;
}

int main(void)
{
    pe_fgs_scenario_input_t input;
    pe_fgs_node_t nodes[512];
    pe_fgs_edge_t edges[512];
    pe_fgs_tree_t tree;
    pe_fgs_result_t result;

    /* Depth 0: the generator must collapse to the plain ICM value. */
    {
        icm_input_t icm_input;
        icm_result_t icm_result;
        fill_hu_symmetric(&input, 0);
        assert(pe_fgs_generate_even_contribution(&input, nodes, 512, edges, 512,
                                                 &tree) == 0);
        assert(tree.node_count == 1);
        assert(tree.edges[0].probability == 0.0 || tree.edge_count == 0);
        assert(pe_fgs_calculate_tree(&tree, &result) == 0);
        assert(result.leaf_count == 1);
        memset(&icm_input, 0, sizeof(icm_input));
        icm_input.num_players = 2;
        icm_input.num_payouts = 2;
        icm_input.stacks[0] = icm_input.stacks[1] = 100.0;
        icm_input.payouts[0] = 70.0;
        icm_input.payouts[1] = 30.0;
        assert(pe_icm_calculate(&icm_input, &icm_result) == 0);
        assert(fabs(result.ev[0] - icm_result.icm_ev[0]) < 1e-9);
        assert(fabs(result.ev[1] - icm_result.icm_ev[1]) < 1e-9);
    }

    /* Depth 1, symmetric: two leaves, each player's EV stays 50. */
    fill_hu_symmetric(&input, 1);
    assert(pe_fgs_generate_even_contribution(&input, nodes, 512, edges, 512,
                                             &tree) == 0);
    assert(tree.node_count == 3);
    assert(tree.edge_count == 2);
    assert(pe_fgs_calculate_tree(&tree, &result) == 0);
    assert(result.leaf_count == 2);
    assert(fabs(result.probability - 1.0) < 1e-9);
    assert(fabs(result.ev[0] - 50.0) < 1e-9);
    assert(fabs(result.ev[1] - 50.0) < 1e-9);

    /* The public player cap is larger than the branching cap used by the
     * original generator; every validated player must now get a branch. */
    memset(&input, 0, sizeof(input));
    input.num_players = 17;
    input.num_payouts = 17;
    input.pot = 20.0;
    input.depth = 1;
    for (int player = 0; player < input.num_players; ++player) {
        input.stacks[player] = 100.0;
        input.win_probability[player] = 1.0;
        input.payouts[player] = (double)(input.num_players - player);
    }
    assert(pe_fgs_generate_even_contribution(&input, nodes, 512, edges, 512,
                                             &tree) == 0);
    assert(tree.node_count == 18 && tree.edge_count == 17);
    for (int edge = 0; edge < 17; ++edge) {
        assert(edges[edge].child_index >= 1 && edges[edge].child_index < 18);
        assert(edges[edge].probability > 0.0);
        assert(nodes[edges[edge].child_index].terminal == 1);
    }

    /* Depth 2 with a favorite: four leaves, probability mass conserved,
     * EV order follows the win probabilities, payouts stay fully distributed. */
    fill_hu_symmetric(&input, 2);
    input.win_probability[0] = 0.7;
    input.win_probability[1] = 0.3;
    assert(pe_fgs_generate_even_contribution(&input, nodes, 512, edges, 512,
                                             &tree) == 0);
    assert(pe_fgs_calculate_tree(&tree, &result) == 0);
    assert(result.leaf_count == 4);
    assert(fabs(result.probability - 1.0) < 1e-9);
    assert(result.ev[0] > result.ev[1]);
    assert(fabs(result.ev[0] + result.ev[1] - 100.0) < 1e-9);

    /* Chip conservation at every generated leaf. */
    for (size_t i = 0; i < tree.node_count; ++i)
        if (tree.nodes[i].terminal) {
            double chips = tree.nodes[i].stacks[0] + tree.nodes[i].stacks[1];
            assert(fabs(chips - 200.0) < 1e-9);
        }

    /* Capacity exhaustion is reported, not truncated. */
    fill_hu_symmetric(&input, 2);
    assert(pe_fgs_generate_even_contribution(&input, nodes, 2, edges, 512,
                                             &tree) != 0);
    assert(pe_fgs_generate_even_contribution(&input, nodes, 512, edges, 1,
                                             &tree) != 0);
    /* Invalid inputs are rejected. */
    fill_hu_symmetric(&input, 1);
    input.pot = -1.0;
    assert(pe_fgs_generate_even_contribution(&input, nodes, 512, edges, 512,
                                             &tree) != 0);
    fill_hu_symmetric(&input, 1);
    input.win_probability[0] = -0.1;
    assert(pe_fgs_generate_even_contribution(&input, nodes, 512, edges, 512,
                                             &tree) != 0);
    fill_hu_symmetric(&input, 1);
    input.num_payouts = 3;
    assert(pe_fgs_generate_even_contribution(&input, nodes, 512, edges, 512,
                                             &tree) != 0);

    puts("FGS scenario generator tests passed");
    return 0;
}
