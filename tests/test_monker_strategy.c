/*
 * test_monker_strategy.c - a saved strategy, indexed by hand
 *
 * The import joins four things that were each tested on their own: the tree,
 * the stored slots, the slot-to-node binding, and the hand-class numbering.
 * What is tested here is the join — that a question about a hand reaches the
 * right bytes, and that the answer is a distribution.
 */

#include <poker_eval/solver/pe_monker_strategy.h>
#include <poker_eval/solver/pe_monker_omaha_tree.h>
#include <poker_eval/solver/pe_monker_tree_vector.h>
#include <poker_eval/solver/pe_best_response.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/core/modern_cardmask.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int failures;

typedef struct
{
    int terminal;
    int winning_action;
} strategy_state_t;

static strategy_state_t strategy_root = {0, -1};
static strategy_state_t strategy_win = {1, 0};
static strategy_state_t strategy_lose = {1, 1};

static int strategy_game_terminal(const void *state, void *user)
{
    (void)user;
    return ((const strategy_state_t *)state)->terminal;
}

static int strategy_game_player(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0;
}

static uint16_t strategy_game_actions(const void *state, void *user)
{
    (void)user;
    return ((const strategy_state_t *)state)->terminal ? 0u : 2u;
}

static uint64_t strategy_game_infoset(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 1u;
}

static const void *strategy_game_apply(const void *state, uint16_t action,
                                       void *user)
{
    (void)user;
    if (state != &strategy_root || action > 1u)
        return NULL;
    return action == 0u ? &strategy_win : &strategy_lose;
}

static int strategy_game_values(const void *state,
                                const pe_reach_vec_t *reach,
                                pe_value_vec_t *out_values,
                                uint8_t player_count, void *user)
{
    size_t combo;
    double value = state == &strategy_win ? 1.0 : 0.0;
    (void)reach;
    (void)user;
    if (!out_values || player_count != 2u)
        return -1;
    for (combo = 0u; combo < out_values[0].n; ++combo)
    {
        out_values[0].v[combo] = value;
        out_values[1].v[combo] = -value;
    }
    return 0;
}

static int strategy_decode_combo(const void *state, uint16_t combo,
                                 int *out_node, int out_cards[4], void *user)
{
    (void)state;
    (void)combo;
    (void)user;
    *out_node = 0;
    out_cards[0] = 0;
    out_cards[1] = 1;
    out_cards[2] = 2;
    out_cards[3] = 3;
    return 0;
}

static int tree_decode_combo(const void *state, uint16_t combo,
                             int *out_node, int out_cards[4], void *user)
{
    const pe_monker_tree_state_t *tree_state =
        (const pe_monker_tree_state_t *)state;
    const pe_monker_classes_t *classes =
        (const pe_monker_classes_t *)user;
    (void)combo;
    if (!tree_state || !out_node || !out_cards ||
        pe_monker_class_representative(classes, combo, out_cards) !=
            PE_MONKER_OK)
        return -1;
    *out_node = tree_state->node_index;
    return 0;
}

typedef struct
{
    int calls;
} tree_terminal_probe_t;

static int tree_terminal_values(int node_index, const pe_reach_vec_t *reach,
                                pe_value_vec_t *out_values,
                                uint8_t player_count, void *user)
{
    tree_terminal_probe_t *probe = (tree_terminal_probe_t *)user;
    size_t combo;
    double value = node_index == 1 ? 1.0 : -1.0;
    (void)reach;
    if (!probe || !out_values || player_count != 2u)
        return -1;
    probe->calls++;
    for (combo = 0u; combo < out_values[0].n; ++combo)
    {
        out_values[0].v[combo] = value;
        out_values[1].v[combo] = -value;
    }
    return 0;
}

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

/* ---- fixtures: a two-node tree and a strategy for it ------------------ */

static void be16(unsigned char *b, size_t *at, unsigned v)
{
    b[(*at)++] = (unsigned char)(v >> 8);
    b[(*at)++] = (unsigned char)(v & 0xFFu);
}

static void be32(unsigned char *b, size_t *at, uint32_t v)
{
    unsigned i;
    for (i = 0u; i < 4u; ++i)
        b[(*at)++] = (unsigned char)(v >> (8u * (3u - i)));
}

/*
 * root with two actions, both leading to leaves. Three nodes; the storage
 * order (children last to first) is 0, 2, 1, so the strategy has an array in
 * slot 0 and absent slots after it.
 */
static int write_tree(const char *path)
{
    unsigned char b[128];
    size_t at = 0u;
    FILE *f;
    unsigned i;
    for (i = 0u; i < 4u; ++i) b[at++] = 0u;
    be32(b, &at, 33487u);
    be32(b, &at, 1u);          /* internal format */
    be32(b, &at, 2u);          /* players */
    be32(b, &at, 0u);          /* first to act */
    be32(b, &at, 1u);          /* street 1 */
    be32(b, &at, 0u);          /* dead money */
    be32(b, &at, 10000u);
    be32(b, &at, 10000u);
    be16(b, &at, 2u);          /* root: two children */
    be16(b, &at, 0u);          /*   fold  */
    be16(b, &at, 0u);          /*     leaf */
    be16(b, &at, 3u);          /*   all in */
    be16(b, &at, 0u);          /*     leaf */
    b[at++] = 0u;              /* no ranges */
    f = fopen(path, "wb");
    if (f == NULL) return -1;
    if (fwrite(b, 1u, at, f) != at || fclose(f) != 0) return -1;
    return 0;
}

/*
 * A strategy entry: block-data bucket count, then one array of
 * 16432 * 2 bytes for the root, then two absent slots.
 *
 * Every class gets 200/56 except class 0, which gets 0/0 so the
 * "no strategy stored" path is exercised, and class 1, which gets 129/128 —
 * summing to 257, the independent-rounding case required by the format.
 */
static unsigned char *build_strategy(size_t *out_size)
{
    size_t classes = 16432u;
    size_t bytes = classes * 2u;
    size_t cap = bytes + 256u;
    unsigned char *s = (unsigned char *)malloc(cap);
    size_t at = 0u;
    size_t i;
    unsigned j;
    if (s == NULL) return NULL;
    s[at++] = 0xacu; s[at++] = 0xedu; be16(s, &at, 5u);
    s[at++] = 0x77u; s[at++] = 4u; be32(s, &at, 30u);
    s[at++] = 0x75u; s[at++] = 0x72u;
    be16(s, &at, 2u); s[at++] = '['; s[at++] = 'B';
    for (j = 0u; j < 8u; ++j) s[at++] = 0u;
    s[at++] = 0x02u; be16(s, &at, 0u); s[at++] = 0x78u; s[at++] = 0x70u;
    be32(s, &at, (uint32_t)bytes);
    for (i = 0u; i < classes; ++i)
    {
        if (i == 0u)       { s[at++] = 0u;   s[at++] = 0u;   }
        else if (i == 1u)  { s[at++] = 129u; s[at++] = 128u; }
        else               { s[at++] = 200u; s[at++] = 56u;  }
    }
    s[at++] = 0x70u;   /* slot for node 2 */
    s[at++] = 0x70u;   /* slot for node 1 */
    *out_size = at;
    return s;
}

static int put_u16(FILE *f, uint16_t v)
{
    unsigned char b[2];
    b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8);
    return fwrite(b, 1u, 2u, f) == 2u ? 0 : -1;
}

static int put_u32(FILE *f, uint32_t v)
{
    unsigned char b[4];
    b[0] = (unsigned char)v;       b[1] = (unsigned char)(v >> 8);
    b[2] = (unsigned char)(v >> 16); b[3] = (unsigned char)(v >> 24);
    return fwrite(b, 1u, 4u, f) == 4u ? 0 : -1;
}

/* UTF-16BE with a BOM, as used for serialized entry names. */
static int put_name(FILE *f, const char *name)
{
    unsigned char bom[2] = {0xFEu, 0xFFu};
    size_t i;
    if (fwrite(bom, 1u, 2u, f) != 2u)
        return -1;
    for (i = 0u; name[i] != '\0'; ++i)
    {
        unsigned char pair[2];
        pair[0] = 0u;
        pair[1] = (unsigned char)name[i];
        if (fwrite(pair, 1u, 2u, f) != 2u)
            return -1;
    }
    return 0;
}

/*
 * One stored (undeflated) ZIP entry whose payload is a zlib stream: the
 * double compression a real .mkr uses for its strategies.
 */
/*
 * The same shape, but the root's array is int[] rather than byte[]. A real
 * entry puts the strategies first and the parallel int arrays second; nothing
 * in the file says it must, so a view that assumed it would read regrets as
 * frequencies. This binds — the presence pattern is right — and must still be
 * refused.
 */
static unsigned char *build_strategy_ints(size_t *out_size)
{
    size_t classes = 16432u;
    size_t values = classes * 2u;
    unsigned char *s = (unsigned char *)malloc(values * 4u + 256u);
    size_t at = 0u;
    size_t i;
    unsigned j;
    if (s == NULL) return NULL;
    s[at++] = 0xacu; s[at++] = 0xedu; be16(s, &at, 5u);
    s[at++] = 0x77u; s[at++] = 4u; be32(s, &at, 30u);
    s[at++] = 0x75u; s[at++] = 0x72u;
    be16(s, &at, 2u); s[at++] = '['; s[at++] = 'I';
    for (j = 0u; j < 8u; ++j) s[at++] = 0u;
    s[at++] = 0x02u; be16(s, &at, 0u); s[at++] = 0x78u; s[at++] = 0x70u;
    be32(s, &at, (uint32_t)values);
    for (i = 0u; i < values; ++i)
        be32(s, &at, 7u);
    s[at++] = 0x70u;
    s[at++] = 0x70u;
    *out_size = at;
    return s;
}

static int write_archive(const char *path, const unsigned char *java,
                         size_t java_size)
{
    static const char name[] = "storedstrategy0";
    const uint16_t namelen = (uint16_t)(2u + 2u * (sizeof(name) - 1u));
    uLongf zlen = compressBound((uLong)java_size);
    unsigned char *z = (unsigned char *)malloc((size_t)zlen);
    FILE *f = NULL;
    uint32_t central;
    uint32_t csize;
    int ok = 0;

    if (z == NULL)
        goto done;
    if (compress2(z, &zlen, java, (uLong)java_size, 6) != Z_OK)
        goto done;
    f = fopen(path, "wb");
    if (f == NULL)
        goto done;
    if (put_u32(f, 0x04034b50u) || put_u16(f, 20u) || put_u16(f, 0x0008u) ||
        put_u16(f, 0u) || put_u16(f, 0u) || put_u16(f, 0u) ||
        put_u32(f, 0u) || put_u32(f, 0u) || put_u32(f, 0u) ||
        put_u16(f, namelen) || put_u16(f, 0u) || put_name(f, name) ||
        fwrite(z, 1u, (size_t)zlen, f) != (size_t)zlen ||
        put_u32(f, 0x08074b50u) || put_u32(f, 0u) ||
        put_u32(f, (uint32_t)zlen) || put_u32(f, (uint32_t)zlen))
        goto done;
    central = (uint32_t)ftell(f);
    if (put_u32(f, 0x02014b50u) || put_u16(f, 20u) || put_u16(f, 20u) ||
        put_u16(f, 0x0008u) || put_u16(f, 0u) || put_u16(f, 0u) ||
        put_u16(f, 0u) || put_u32(f, 0u) ||
        put_u32(f, (uint32_t)zlen) || put_u32(f, (uint32_t)zlen) ||
        put_u16(f, namelen) || put_u16(f, 0u) || put_u16(f, 0u) ||
        put_u16(f, 0u) || put_u16(f, 0u) || put_u32(f, 0u) ||
        put_u32(f, 0u) || put_name(f, name))
        goto done;
    csize = (uint32_t)ftell(f) - central;
    if (put_u32(f, 0x06054b50u) || put_u16(f, 0u) || put_u16(f, 0u) ||
        put_u16(f, 1u) || put_u16(f, 1u) || put_u32(f, csize) ||
        put_u32(f, central) || put_u16(f, 0u))
        goto done;
    ok = 1;
done:
    if (f != NULL && fclose(f) != 0)
        ok = 0;
    free(z);
    return ok ? 0 : -1;
}

int main(void)
{
    const char *tree_path = "/tmp/poker_eval_monker_view.tree";
    const char *mkr_path = "/tmp/poker_eval_monker_view.mkr";
    mpf_tree_def_t *tree = NULL;
    pe_monker_mkr_t archive;
    pe_monker_mkr_strategy_t stored;
    pe_monker_classes_t *classes = NULL;
    pe_monker_strategy_t *view = NULL;
    unsigned char *payload;
    size_t payload_size = 0u;

    CHECK(write_tree(tree_path) == 0, "tree fixture write failed");
    payload = build_strategy(&payload_size);
    CHECK(payload != NULL, "strategy fixture build failed");
    if (payload == NULL) return 1;
    CHECK(write_archive(mkr_path, payload, payload_size) == 0,
          "archive fixture write failed");
    free(payload);

    if (pe_monker_tree_load(tree_path, &tree) != PE_MONKER_OK || tree == NULL)
    {
        fprintf(stderr, "FAILED: fixture tree did not load\n");
        return 1;
    }
    CHECK(tree->node_count == 3, "fixture tree has %d nodes, expected 3",
          tree->node_count);
    if (pe_monker_mkr_read(mkr_path, &archive) != PE_MONKER_MKR_OK)
    {
        fprintf(stderr, "FAILED: fixture archive was rejected\n");
        mpf_tree_free(tree);
        return 1;
    }
    if (pe_monker_mkr_read_strategy(&archive, "storedstrategy0", &stored) !=
        PE_MONKER_MKR_OK)
    {
        fprintf(stderr, "FAILED: fixture strategy was not decoded\n");
        pe_monker_mkr_free(&archive);
        mpf_tree_free(tree);
        return 1;
    }
    if (pe_monker_classes_create(&classes) != PE_MONKER_OK)
    {
        fprintf(stderr, "FAILED: class table was not built\n");
        return 1;
    }
    CHECK(pe_monker_strategy_open(tree, &stored, classes, &view) ==
              PE_MONKER_OK && view != NULL,
          "the view did not open");
    if (view == NULL) return 1;
    CHECK(pe_monker_strategy_class_count(view) == 16432u,
          "view reports %u classes", pe_monker_strategy_class_count(view));

    /* 2s3s4s5s is class 0, which the fixture stores as all zero. */
    {
        int cards[4] = {0, 1, 2, 3};
        double p[4] = {0.0, 0.0, 0.0, 0.0};
        uint16_t n = 0u;
        int specified = 1;
        CHECK(pe_monker_strategy_probs(view, tree->root_index, cards, p,
                                       4u, &n, &specified) == PE_MONKER_OK,
              "class 0 could not be read");
        CHECK(n == 2u, "root has %u actions, expected 2", n);
        CHECK(specified == 0, "an all-zero class was reported as specified");
        CHECK(fabs(p[0] - 0.5) < 1e-12 && fabs(p[1] - 0.5) < 1e-12,
              "an unstored class gave %.6f/%.6f, expected uniform", p[0], p[1]);
    }

    /* 2s3s4s6s is class 1: 129/128, which sums to 257 and must still
       normalise to one. */
    {
        int cards[4] = {0, 1, 2, 4};
        double p[4] = {0.0, 0.0, 0.0, 0.0};
        uint16_t n = 0u;
        int specified = 0;
        uint32_t k = 99u;
        CHECK(pe_monker_class_of(classes, cards, &k) == PE_MONKER_OK && k == 1u,
              "2s3s4s6s is class %u, expected 1", k);
        CHECK(pe_monker_strategy_probs(view, tree->root_index, cards, p,
                                       4u, &n, &specified) == PE_MONKER_OK,
              "class 1 could not be read");
        CHECK(specified == 1, "a stored class was reported as unstored");
        CHECK(fabs(p[0] + p[1] - 1.0) < 1e-12,
              "129/128 normalised to %.12f, not 1", p[0] + p[1]);
        CHECK(fabs(p[0] - 129.0 / 257.0) < 1e-12,
              "129 of 257 became %.12f", p[0]);
    }

    /* Any other class is 200/56, and suit relabelling must not change it. */
    {
        int a[4] = {0, 1, 2, 5};
        int b[4] = {13, 14, 15, 18};
        double pa[4] = {0}, pb[4] = {0};
        uint16_t na = 0u, nb = 0u;
        pe_monker_strategy_probs(view, tree->root_index, a, pa, 4u, &na, NULL);
        pe_monker_strategy_probs(view, tree->root_index, b, pb, 4u, &nb, NULL);
        CHECK(fabs(pa[0] - 200.0 / 256.0) < 1e-12,
              "200 of 256 became %.12f", pa[0]);
        CHECK(fabs(pa[0] - pb[0]) < 1e-15 && fabs(pa[1] - pb[1]) < 1e-15,
              "the same hand in other suits gave %.12f and %.12f", pa[0], pb[0]);
    }

    /* Terminals have no strategy, and asking for one is an error rather than
       a zero-length answer. */
    {
        double p[4] = {0};
        uint16_t n = 0u;
        int leaf = -1;
        int i;
        for (i = 0; i < tree->node_count; ++i)
            if (tree->nodes[i].action_count == 0)
            {
                leaf = i;
                break;
            }
        CHECK(leaf >= 0, "the fixture tree has no terminal");
        if (leaf >= 0)
            CHECK(pe_monker_strategy_probs(view, leaf, (int[]){0,1,2,3}, p,
                                           4u, &n, NULL) != PE_MONKER_OK,
                  "a terminal node answered with a strategy");
    }

    /* A buffer too small for the node's actions is refused, not overrun. */
    {
        int cards[4] = {0, 1, 2, 5};
        double p[1] = {0.0};
        uint16_t n = 0u;
        CHECK(pe_monker_strategy_probs(view, tree->root_index, cards, p,
                                       1u, &n, NULL) != PE_MONKER_OK,
              "a one-slot buffer was accepted for a two-action node");
    }

    /* The imported policy is evaluated through the vector best-response
       engine, not by comparing its action frequencies. Every combo is class
       0 (the fixture's unstored/uniform class), so action 0 wins half the
       time against an always-action-0 opponent. */
    {
        pe_vector_game_t base = {0};
        pe_monker_strategy_game_t adapted = {0};
        pe_best_response_vector_config_t br_config =
            pe_best_response_vector_config_default();
        pe_exploitability_vector_result_t result = {0};
        base.root = &strategy_root;
        base.player_count = 2u;
        base.combo_count = 16432u;
        base.is_terminal = strategy_game_terminal;
        base.acting_player = strategy_game_player;
        base.action_count = strategy_game_actions;
        base.infoset_key = strategy_game_infoset;
        base.apply_action = strategy_game_apply;
        base.terminal_values = strategy_game_values;
        CHECK(pe_monker_strategy_vector_game_init(
                  &adapted, &base, view, strategy_decode_combo, NULL) ==
              PE_MONKER_OK, "vector game adapter did not initialise");
        CHECK(pe_exploitability_vector(&adapted.game, &br_config, &result) ==
                  PE_SOLVER_OK, "vector exploitability did not run");
        CHECK(fabs(result.policy_value[0] - 0.5) < 1e-12 &&
                  fabs(result.policy_value[1] + 0.5) < 1e-12,
              "policy values were %.12f/%.12f, expected 0.5/-0.5",
              result.policy_value[0], result.policy_value[1]);
        CHECK(fabs(result.br_gap[0] - 0.5) < 1e-12 &&
                  fabs(result.br_gap[1]) < 1e-12 &&
                  fabs(result.exploitability_raw - 0.5) < 1e-12,
              "exploitability was %.12f with gaps %.12f/%.12f",
              result.exploitability_raw, result.br_gap[0], result.br_gap[1]);
    }

    /* The actual loaded tree can now be used as the vector game's topology.
     * The Monker wrapper supplies one policy vector per node/action and the
     * full traversal reaches both leaves before the terminal callback. */
    {
        pe_monker_tree_vector_t tree_game = {0};
        pe_monker_strategy_game_t adapted = {0};
        pe_traversal_ctx_t traversal = {0};
        pe_update_batch_t batch = {0};
        tree_terminal_probe_t probe = {0};
        const pe_traversal_ops_t *ops = pe_traversal_full_vector_ops();
        CHECK(pe_monker_tree_vector_init(&tree_game, tree, 2u, 16432u,
                                         tree_terminal_values, &probe) == 0,
              "tree vector adapter did not initialise");
        CHECK(pe_monker_strategy_vector_game_init(
                  &adapted, &tree_game.game, view, tree_decode_combo, classes) ==
                  PE_MONKER_OK,
              "loaded tree could not be wrapped with its strategy");
        CHECK(pe_traversal_ctx_init(&traversal, &adapted.game) == 0,
              "tree traversal context did not initialise");
        if (traversal.initialized)
        {
            CHECK(ops->begin_iteration(&traversal, 1u) == 0,
                  "tree traversal begin failed");
            CHECK(ops->run_iteration(&traversal, &batch) == 0,
                  "tree traversal failed");
            CHECK(traversal.visited_nodes == 3u &&
                      traversal.terminal_nodes == 2u,
                  "tree traversal visited %zu/%zu nodes, expected 3/2",
                  traversal.visited_nodes, traversal.terminal_nodes);
            CHECK(probe.calls == 2,
                  "terminal callback called %d times, expected 2",
                  probe.calls);
        }
        pe_update_batch_destroy(&batch);
        pe_traversal_ctx_destroy(&traversal);
        pe_monker_tree_vector_destroy(&tree_game);
    }

    /* Explicit chance nodes are composed by the topology adapter and the
     * full vector traversal. They are not player actions and therefore must
     * not ask the Monker strategy for an infoset or an action frequency. */
    {
        mpf_tree_def_t chance_tree = {0};
        mpf_tree_node_t chance_nodes[3];
        mpf_tree_action_t chance_actions[2];
        pe_monker_tree_vector_t chance_game = {0};
        pe_traversal_ctx_t chance_traversal = {0};
        pe_update_batch_t chance_batch = {0};
        tree_terminal_probe_t chance_probe = {0};
        const pe_traversal_ops_t *ops = pe_traversal_full_vector_ops();

        memset(chance_nodes, 0, sizeof(chance_nodes));
        memset(chance_actions, 0, sizeof(chance_actions));
        chance_tree.node_count = 3;
        chance_tree.nodes = chance_nodes;
        chance_tree.root_index = 0;
        chance_nodes[0].type = MPF_TREE_NODE_CHANCE;
        chance_nodes[0].actions = chance_actions;
        chance_nodes[0].action_count = 2;
        chance_actions[0].type = MPF_TREE_ACTION_CHANCE;
        chance_actions[0].weight = 0.25;
        chance_actions[0].next_index = 1;
        chance_actions[1].type = MPF_TREE_ACTION_CHANCE;
        chance_actions[1].weight = 0.75;
        chance_actions[1].next_index = 2;
        chance_nodes[1].type = MPF_TREE_NODE_TERMINAL;
        chance_nodes[2].type = MPF_TREE_NODE_TERMINAL;
        CHECK(pe_monker_tree_vector_init(&chance_game, &chance_tree, 2u, 1u,
                                         tree_terminal_values,
                                         &chance_probe) == 0,
              "chance tree vector adapter did not initialise");
        CHECK(pe_traversal_ctx_init(&chance_traversal, &chance_game.game) == 0,
              "chance traversal context did not initialise");
        if (chance_traversal.initialized)
        {
            CHECK(ops->begin_iteration(&chance_traversal, 2u) == 0,
                  "chance traversal begin failed");
            CHECK(ops->run_iteration(&chance_traversal, &chance_batch) == 0,
                  "chance traversal failed");
            CHECK(chance_traversal.visited_nodes == 3u &&
                      chance_traversal.terminal_nodes == 2u,
                  "chance traversal visited %zu/%zu nodes, expected 3/2",
                  chance_traversal.visited_nodes,
                  chance_traversal.terminal_nodes);
        }
        pe_update_batch_destroy(&chance_batch);
        pe_traversal_ctx_destroy(&chance_traversal);
        pe_monker_tree_vector_destroy(&chance_game);
    }

    /* A single chance transition with an omitted JSON weight is a valid
     * topological step and therefore has unit probability. */
    {
        mpf_tree_def_t single_chance_tree = {0};
        mpf_tree_node_t single_chance_nodes[2];
        mpf_tree_action_t single_chance_action;
        pe_monker_tree_vector_t single_chance_game = {0};
        pe_traversal_ctx_t single_chance_traversal = {0};
        pe_update_batch_t single_chance_batch = {0};
        tree_terminal_probe_t single_chance_probe = {0};
        const pe_traversal_ops_t *ops = pe_traversal_full_vector_ops();

        memset(single_chance_nodes, 0, sizeof(single_chance_nodes));
        memset(&single_chance_action, 0, sizeof(single_chance_action));
        single_chance_tree.node_count = 2;
        single_chance_tree.nodes = single_chance_nodes;
        single_chance_tree.root_index = 0;
        single_chance_nodes[0].type = MPF_TREE_NODE_CHANCE;
        single_chance_nodes[0].actions = &single_chance_action;
        single_chance_nodes[0].action_count = 1;
        single_chance_action.type = MPF_TREE_ACTION_CHANCE;
        single_chance_action.next_index = 1;
        single_chance_nodes[1].type = MPF_TREE_NODE_TERMINAL;
        CHECK(pe_monker_tree_vector_init(&single_chance_game,
                                         &single_chance_tree, 2u, 1u,
                                         tree_terminal_values,
                                         &single_chance_probe) == 0,
              "single chance tree adapter did not initialise");
        CHECK(pe_traversal_ctx_init(&single_chance_traversal,
                                    &single_chance_game.game) == 0,
              "single chance traversal context did not initialise");
        if (single_chance_traversal.initialized)
        {
            CHECK(ops->begin_iteration(&single_chance_traversal, 3u) == 0,
                  "single chance traversal begin failed");
            CHECK(ops->run_iteration(&single_chance_traversal,
                                     &single_chance_batch) == 0,
                  "single chance traversal failed");
            CHECK(single_chance_traversal.visited_nodes == 2u &&
                      single_chance_traversal.terminal_nodes == 1u,
                  "single chance traversal visited %zu/%zu nodes, expected 2/1",
                  single_chance_traversal.visited_nodes,
                  single_chance_traversal.terminal_nodes);
        }
        pe_update_batch_destroy(&single_chance_batch);
        pe_traversal_ctx_destroy(&single_chance_traversal);
        pe_monker_tree_vector_destroy(&single_chance_game);
    }

    /* A concrete PLO4 deal is now valued after the imported policy is
     * applied. The fixture folds 200/256 and reaches showdown 56/256; the
     * selected hand wins at showdown, so net EV is -200/256 + 56/256. */
    {
        EvalConfig config = eval_config_holdem();
        EvalContext *context = eval_context_create(&config);
        pe_omaha_combo_t combos[2];
        pe_omaha_range_t ranges[2];
        pe_betting_state_t state;
        pe_monker_omaha_tree_spec_t spec;
        mask_t board = string_to_mask("2c 7d Th Js Qc");
        double values[2] = {0.0, 0.0};
        double path_weight = 0.0;
        size_t deals = 0u;
        double weight = 0.0;
        memset(&state, 0, sizeof(state));
        memset(&spec, 0, sizeof(spec));
        combos[0].cards = string_to_mask("As Ks Qd 3c");
        combos[0].weight = 1.0;
        combos[1].cards = string_to_mask("9c 8d 5c 6c");
        combos[1].weight = 1.0;
        ranges[0].combos = &combos[0];
        ranges[0].count = 1u;
        ranges[1].combos = &combos[1];
        ranges[1].count = 1u;
        state.player_count = 2u;
        state.pot = 2.0;
        state.invested[0] = 1.0;
        state.invested[1] = 1.0;
        state.stack[0] = 9999.0;
        state.stack[1] = 9999.0;
        state.round_contrib[0] = 1.0;
        state.round_contrib[1] = 1.0;
        state.to_call = 1.0;
        state.current_bet = 1.0;
        state.min_raise = 1.0;
        state.active[0] = 1;
        state.active[1] = 1;
        state.winner = -1;
        spec.context = context;
        spec.board = board;
        spec.ranges = ranges;
        spec.state = &state;
        spec.player_count = 2u;
        spec.hole_cards = 4u;
        spec.tree = tree;
        spec.strategy = view;
        spec.classes = classes;
        CHECK(context != NULL &&
                  pe_monker_omaha_tree_values(&spec, values, &deals, &weight,
                                              &path_weight) == 0,
              "weighted Omaha tree evaluation failed");
        CHECK(deals == 1u && fabs(weight - 1.0) < 1e-12 &&
                  fabs(path_weight - 1.0) < 1e-12,
              "weighted deal accounting was %zu/%.12f/%.12f", deals, weight,
              path_weight);
        CHECK(fabs(values[0] + 0.5625) < 1e-12 &&
                  fabs(values[1] - 0.5625) < 1e-12,
              "weighted EV was %.12f/%.12f, expected -0.5625/0.5625",
              values[0], values[1]);
        eval_context_destroy(context);
    }

    pe_monker_strategy_close(view);
    pe_monker_mkr_strategy_free(&stored);
    pe_monker_mkr_free(&archive);

    /* An entry whose decision node carries an int array instead of a byte
       array binds, and must not open as a strategy. */
    {
        const char *ints_path = "/tmp/poker_eval_monker_view_ints.mkr";
        unsigned char *ints;
        size_t ints_size = 0u;
        pe_monker_mkr_t ints_archive;
        pe_monker_mkr_strategy_t ints_stored;
        pe_monker_strategy_t *ints_view = NULL;
        int32_t map[8];

        ints = build_strategy_ints(&ints_size);
        CHECK(ints != NULL, "int-array fixture build failed");
        if (ints != NULL)
        {
            CHECK(write_archive(ints_path, ints, ints_size) == 0,
                  "int-array archive write failed");
            free(ints);
            if (pe_monker_mkr_read(ints_path, &ints_archive) ==
                    PE_MONKER_MKR_OK &&
                pe_monker_mkr_read_strategy(&ints_archive, "storedstrategy0",
                                            &ints_stored) == PE_MONKER_MKR_OK)
            {
                CHECK(pe_monker_mkr_bind_strategy(tree, &ints_stored, map,
                                                  sizeof(map) / sizeof(map[0]))
                          == PE_MONKER_MKR_OK,
                      "the int-array entry should bind: its shape is right");
                CHECK(pe_monker_strategy_open(tree, &ints_stored, classes,
                                              &ints_view) != PE_MONKER_OK,
                      "an int array was opened as a strategy");
                pe_monker_strategy_close(ints_view);
                pe_monker_mkr_strategy_free(&ints_stored);
                pe_monker_mkr_free(&ints_archive);
            }
            else
            {
                CHECK(0, "int-array archive was not readable");
            }
        }
    }

    pe_monker_classes_destroy(classes);
    mpf_tree_free(tree);

    if (failures)
    {
        fprintf(stderr, "test_monker_strategy: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_monker_strategy: a saved strategy answers by hand\n");
    return 0;
}
