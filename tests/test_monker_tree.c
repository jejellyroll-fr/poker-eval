/*
 * test_monker_tree.c - MKR-01: MonkerSolver .tree reading
 *
 * The fixtures below are written the way MonkerSolver writes: big-endian,
 * because java.io.DataOutputStream is big-endian on every platform; int32 for
 * the money fields; two bytes per node field, because the node stream is
 * written with writeChar and a Java char is a 16-bit code unit.
 *
 * That was not always true here. The fixtures used to be written little-endian
 * with 8-byte doubles and 1-byte node fields — the same conventions the reader
 * used — so every test passed and not one real MonkerSolver file could be
 * opened. A hand-built fixture only proves the reader agrees with the fixture
 * writer; when the same person writes both, it proves nothing at all. That is
 * why the last test in this file reads bytes nobody here wrote.
 */

#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                          \
            fputc('\n', stderr);                                   \
            failures++;                                            \
        }                                                          \
    } while (0)

/* Big-endian, like the format. */
static void put_i32(unsigned char *buffer, size_t *at, int32_t value)
{
    uint32_t u = (uint32_t)value;
    unsigned i;
    for (i = 0u; i < 4u; ++i)
        buffer[(*at)++] = (unsigned char)(u >> (8u * (3u - i)));
}

static void put_i64(unsigned char *buffer, size_t *at, int64_t value)
{
    uint64_t u = (uint64_t)value;
    unsigned i;
    for (i = 0u; i < 8u; ++i)
        buffer[(*at)++] = (unsigned char)(u >> (8u * (7u - i)));
}

/* A Java `char`: two big-endian bytes. Both node fields use it. */
static void put_u16(unsigned char *buffer, size_t *at, unsigned value)
{
    buffer[(*at)++] = (unsigned char)(value >> 8u);
    buffer[(*at)++] = (unsigned char)(value & 0xFFu);
}

/* Committed, dead money and stacks are int32 in the file, not doubles. */
static void put_money(unsigned char *buffer, size_t *at, int32_t value)
{
    put_i32(buffer, at, value);
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
    put_money(bytes, &at, 1);      /* committed[0] */
    put_money(bytes, &at, 2);      /* committed[1] */
    put_money(bytes, &at, 3);      /* committed[2] */
    put_money(bytes, &at, 50);     /* dead money */
    put_money(bytes, &at, 10000);  /* stacks[0] */
    put_money(bytes, &at, 9900);
    put_money(bytes, &at, 9800);
    CHECK(write_path_fixture(path, bytes, at) == 0, "fixture write failed");
    CHECK(pe_monker_tree_read_header(path, &header) == PE_MONKER_OK,
          "known header was rejected");
    CHECK(header.signature == 33487 && header.internal_format == 12 &&
              header.player_count == 3u && header.first_to_act == 1 &&
              header.street == 0 && header.committed[1] == 2.0 &&
              header.dead_money == 50.0 && header.stacks[2] == 9800.0,
          "known header fields were decoded incorrectly");

    at = 0u;
    put_i64(bytes, &at, 33488);
    put_i32(bytes, &at, 13);
    put_i32(bytes, &at, 2);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 2);
    put_money(bytes, &at, 125);
    put_money(bytes, &at, 8000);
    put_money(bytes, &at, 8000);
    CHECK(write_path_fixture(path, bytes, at) == 0, "later-street fixture failed");
    memset(&header, 0xA5, sizeof(header));
    CHECK(pe_monker_tree_read_header(path, &header) == PE_MONKER_OK,
          "later-street header was rejected");
    CHECK(header.committed[0] == 0.0 && header.dead_money == 125.0 &&
              header.stacks[0] == 8000.0,
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

    /* Little-endian is the exact mistake this reader used to make. It must be
       a rejection now, not a successful read. */
    at = 0u;
    bytes[at++] = 0xcf; bytes[at++] = 0x82;
    bytes[at++] = 0x00; bytes[at++] = 0x00;
    bytes[at++] = 0x00; bytes[at++] = 0x00;
    bytes[at++] = 0x00; bytes[at++] = 0x00;
    put_i32(bytes, &at, 1);
    put_i32(bytes, &at, 2);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 0);
    CHECK(write_path_fixture(path, bytes, at) == 0, "byte-order fixture failed");
    CHECK(pe_monker_tree_read_header(path, &header) == PE_MONKER_ERR_BAD_SIGNATURE,
          "a little-endian signature was accepted");

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
    put_money(bytes, &at, 100);   /* committed[0] */
    put_money(bytes, &at, 100);   /* committed[1] */
    put_money(bytes, &at, 0);     /* dead money */
    put_money(bytes, &at, 10000); /* stacks */
    put_money(bytes, &at, 10000);

    /*
     * Preorder. The root writes only its child count; every other node is
     * preceded by the action code of the edge that reaches it.
     *
     *   root (2)
     *     -- CALL -----> leaf
     *     -- 50% pot --> node (2)
     *                      -- FOLD --> leaf
     *                      -- CALL --> leaf
     */
    put_u16(bytes, &at, 2u);      /* root child count   */
    put_u16(bytes, &at, 1u);      /*   edge: call       */
    put_u16(bytes, &at, 0u);      /*   leaf             */
    put_u16(bytes, &at, 40050u);  /*   edge: 50% pot    */
    put_u16(bytes, &at, 2u);      /*   node, 2 children */
    put_u16(bytes, &at, 0u);      /*     edge: fold     */
    put_u16(bytes, &at, 0u);      /*     leaf           */
    put_u16(bytes, &at, 1u);      /*     edge: call     */
    put_u16(bytes, &at, 0u);      /*     leaf           */
    bytes[at++] = 0u;             /* no ranges block    */

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
    /* The size must survive as a pot fraction, not as the raw code. */
    CHECK(tree->nodes[0].bet_size_count == 1 &&
              tree->nodes[0].bet_sizes[0] == 0.5,
          "the half-pot sizing did not decode to 0.5");
    CHECK(tree->nodes[2].action_count == 2 &&
              tree->nodes[2].actions[0].type == MPF_TREE_ACTION_FOLD &&
              tree->nodes[2].actions[1].type == MPF_TREE_ACTION_CALL,
          "nested actions were not reconstructed");
    CHECK(mpf_tree_validate(tree, &error) != 0,
          "reconstructed topology does not validate");
    mpf_tree_free(tree);
}

/*
 * An action code with no evidence behind it must be refused, not guessed.
 * Returning a plausible number for an unknown code is this reader's most
 * dangerous failure: nothing crashes, and the tree solves a different game.
 */
static void test_unverified_action_codes_are_refused(void)
{
    const char *path = "/tmp/poker_eval_monker_unknown.tree";
    unsigned char bytes[128];
    size_t at = 0u;
    mpf_tree_def_t *tree = NULL;

    put_i64(bytes, &at, 33487);
    put_i32(bytes, &at, 12);
    put_i32(bytes, &at, 2);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 1);
    put_money(bytes, &at, 0);
    put_money(bytes, &at, 10000);
    put_money(bytes, &at, 10000);
    put_u16(bytes, &at, 1u);   /* root, one child     */
    put_u16(bytes, &at, 7u);   /* an unevidenced code */
    put_u16(bytes, &at, 0u);   /* leaf                */
    bytes[at++] = 0u;

    CHECK(write_path_fixture(path, bytes, at) == 0, "unknown-code fixture failed");
    CHECK(pe_monker_tree_load(path, &tree) == PE_MONKER_ERR_INVALID_ACTION,
          "an unevidenced action code was given a bet size anyway");
    if (tree)
        mpf_tree_free(tree);
}

static void test_fixed_ranges(void)
{
    const char *path = "/tmp/poker_eval_monker_ranges.tree";
    unsigned char *bytes = (unsigned char *)calloc(12000u, 1u);
    size_t at = 0u;
    pe_monker_range_set_t ranges;
    uint32_t player;
    uint32_t combo;

    CHECK(bytes != NULL, "range fixture allocation failed");
    if (!bytes)
        return;
    put_i64(bytes, &at, 33487);
    put_i32(bytes, &at, 12);
    put_i32(bytes, &at, 2);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 1);
    put_money(bytes, &at, 0);
    put_money(bytes, &at, 10000);
    put_money(bytes, &at, 10000);
    put_u16(bytes, &at, 0u);   /* root is a leaf: no edges to describe */
    bytes[at++] = 1u;          /* ranges present */
    /*
     * Player-major layout: all of player 0's combos, then player 1's. None of
     * the .tree files shipped with MonkerSolver carries a range block, so this
     * ordering comes from the format notes and has never been confronted with
     * a real file. It is the one part of this reader still resting on a
     * fixture written by the same hand as the reader.
     */
    for (player = 0u; player < 2u; ++player)
        for (combo = 0u; combo < 1326u; ++combo)
            put_i32(bytes, &at, combo == player ? INT32_MAX : 0);

    CHECK(write_path_fixture(path, bytes, at) == 0,
          "range fixture write failed");
    CHECK(pe_monker_tree_read_ranges(path, &ranges) == PE_MONKER_OK,
          "fixed ranges were not decoded");
    if (ranges.players)
    {
        CHECK(ranges.player_count == 2u && ranges.combo_count == 1326u,
              "range dimensions are wrong");
        CHECK(ranges.players[0]->count == 1326u &&
                  ranges.players[0]->combos[0].weight == 1.0 &&
                  ranges.players[0]->combos[1].weight == 0.0,
              "player 0 fixed weights are wrong");
        CHECK(ranges.players[1]->combos[1].weight == 1.0,
              "player 1 fixed weight is wrong");
        {
            unsigned card_count = 0u;
            unsigned card;
            for (card = 0u; card < 52u; ++card)
                if (StdDeck_CardMask_CARD_IS_SET(
                        ranges.players[0]->combos[0].hand, card))
                    ++card_count;
            CHECK(card_count == 2u,
                  "a decoded two-card combo does not contain two cards");
        }
    }
    pe_monker_range_set_free(&ranges);
    free(bytes);
}

/*
 * Bytes MonkerSolver wrote.
 *
 * trees/test.tree from a MonkerSolver 2.1.9 installation, copied verbatim: a
 * 167-byte two-handed PLO river tree, 33 nodes, pot-sized bets. sha256
 * 9b8659c16d7cdff53bd819aa2dcdf6a4460726b6286c7d7653b01a78bfc18230.
 *
 * Embedded rather than referenced by path, because a test that reads outside
 * the repository is a test CI does not run — and this reader has already shown
 * that nothing except real bytes catches its mistakes. Every synthetic fixture
 * above agreed with a reader that could not open a single genuine file.
 */
static const unsigned char k_real_tree[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82, 0xcf, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x0f, 0xa0, 0x00, 0x03, 0x0d, 0x40, 0x00, 0x03, 0x0d, 0x40,
    0x00, 0x02, 0x9c, 0xa4, 0x00, 0x03, 0x9c, 0xa4, 0x00, 0x03, 0x9c, 0xa4,
    0x00, 0x03, 0x9c, 0xa4, 0x00, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x9c, 0xa4, 0x00, 0x03, 0x9c, 0xa4,
    0x00, 0x03, 0x9c, 0xa4, 0x00, 0x03, 0x9c, 0xa4, 0x00, 0x03, 0x00, 0x03,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};

static void test_real_monker_file(void)
{
    const char *path = "/tmp/poker_eval_monker_real.tree";
    pe_monker_tree_header_t header;
    mpf_tree_def_t *tree = NULL;
    int pot_bets = 0;
    int all_ins = 0;
    int i;

    CHECK(write_path_fixture(path, k_real_tree, sizeof(k_real_tree)) == 0,
          "real fixture write failed");

    CHECK(pe_monker_tree_read_header(path, &header) == PE_MONKER_OK,
          "a real MonkerSolver header was rejected");
    CHECK(header.signature == 33487 && header.internal_format == 1 &&
              header.player_count == 2u && header.first_to_act == 1 &&
              header.street == 3,
          "real header fields decoded wrongly");
    /* Read as doubles these came out as denormals; as int32 they are the
       numbers MonkerSolver itself shows. */
    CHECK(header.dead_money == 4000.0 && header.stacks[0] == 200000.0 &&
              header.stacks[1] == 200000.0,
          "real money fields decoded wrongly (%g, %g)",
          header.dead_money, header.stacks[0]);

    CHECK(pe_monker_tree_load(path, &tree) == PE_MONKER_OK && tree != NULL,
          "a real MonkerSolver tree did not load");
    if (!tree)
        return;
    /*
     * 33 is what an independent decode of the same bytes produces. A one-byte
     * node framing yields a different number from the same file, which is
     * exactly what went unnoticed before.
     */
    CHECK(tree->node_count == 33, "real tree has %d nodes, expected 33",
          tree->node_count);
    /*
     * The file's whole action alphabet is {0, 1, 3, 40100}: fold, call,
     * all-in, pot. Every raise must therefore decode to one of exactly two
     * sizes, and both must occur — a decode that produced only one of them
     * would mean half the tree's action codes were misread.
     */
    for (i = 0; i < tree->node_count; ++i)
    {
        int a;
        for (a = 0; a < tree->nodes[i].action_count; ++a)
        {
            int idx;
            double size;
            if (tree->nodes[i].actions[a].type != MPF_TREE_ACTION_RAISE)
                continue;
            idx = tree->nodes[i].actions[a].size_index;
            if (idx < 0 || idx >= tree->nodes[i].bet_size_count)
            {
                CHECK(0, "node %d raise has no bet size", i);
                continue;
            }
            size = tree->nodes[i].bet_sizes[idx];
            if (size == 1.0)
                pot_bets++;
            else if (size == -1.0)
                all_ins++;
            else
                CHECK(0, "node %d decoded a size of %g, which this file "
                         "has no code for", i, size);
        }
    }
    CHECK(pot_bets > 0, "no pot-sized bet survived the decode");
    CHECK(all_ins > 0, "no all-in survived the decode");
    mpf_tree_free(tree);
}

int main(void)
{
    test_header_variants();
    test_rejections();
    test_node_topology();
    test_unverified_action_codes_are_refused();
    test_fixed_ranges();
    test_real_monker_file();
    if (failures)
    {
        fprintf(stderr, "test_monker_tree: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_monker_tree: real MonkerSolver bytes decode\n");
    return 0;
}
