#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * Parser shared with the desktop trainer tree view. The sample document
 * below mirrors mpf_tree_serialize_json output (examples/heads_up_river/
 * tree.json has the same shape), including nested snapshot objects whose
 * braces and strings the scanner must skip.
 */
#include "../tools/pe_tree_json.h"

static const char *SAMPLE_TREE =
    "{\"version\":1,\"root\":\"river_p0\","
    "\"betProfiles\":[{\"id\":\"default\",\"sizes\":[3.0],\"pot_sizing\":false}],"
    "\"rangeProfiles\":[{\"id\":\"p0_river\",\"player\":0,\"street\":\"RIVER\","
    "\"combos\":[{\"hand\":\"AhKh\",\"weight\":1.0}]}],"
    "\"nodes\":["
    "{\"id\":\"river_p0\",\"type\":\"player\",\"street\":\"RIVER\",\"player\":0,"
    "\"bet_profile\":\"default\",\"range_profile\":\"p0_river\","
    "\"snapshot\":{\"defined\":true,\"num_players\":2,\"street\":\"RIVER\","
    "\"to_act\":0,\"first_to_act\":0,\"pot\":4.0,\"to_call\":0.0},"
    "\"actions\":[{\"type\":\"call\",\"next\":\"terminal_check\"},"
    "{\"type\":\"raise\",\"size_index\":0,\"next\":\"river_p1\"}]},"
    "{\"id\":\"river_p1\",\"type\":\"player\",\"street\":\"RIVER\",\"player\":1,"
    "\"actions\":[{\"type\":\"call\",\"next\":\"terminal_showdown\"},"
    "{\"type\":\"fold\",\"next\":\"terminal_fold\"}]},"
    "{\"id\":\"deal_turn\",\"type\":\"chance\",\"street\":\"TURN\","
    "\"actions\":[{\"type\":\"deal\",\"next\":\"river_p0\"}]},"
    "{\"id\":\"terminal_check\",\"type\":\"terminal\",\"street\":\"RIVER\"}"
    "]}";

static void test_full_parse(void)
{
    pe_tree_json_t tree;
    assert(pe_tree_json_parse(SAMPLE_TREE, strlen(SAMPLE_TREE), &tree, 64) == 0);
    assert(strcmp(tree.root_id, "river_p0") == 0);
    assert(tree.count == 4);

    assert(strcmp(tree.nodes[0].id, "river_p0") == 0);
    assert(strcmp(tree.nodes[0].type, "player") == 0);
    assert(tree.nodes[0].player == 0);
    assert(tree.nodes[0].action_count == 2);
    assert(strcmp(tree.nodes[0].action_type[0], "call") == 0);
    assert(strcmp(tree.nodes[0].action_next[0], "terminal_check") == 0);
    assert(strcmp(tree.nodes[0].action_type[1], "raise") == 0);
    assert(strcmp(tree.nodes[0].action_next[1], "river_p1") == 0);

    /* Snapshot braces/strings must not confuse the object scanner. */
    assert(strcmp(tree.nodes[1].id, "river_p1") == 0);
    assert(tree.nodes[1].player == 1);
    assert(tree.nodes[1].action_count == 2);

    /* Chance nodes carry no acting player. */
    assert(strcmp(tree.nodes[2].type, "chance") == 0);
    assert(tree.nodes[2].player == -1);
    assert(tree.nodes[2].action_count == 1);

    assert(strcmp(tree.nodes[3].type, "terminal") == 0);
    assert(tree.nodes[3].action_count == 0);

    pe_tree_json_free(&tree);
    assert(tree.nodes == NULL && tree.count == 0);
}

static void test_bounded_and_invalid(void)
{
    pe_tree_json_t tree;
    /* Huge trees are capped, not rejected. */
    assert(pe_tree_json_parse(SAMPLE_TREE, strlen(SAMPLE_TREE), &tree, 2) == 0);
    assert(tree.count == 2);
    pe_tree_json_free(&tree);

    assert(pe_tree_json_parse("", 0, &tree, 4) != 0);
    assert(pe_tree_json_parse("{\"root\":\"x\"}", 12, &tree, 4) != 0);
    assert(pe_tree_json_parse(NULL, 10, &tree, 4) != 0);
}

#ifdef PE_TREE_JSON_SAMPLE
/* Parse the committed example tree, which is exactly what the GUI receives
 * after mpf_tree_serialize_json (or pe-monker-validate --tree-json). */
static void test_example_file(void)
{
    FILE *file = fopen(PE_TREE_JSON_SAMPLE, "rb");
    long size;
    char *data;
    pe_tree_json_t tree;
    size_t with_actions = 0;
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 0);
    rewind(file);
    data = (char *)malloc((size_t)size + 1);
    assert(data != NULL);
    assert(fread(data, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    data[size] = '\0';
    assert(pe_tree_json_parse(data, (size_t)size, &tree, 4096) == 0);
    assert(tree.root_id[0] != '\0');
    assert(tree.count > 0);
    for (size_t i = 0; i < tree.count; ++i)
        if (tree.nodes[i].action_count > 0)
            ++with_actions;
    assert(with_actions > 0);
    pe_tree_json_free(&tree);
    free(data);
}
#endif

int main(void)
{
    test_full_parse();
    test_bounded_and_invalid();
#ifdef PE_TREE_JSON_SAMPLE
    test_example_file();
#endif
    puts("pe_tree_json tests passed");
    return 0;
}
