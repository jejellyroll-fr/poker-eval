/*
 * test_monker_tree.c - MKR-01: MonkerSolver .tree header validation
 */

#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
            failures++;                                            \
        }                                                          \
    } while (0)

static void put_i32(unsigned char *buffer, size_t *at, int32_t value)
{
    uint32_t u = (uint32_t)value;
    unsigned i;
    for (i = 0u; i < 4u; ++i)
        buffer[(*at)++] = (unsigned char)(u >> (8u * i));
}

static void put_i64(unsigned char *buffer, size_t *at, int64_t value)
{
    uint64_t u = (uint64_t)value;
    unsigned i;
    for (i = 0u; i < 8u; ++i)
        buffer[(*at)++] = (unsigned char)(u >> (8u * i));
}

static void put_f64(unsigned char *buffer, size_t *at, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_i64(buffer, at, (int64_t)bits);
}

static int write_path_fixture(const char *path,
                              const unsigned char *bytes, size_t length)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (!file)
        return -1;
    ok = fwrite(bytes, 1u, length, file) == length && fclose(file) == 0;
    return ok ? 0 : -1;
}

static void test_header_variants(void)
{
    const char *path = "/tmp/poker_eval_monker_header.tree";
    unsigned char bytes[256];
    size_t at = 0u;
    pe_monker_tree_header_t header;

    put_i64(bytes, &at, 33487);
    put_i32(bytes, &at, 12);
    put_i32(bytes, &at, 3);
    put_i32(bytes, &at, 1);
    put_i32(bytes, &at, 0);
    put_f64(bytes, &at, 1.0);
    put_f64(bytes, &at, 2.0);
    put_f64(bytes, &at, 3.0);
    put_f64(bytes, &at, 0.5);
    put_f64(bytes, &at, 100.0);
    put_f64(bytes, &at, 99.0);
    put_f64(bytes, &at, 98.0);
    CHECK(write_path_fixture(path, bytes, at) == 0, "fixture write failed");
    CHECK(pe_monker_tree_read_header(path, &header) == PE_MONKER_OK,
          "known header was rejected");
    CHECK(header.signature == 33487 && header.internal_format == 12 &&
              header.player_count == 3u && header.first_to_act == 1 &&
              header.street == 0 && header.committed[1] == 2.0 &&
              header.dead_money == 0.5 && header.stacks[2] == 98.0,
          "known header fields were decoded incorrectly");

    at = 0u;
    put_i64(bytes, &at, 33488);
    put_i32(bytes, &at, 13);
    put_i32(bytes, &at, 2);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 2);
    put_f64(bytes, &at, 1.25);
    put_f64(bytes, &at, 80.0);
    put_f64(bytes, &at, 80.0);
    CHECK(write_path_fixture(path, bytes, at) == 0, "later-street fixture failed");
    memset(&header, 0xA5, sizeof(header));
    CHECK(pe_monker_tree_read_header(path, &header) == PE_MONKER_OK,
          "later-street header was rejected");
    CHECK(header.committed[0] == 0.0 && header.dead_money == 1.25 &&
              header.stacks[0] == 80.0,
          "committed values were consumed on a later street");
}

static void test_rejections(void)
{
    const char *path = "/tmp/poker_eval_monker_header.tree";
    unsigned char bytes[32];
    size_t at = 0u;
    pe_monker_tree_header_t header;

    put_i64(bytes, &at, 12345);
    put_i32(bytes, &at, 1);
    put_i32(bytes, &at, 2);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 0);
    CHECK(write_path_fixture(path, bytes, at) == 0, "bad signature fixture failed");
    CHECK(pe_monker_tree_read_header(path, &header) == PE_MONKER_ERR_BAD_SIGNATURE,
          "unknown signature was not rejected explicitly");

    at = 0u;
    put_i64(bytes, &at, 33487);
    put_i32(bytes, &at, 1);
    CHECK(write_path_fixture(path, bytes, at) == 0, "truncated fixture failed");
    CHECK(pe_monker_tree_read_header(path, &header) == PE_MONKER_ERR_TRUNCATED,
          "truncated header was not rejected");
    CHECK(pe_monker_tree_read_header(path, NULL) == PE_MONKER_ERR_NULL_ARGUMENT,
          "NULL output was not rejected");
}

static void test_node_topology(void)
{
    const char *path = "/tmp/poker_eval_monker_nodes.tree";
    unsigned char bytes[256];
    size_t at = 0u;
    pe_monker_tree_header_t header;
    mpf_tree_def_t *tree = NULL;
    mpf_tree_error_t error;

    put_i64(bytes, &at, 33487);
    put_i32(bytes, &at, 12);
    put_i32(bytes, &at, 2);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 0);
    put_f64(bytes, &at, 1.0);
    put_f64(bytes, &at, 1.0);
    put_f64(bytes, &at, 0.0);
    put_f64(bytes, &at, 100.0);
    put_f64(bytes, &at, 100.0);

    /* Preorder: root, CALL leaf, half-pot node, then FOLD/CALL leaves. */
    bytes[at++] = 0u;
    bytes[at++] = 2u;
    bytes[at++] = 1u;
    bytes[at++] = 0u;
    bytes[at++] = 4u;
    bytes[at++] = 2u;
    bytes[at++] = 0u;
    bytes[at++] = 0u;
    bytes[at++] = 1u;
    bytes[at++] = 0u;

    CHECK(write_path_fixture(path, bytes, at) == 0, "node fixture write failed");
    CHECK(pe_monker_tree_read_header(path, &header) == PE_MONKER_OK,
          "node fixture header failed");
    CHECK(pe_monker_tree_load(path, &tree) == PE_MONKER_OK && tree != NULL,
          "node stream was not loaded");
    if (!tree)
        return;
    CHECK(tree->node_count == 5 && tree->root_index == 0,
          "unexpected node count or root");
    CHECK(tree->nodes[0].action_count == 2 &&
              tree->nodes[0].actions[0].type == MPF_TREE_ACTION_CALL &&
              tree->nodes[0].actions[0].next_index == 1 &&
              tree->nodes[0].actions[1].type == MPF_TREE_ACTION_RAISE &&
              tree->nodes[0].actions[1].next_index == 2,
          "root actions were not reconstructed");
    CHECK(tree->nodes[2].action_count == 2 &&
              tree->nodes[2].actions[0].type == MPF_TREE_ACTION_FOLD &&
              tree->nodes[2].actions[1].type == MPF_TREE_ACTION_CALL,
          "nested actions were not reconstructed");
    CHECK(mpf_tree_validate(tree, &error) != 0,
          "reconstructed topology does not validate");
    mpf_tree_free(tree);
}

int main(void)
{
    test_header_variants();
    test_rejections();
    test_node_topology();
    if (failures != 0)
        return 1;
    puts("test_monker_tree: header decoding and rejection paths passed");
    return 0;
}
