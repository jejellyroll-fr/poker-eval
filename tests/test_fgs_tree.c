#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/economics/fgs.h>

int main(void)
{
    pe_fgs_node_t nodes[3] = {0};
    pe_fgs_edge_t edges[2] = {{1, 0.5}, {2, 0.5}};
    pe_fgs_tree_t tree;
    pe_fgs_result_t result;
    nodes[0].first_edge = 0; nodes[0].edge_count = 2;
    nodes[1].terminal = 1; nodes[1].stacks[0] = 100.0; nodes[1].stacks[1] = 1.0;
    nodes[2].terminal = 1; nodes[2].stacks[0] = 1.0; nodes[2].stacks[1] = 100.0;
    memset(&tree, 0, sizeof(tree));
    tree.nodes = nodes; tree.node_count = 3;
    tree.edges = edges; tree.edge_count = 2; tree.root_index = 0;
    tree.num_players = 2; tree.num_payouts = 2;
    tree.payouts[0] = 70.0; tree.payouts[1] = 30.0;
    assert(pe_fgs_calculate_tree(&tree, &result) == 0);
    assert(result.leaf_count == 2);
    assert(fabs(result.probability - 1.0) < 1e-9);
    assert(fabs(result.ev[0] - 50.0) < 1e-9);
    assert(fabs(result.ev[1] - 50.0) < 1e-9);
    puts("Dynamic FGS tree tests passed");
    return 0;
}
