#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../tools/pe_tree_layout.h"

/* root -> {n1, n2}; n1 -> {n3, n4}; n2 -> {n5, n6}; n3..n6 terminal. */
static const char *DEEP_TREE =
    "{\"version\":1,\"root\":\"r\",\"nodes\":["
    "{\"id\":\"r\",\"type\":\"player\",\"player\":0,\"actions\":["
    "{\"type\":\"bet\",\"next\":\"n1\"},{\"type\":\"check\",\"next\":\"n2\"}]},"
    "{\"id\":\"n1\",\"type\":\"player\",\"player\":1,\"actions\":["
    "{\"type\":\"call\",\"next\":\"n3\"},{\"type\":\"raise\",\"next\":\"n4\"}]},"
    "{\"id\":\"n2\",\"type\":\"player\",\"player\":1,\"actions\":["
    "{\"type\":\"fold\",\"next\":\"n5\"},{\"type\":\"call\",\"next\":\"n6\"}]},"
    "{\"id\":\"n3\",\"type\":\"terminal\"},"
    "{\"id\":\"n4\",\"type\":\"terminal\"},"
    "{\"id\":\"n5\",\"type\":\"terminal\"},"
    "{\"id\":\"n6\",\"type\":\"terminal\"}"
    "]}";

/* Two-node cycle: BFS must terminate via the visited mark. */
static const char *CYCLE_TREE =
    "{\"version\":1,\"root\":\"g\",\"nodes\":["
    "{\"id\":\"g\",\"type\":\"player\",\"player\":0,\"actions\":["
    "{\"type\":\"bet\",\"next\":\"h\"}]},"
    "{\"id\":\"h\",\"type\":\"player\",\"player\":1,\"actions\":["
    "{\"type\":\"call\",\"next\":\"g\"}]}"
    "]}";

static int entry_at(const pe_tree_layout_t *layout, const char *id,
                    const pe_tree_json_t *tree)
{
    for (size_t i = 0; i < layout->count; ++i)
        if (strcmp(tree->nodes[layout->entries[i].node_index].id, id) == 0)
            return (int)i;
    return -1;
}

int main(void)
{
    pe_tree_json_t tree;
    pe_tree_layout_entry_t entries[64];
    pe_tree_layout_t layout;

    assert(pe_tree_json_parse(DEEP_TREE, strlen(DEEP_TREE), &tree, 64) == 0);

    /* Full expansion: 7 nodes over three depths. */
    assert(pe_tree_layout_bfs(&tree, 3, 4, entries, 64, &layout) == 0);
    assert(layout.count == 7);
    assert(layout.depth_count == 3);
    assert(layout.truncated == 0);
    {
        int root = entry_at(&layout, "r", &tree);
        int n1 = entry_at(&layout, "n1", &tree);
        int n2 = entry_at(&layout, "n2", &tree);
        int n3 = entry_at(&layout, "n3", &tree);
        int n6 = entry_at(&layout, "n6", &tree);
        assert(root == 0);
        assert(layout.entries[root].depth == 0);
        assert(layout.entries[root].parent_entry == -1);
        assert(layout.entries[root].action_from_parent == -1);
        assert(n1 >= 0 && n2 >= 0 && n3 >= 0 && n6 >= 0);
        assert(layout.entries[n1].depth == 1 && layout.entries[n2].depth == 1);
        assert(layout.entries[n1].slot == 0 && layout.entries[n2].slot == 1);
        assert(layout.entries[n1].row_width == 2);
        assert(layout.entries[n1].parent_entry == root);
        assert(layout.entries[n1].action_from_parent == 0); /* bet */
        assert(layout.entries[n2].action_from_parent == 1); /* check */
        assert(layout.entries[n3].depth == 2 && layout.entries[n6].depth == 2);
        assert(layout.entries[n3].slot == 0 && layout.entries[n3].row_width == 4);
        assert(layout.entries[n6].slot == 3);
        assert(layout.entries[n3].parent_entry == n1);
        assert(layout.entries[n6].parent_entry == n2);
    }

    /* Row cap keeps one node per depth. */
    assert(pe_tree_layout_bfs(&tree, 3, 1, entries, 64, &layout) == 0);
    assert(layout.count == 3);
    assert(layout.truncated == 1);
    assert(strcmp(tree.nodes[layout.entries[1].node_index].id, "n1") == 0);
    assert(strcmp(tree.nodes[layout.entries[2].node_index].id, "n3") == 0);

    /* Depth cap stops after the second level. */
    assert(pe_tree_layout_bfs(&tree, 1, 4, entries, 64, &layout) == 0);
    assert(layout.count == 3);
    assert(layout.depth_count == 2);
    assert(layout.truncated == 1);

    /* Cycles terminate. */
    pe_tree_json_free(&tree);
    assert(pe_tree_json_parse(CYCLE_TREE, strlen(CYCLE_TREE), &tree, 64) == 0);
    assert(pe_tree_layout_bfs(&tree, 4, 4, entries, 64, &layout) == 0);
    assert(layout.count == 2);
    assert(layout.truncated == 0);
    pe_tree_json_free(&tree);

    /* Invalid arguments. */
    assert(pe_tree_json_parse(DEEP_TREE, strlen(DEEP_TREE), &tree, 64) == 0);
    assert(pe_tree_layout_bfs(NULL, 2, 2, entries, 64, &layout) != 0);
    assert(pe_tree_layout_bfs(&tree, 2, 0, entries, 64, &layout) != 0);
    assert(pe_tree_layout_bfs(&tree, 2, 2, entries, 2, &layout) != 0);
    pe_tree_json_free(&tree);

    puts("pe_tree_layout tests passed");
    return 0;
}
