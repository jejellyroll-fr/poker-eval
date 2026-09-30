/* test_preflop_monker_multiway.c - native Monker preflop trees keep their seats
 *
 * A native Monker multiway preflop tree is numbered Monker's way: seat 1 posts
 * the small blind, seat 2 the big blind, and the opener is the last seat
 * (first_to_act in the header).  The classic Lane B root posts SB = 0, BB = 1
 * and opens on seat 2, so at the root the betting state waited on seat 2 while
 * the tree acted for seat 3, the node offered no action, and the first sampled
 * traversal failed (issue #273).
 *
 * The tree is now played in its own numbering: the header's committed[]
 * posts the blinds and first_to_act opens.  Covered here:
 *   1. the header converts to per-seat posts and stacks in big blinds;
 *   2. the classic root still fails on that tree (the regression pinned);
 *   3. the Monker-seated root solves it and reaches every decision node, each
 *      one acted by the seat the tree names;
 *   4. heads-up, where both numberings agree, the header-seated root solves
 *      exactly the same game as the classic one.
 */

#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/solver/pe_preflop_allin_game.h>
#include <poker_eval/solver/pe_rng.h>
#include <poker_eval/solver/pe_storage.h>

#include <math.h>
#include <stdint.h>
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

/* 169 preflop hand classes per decision node. */
#define HOLDEM_CLASSES 169

static const char *test_tmp_path(const char *name)
{
    static char path[512];
    const char *directory = getenv("PE_TEST_TMPDIR");
    int written;
    if (!directory || !*directory)
        directory = ".";
    written = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (written < 0 || (size_t)written >= sizeof(path))
        return NULL;
    return path;
}

/* Big-endian, like the format. */
static void put_i32(unsigned char *buffer, size_t *at, int32_t value)
{
    uint32_t u = (uint32_t)value;
    for (unsigned i = 0u; i < 4u; ++i)
        buffer[(*at)++] = (unsigned char)(u >> (8u * (3u - i)));
}

static void put_i64(unsigned char *buffer, size_t *at, int64_t value)
{
    uint64_t u = (uint64_t)value;
    for (unsigned i = 0u; i < 8u; ++i)
        buffer[(*at)++] = (unsigned char)(u >> (8u * (7u - i)));
}

static void put_u16(unsigned char *buffer, size_t *at, unsigned value)
{
    buffer[(*at)++] = (unsigned char)(value >> 8u);
    buffer[(*at)++] = (unsigned char)(value & 0xFFu);
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

/* Push/fold, in the order the seats act.  Unopened: fold (0) or all-in (3);
 * facing the shove: fold (0) or call (1).  A node ends the hand once one
 * player is left or every seat has acted. */
static void put_push_fold_node(unsigned char *bytes, size_t *at,
                               int players, int acted, int live, int shoved)
{
    int remaining_live;

    put_u16(bytes, at, 2u);
    for (int action = 0; action < 2; ++action)
    {
        int folds = action == 0;
        put_u16(bytes, at, folds ? 0u : shoved ? 1u : 3u);
        remaining_live = live - folds;
        if (remaining_live <= 1 || acted + 1 == players)
            put_u16(bytes, at, 0u);
        else
            put_push_fold_node(bytes, at, players, acted + 1, remaining_live,
                               shoved || !folds);
    }
}

/* Monker header: signature, format, players, first to act, street, committed
 * per seat (street 0 only), dead money, stacks per seat. */
static size_t build_push_fold_tree(unsigned char *bytes, int players,
                                   int first_to_act, const int32_t *committed,
                                   int32_t stack)
{
    size_t at = 0u;
    put_i64(bytes, &at, 33487);
    put_i32(bytes, &at, 1);
    put_i32(bytes, &at, players);
    put_i32(bytes, &at, first_to_act);
    put_i32(bytes, &at, 0);
    for (int p = 0; p < players; ++p)
        put_i32(bytes, &at, committed[p]);
    put_i32(bytes, &at, 0);
    for (int p = 0; p < players; ++p)
        put_i32(bytes, &at, stack);
    /* Preorder: the root writes only its child count, every other node is
     * preceded by the action code of the edge that reaches it. */
    put_push_fold_node(bytes, &at, players, 0, players, 0);
    bytes[at++] = 0u; /* no ranges block */
    return at;
}

static int load_tree(const char *name, const unsigned char *bytes,
                     size_t length, pe_monker_tree_header_t *header,
                     mpf_tree_def_t **tree)
{
    const char *path = test_tmp_path(name);
    *tree = NULL;
    if (!path || write_path_fixture(path, bytes, length) != 0)
        return -1;
    if (pe_monker_tree_read_header(path, header) != PE_MONKER_OK ||
        pe_monker_tree_load(path, tree) != PE_MONKER_OK || !*tree)
        return -1;
    return 0;
}

static void base_rules(pe_preflop_allin_rules_t *rules, int players,
                       const mpf_tree_def_t *tree)
{
    memset(rules, 0, sizeof(*rules));
    rules->variant = PE_PREFLOP_HOLDEM;
    rules->player_count = players;
    for (int p = 0; p < players; ++p)
        rules->stacks[p] = 5.0;
    rules->small_blind = 0.5;
    rules->big_blind = 1.0;
    rules->min_raise = 1.0;
    rules->allow_nonallin_call = 1;
    rules->showdown_samples = 4;
    rules->showdown_seed = 0x273u;
    rules->tree = tree;
    rules->tree_showdown = 1;
    rules->complete_ranges = 1;
    rules->root_to_act = -1;
}

/* Plain external-sampling MCCFR; -1 when a traversal fails. */
static int run_solve(pe_preflop_allin_game_t *game, int iterations,
                     uint64_t seed, pe_storage_t **out_storage)
{
    pe_storage_t *storage = pe_storage_create(64);
    pe_external_sampling_ctx_t ctx[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    pe_update_batch_t batch = {0};
    const pe_external_game_t *external = pe_preflop_allin_external(game);
    int players = pe_preflop_allin_player_count(game);
    int initialised = 0;
    int failed = 0;

    *out_storage = NULL;
    if (!storage)
        return -1;
    pe_preflop_allin_game_set_storage(game, storage);
    for (int player = 0; player < players; ++player)
    {
        if (pe_external_sampling_ctx_init(&ctx[player], external,
                                          pe_storage_ram_ops(), storage,
                                          player, seed + (uint64_t)player) != 0)
        {
            failed = 1;
            break;
        }
        ++initialised;
    }
    for (int iteration = 0; iteration < iterations && !failed; ++iteration)
    {
        for (int player = 0; player < players && !failed; ++player)
        {
            if (pe_external_sampling_run(&ctx[player], &batch) != 0)
            {
                failed = 1;
                break;
            }
            for (size_t i = 0u; i < batch.count; ++i)
            {
                double *regret = pe_storage_values(
                    storage, batch.items[i].infoset, PE_VALUES_REGRET);
                double *average = pe_storage_values(
                    storage, batch.items[i].infoset, PE_VALUES_AVERAGE);
                if (!regret || !average)
                {
                    failed = 1;
                    break;
                }
                regret[batch.items[i].action] += batch.items[i].delta;
                average[batch.items[i].action] += batch.items[i].average_delta;
            }
            pe_update_batch_clear(&batch);
        }
    }
    pe_update_batch_destroy(&batch);
    for (int player = 0; player < initialised; ++player)
        pe_external_sampling_ctx_destroy(&ctx[player]);
    pe_preflop_allin_game_set_storage(game, NULL);
    if (failed)
    {
        pe_storage_destroy(storage);
        return -1;
    }
    *out_storage = storage;
    return 0;
}

static int count_decision_nodes(const mpf_tree_def_t *tree)
{
    int count = 0;
    for (int n = 0; n < tree->node_count; ++n)
        count += tree->nodes[n].type == MPF_TREE_NODE_PLAYER;
    return count;
}

/* The 4-handed push/fold tree MonkerSolver 2.1.9 writes: SB on seat 1, BB on
 * seat 2, UTG (seat 3) opens.  5 BB stacks at 2000 file units per BB. */
static void test_four_handed_monker_seats(void)
{
    static const int32_t committed[4] = {0, 1000, 2000, 0};
    unsigned char bytes[512];
    size_t length = build_push_fold_tree(bytes, 4, 3, committed, 10000);
    pe_monker_tree_header_t header;
    mpf_tree_def_t *tree = NULL;
    double posts[PE_MONKER_MAX_PLAYERS];
    double stacks[PE_MONKER_MAX_PLAYERS];
    double dead = -1.0;
    pe_preflop_allin_rules_t rules;
    pe_preflop_allin_game_t *game;
    pe_storage_t *storage = NULL;
    int reached[64] = {0};
    int decision_nodes;
    int reached_nodes = 0;

    CHECK(load_tree("poker_eval_monker_pf4.tree", bytes, length, &header,
                    &tree) == 0,
          "4-handed push/fold fixture did not load");
    if (!tree)
        return;
    decision_nodes = count_decision_nodes(tree);
    CHECK(decision_nodes == 14,
          "4-handed push/fold has %d decision nodes, want 14", decision_nodes);
    CHECK(tree->nodes[0].acting_player == 3,
          "the root acts for seat %d, want the header's first_to_act (3)",
          tree->nodes[0].acting_player);

    /* 1. Header -> posts and stacks in big blinds, seats untouched. */
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_OK,
          "preflop header did not convert");
    CHECK(posts[0] == 0.0 && posts[1] == 0.5 && posts[2] == 1.0 &&
              posts[3] == 0.0,
          "posts %g,%g,%g,%g, want 0,0.5,1,0",
          posts[0], posts[1], posts[2], posts[3]);
    CHECK(dead == 0.0, "dead money %g, want 0", dead);
    for (int p = 0; p < 4; ++p)
        CHECK(stacks[p] == 5.0, "seat %d stack %g, want 5", p, stacks[p]);

    /* 2. The classic root (SB = 0, BB = 1, seat 2 opens) cannot play it. */
    base_rules(&rules, 4, tree);
    game = pe_preflop_allin_game_create(&rules, NULL);
    CHECK(game != NULL, "classic 4-handed root was not created");
    if (game)
    {
        pe_preflop_root_view_t root;
        CHECK(pe_preflop_allin_root_view(game, &root) == 0 &&
                  root.first_to_act == 2 && root.posts[0] == 0.5 &&
                  root.posts[1] == 1.0 && fabs(root.pot - 1.5) < 1e-12 &&
                  root.min_raise == 1.0,
              "classic root view: opener %d posts %g,%g pot %g",
              root.first_to_act, root.posts[0], root.posts[1], root.pot);
        CHECK(run_solve(game, 1, 0x273u, &storage) != 0,
              "the classic root solved a tree whose root seat 2 never acts");
        pe_storage_destroy(storage);
        storage = NULL;
        pe_preflop_allin_game_destroy(game);
    }

    /* 3. Played in Monker's numbering. */
    base_rules(&rules, 4, tree);
    rules.has_root_posts = 1;
    rules.tree_actors_from_betting = 1;
    rules.root_to_act = header.first_to_act;
    for (int p = 0; p < 4; ++p)
    {
        rules.root_posts[p] = posts[p];
        rules.stacks[p] = stacks[p];
    }
    game = pe_preflop_allin_game_create(&rules, NULL);
    CHECK(game != NULL, "Monker-seated 4-handed root was not created");
    if (!game)
    {
        mpf_tree_free(tree);
        return;
    }
    CHECK(run_solve(game, 3000, 0x273u, &storage) == 0,
          "the Monker-seated root failed a traversal");
    if (storage)
    {
        size_t infosets = pe_storage_count(storage);
        CHECK(infosets > (size_t)HOLDEM_CLASSES &&
                  infosets <= (size_t)(14 * HOLDEM_CLASSES),
              "%zu infosets, want (169, 2366]", infosets);
    }
    for (size_t i = 0u; i < pe_preflop_allin_infodesc_count(game); ++i)
    {
        pe_preflop_infodesc_view_t view;
        int node;
        if (pe_preflop_allin_infodesc_view_at(game, i, &view) != 0)
            continue;
        node = view.tree_node_index;
        CHECK(node >= 0 && node < tree->node_count &&
                  node < (int)(sizeof(reached) / sizeof(reached[0])),
              "decision off the tree (node %d)", node);
        if (node < 0 || node >= tree->node_count ||
            node >= (int)(sizeof(reached) / sizeof(reached[0])))
            continue;
        CHECK(view.actor == tree->nodes[node].acting_player,
              "node %d acted by seat %d, the tree names seat %d",
              node, view.actor, tree->nodes[node].acting_player);
        CHECK(view.action_count == 2u, "node %d offered %u actions, want 2",
              node, (unsigned)view.action_count);
        if (node == 0)
            CHECK(fabs(view.pot - 1.5) < 1e-9 && fabs(view.to_call - 1.0) < 1e-9,
                  "root pot %g to_call %g, want 1.5 and 1",
                  view.pot, view.to_call);
        if (!reached[node])
            ++reached_nodes;
        reached[node] = 1;
    }
    CHECK(reached_nodes == decision_nodes,
          "reached %d of %d decision nodes", reached_nodes, decision_nodes);

    pe_storage_destroy(storage);
    pe_preflop_allin_game_destroy(game);
    mpf_tree_free(tree);
    printf("  4-handed Monker seats: OK (%d/%d decision nodes)\n",
           reached_nodes, decision_nodes);
}

/* Heads-up the numberings agree (SB = 0 opens, BB = 1): seating from the
 * header must solve exactly the classic game. */
static void test_heads_up_unchanged(void)
{
    static const int32_t committed[2] = {1000, 2000};
    unsigned char bytes[256];
    size_t length = build_push_fold_tree(bytes, 2, 0, committed, 10000);
    pe_monker_tree_header_t header;
    mpf_tree_def_t *tree = NULL;
    double posts[PE_MONKER_MAX_PLAYERS];
    double stacks[PE_MONKER_MAX_PLAYERS];
    double dead = -1.0;
    pe_preflop_allin_rules_t classic;
    pe_preflop_allin_rules_t seated;
    pe_preflop_allin_game_t *classic_game;
    pe_preflop_allin_game_t *seated_game;
    pe_storage_t *classic_storage = NULL;
    pe_storage_t *seated_storage = NULL;

    CHECK(load_tree("poker_eval_monker_pf2.tree", bytes, length, &header,
                    &tree) == 0,
          "heads-up push/fold fixture did not load");
    if (!tree)
        return;
    CHECK(count_decision_nodes(tree) == 2, "heads-up push/fold is not 2 nodes");
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_OK && posts[0] == 0.5 && posts[1] == 1.0,
          "heads-up header did not convert to 0.5/1");

    base_rules(&classic, 2, tree);
    seated = classic;
    seated.has_root_posts = 1;
    seated.root_to_act = header.first_to_act;
    seated.root_posts[0] = posts[0];
    seated.root_posts[1] = posts[1];
    classic_game = pe_preflop_allin_game_create(&classic, NULL);
    seated_game = pe_preflop_allin_game_create(&seated, NULL);
    CHECK(classic_game && seated_game, "heads-up games were not created");
    if (classic_game && seated_game)
    {
        CHECK(run_solve(classic_game, 500, 0xAB2u, &classic_storage) == 0 &&
                  run_solve(seated_game, 500, 0xAB2u, &seated_storage) == 0,
              "a heads-up solve failed");
    }
    if (classic_storage && seated_storage)
    {
        size_t count = pe_storage_count(classic_storage);
        CHECK(count == pe_storage_count(seated_storage),
              "heads-up infosets differ: %zu vs %zu", count,
              pe_storage_count(seated_storage));
        for (size_t id = 0u; id < count && id < pe_storage_count(seated_storage);
             ++id)
        {
            const pe_infoset_meta_t *a =
                pe_storage_meta(classic_storage, (pe_infoset_id_t)id);
            const pe_infoset_meta_t *b =
                pe_storage_meta(seated_storage, (pe_infoset_id_t)id);
            const double *ra = pe_storage_values(
                classic_storage, (pe_infoset_id_t)id, PE_VALUES_REGRET);
            const double *rb = pe_storage_values(
                seated_storage, (pe_infoset_id_t)id, PE_VALUES_REGRET);
            if (!a || !b || !ra || !rb || a->action_count != b->action_count)
            {
                CHECK(0, "heads-up infoset %zu differs in shape", id);
                continue;
            }
            for (uint16_t k = 0u; k < a->action_count; ++k)
                CHECK(ra[k] == rb[k], "heads-up infoset %zu regret %u: %g vs %g",
                      id, (unsigned)k, ra[k], rb[k]);
        }
    }
    pe_storage_destroy(classic_storage);
    pe_storage_destroy(seated_storage);
    pe_preflop_allin_game_destroy(classic_game);
    pe_preflop_allin_game_destroy(seated_game);
    mpf_tree_free(tree);
    printf("  heads-up header seats: OK (same game as the classic root)\n");
}

/* The scale is anchored on the big blind seat, not on the largest post, and
 * the ante (the smallest post at the table) is not part of the blind. */
static void test_header_scaling(void)
{
    pe_monker_tree_header_t header;
    double posts[PE_MONKER_MAX_PLAYERS];
    double stacks[PE_MONKER_MAX_PLAYERS];
    double dead;

    /* UTG straddle: 0 / SB / BB / 2 BB. */
    memset(&header, 0, sizeof(header));
    header.player_count = 4u;
    header.first_to_act = 0;
    header.committed[1] = 1000.0;
    header.committed[2] = 2000.0;
    header.committed[3] = 4000.0;
    header.dead_money = 1000.0;
    for (int p = 0; p < 4; ++p)
        header.stacks[p] = 200000.0;
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_OK,
          "straddled header did not convert");
    CHECK(posts[0] == 0.0 && posts[1] == 0.5 && posts[2] == 1.0 &&
              posts[3] == 2.0 && stacks[0] == 100.0 && dead == 0.5,
          "straddle read as %g,%g,%g,%g stack %g dead %g; want 0,0.5,1,2 "
          "stack 100 dead 0.5",
          posts[0], posts[1], posts[2], posts[3], stacks[0], dead);

    /* A 100-unit ante on every seat does not inflate the blind. */
    header.committed[0] = 100.0;
    header.committed[1] = 1100.0;
    header.committed[2] = 2100.0;
    header.committed[3] = 100.0;
    header.dead_money = 0.0;
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_OK &&
              fabs(posts[0] - 0.05) < 1e-12 && fabs(posts[1] - 0.55) < 1e-12 &&
              fabs(posts[2] - 1.05) < 1e-12 && fabs(posts[3] - 0.05) < 1e-12,
          "ante header read as %g,%g,%g,%g; want 0.05,0.55,1.05,0.05",
          posts[0], posts[1], posts[2], posts[3]);

    /* Seat 0 is all in for 50, less than the 100 ante: its post is capped
     * and says nothing about the ante, so the blind stays 2100 - 100. */
    header.committed[0] = 50.0;
    header.stacks[0] = 50.0;
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_OK &&
              fabs(posts[0] - 0.025) < 1e-12 && fabs(posts[2] - 1.05) < 1e-12 &&
              fabs(stacks[0] - 0.025) < 1e-12 && fabs(stacks[1] - 100.0) < 1e-12,
          "short all-in ante read as %g,%g,%g,%g stack %g; want blind 2000",
          posts[0], posts[1], posts[2], posts[3], stacks[1]);

    /* A big blind all in from its post leaves no blind to anchor on. */
    header.stacks[2] = header.committed[2];
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_ERR_INVALID_HEADER,
          "a header whose big blind is all in from its post was seated");
}

/* Dead money sits in the root pot and goes to whoever wins it. */
static void test_dead_money_in_pot(void)
{
    static const int32_t committed[4] = {0, 1000, 2000, 0};
    unsigned char bytes[512];
    size_t length = build_push_fold_tree(bytes, 4, 3, committed, 10000);
    pe_monker_tree_header_t header;
    mpf_tree_def_t *tree = NULL;
    pe_preflop_allin_rules_t rules;
    pe_preflop_allin_game_t *game;

    CHECK(load_tree("poker_eval_monker_pf4_dead.tree", bytes, length, &header,
                    &tree) == 0,
          "dead-money fixture did not load");
    if (!tree)
        return;
    base_rules(&rules, 4, tree);
    rules.has_root_posts = 1;
    rules.tree_actors_from_betting = 1;
    rules.root_to_act = 3;
    rules.root_posts[1] = 0.5;
    rules.root_posts[2] = 1.0;
    rules.root_dead_money = 0.5;
    game = pe_preflop_allin_game_create(&rules, NULL);
    CHECK(game != NULL, "dead-money game was not created");
    if (game)
    {
        /* The root a report describes is the one play starts from. */
        pe_preflop_root_view_t root;
        CHECK(pe_preflop_allin_root_view(game, &root) == 0 &&
                  root.street == 0 && root.first_to_act == 3 &&
                  fabs(root.pot - 2.0) < 1e-12 && root.posts[0] == 0.0 &&
                  root.posts[1] == 0.5 && root.posts[2] == 1.0 &&
                  root.posts[3] == 0.0 && root.behind[1] == 4.5 &&
                  root.behind[2] == 4.0 && root.behind[3] == 5.0,
              "root view: street %d opener %d pot %g posts %g,%g,%g,%g",
              root.street, root.first_to_act, root.pot, root.posts[0],
              root.posts[1], root.posts[2], root.posts[3]);
    }
    if (game)
    {
        const pe_external_game_t *external = pe_preflop_allin_external(game);
        pe_rng_t rng = pe_solver_rng_root(0xDEADu);
        pe_chance_sample_t sample;
        const void *states[4];
        states[0] = external->sample_chance_child(external->root, &rng,
                                                  &sample, external->user);
        /* UTG, BTN and SB fold: the BB takes the blinds and the dead money. */
        for (int i = 1; i < 4; ++i)
            states[i] = states[i - 1]
                ? external->apply_action(states[i - 1], 0u, external->user)
                : NULL;
        CHECK(states[3] != NULL, "the fold line did not play out");
        if (states[3])
        {
            double bb = external->terminal_value(states[3], 2, external->user);
            double sb = external->terminal_value(states[3], 1, external->user);
            CHECK(fabs(bb - 1.0) < 1e-9 && fabs(sb + 0.5) < 1e-9,
                  "fold-out paid BB %g and SB %g, want +1 and -0.5", bb, sb);
        }
        for (int i = 3; i >= 0; --i)
            if (states[i])
                external->release_state(states[i], external->user);
        pe_preflop_allin_game_destroy(game);
    }
    rules.root_dead_money = -1.0;
    CHECK(pe_preflop_allin_game_create(&rules, NULL) == NULL,
          "negative dead money was accepted");
    mpf_tree_free(tree);
}

/* 3-handed, 100 BB, BTN (seat 0) opens.  BTN folds, SB raises pot, BB
 * re-raises pot: the action is back on the SB.  The reader labels that node
 * "the seat after the BB", i.e. the folded BTN; the betting engine skips the
 * folded seat.  Replaying the tree binds the node to the SB. */
static void test_actor_wraps_past_fold(void)
{
    static const unsigned edges[] = {
        2u,                         /* BTN: fold | all-in                 */
        0u, 2u,                     /*  fold -> SB: fold | pot            */
        0u, 0u,                     /*    fold -> leaf                    */
        40100u, 3u,                 /*    pot -> BB: fold | call | pot    */
        0u, 0u,                     /*      fold -> leaf                  */
        1u, 0u,                     /*      call -> leaf                  */
        40100u, 2u,                 /*      pot -> SB again: fold | call  */
        0u, 0u, 1u, 0u,
        3u, 2u,                     /*  all-in -> SB: fold | call         */
        0u, 2u, 0u, 0u, 1u, 0u,     /*    fold -> BB: fold | call         */
        1u, 2u, 0u, 0u, 1u, 0u      /*    call -> BB: fold | call         */
    };
    enum { SB_AGAIN = 6 };
    unsigned char bytes[256];
    size_t at = 0u;
    pe_monker_tree_header_t header;
    mpf_tree_def_t *tree = NULL;
    double posts[PE_MONKER_MAX_PLAYERS];
    double stacks[PE_MONKER_MAX_PLAYERS];
    double dead;
    pe_preflop_allin_rules_t rules;
    pe_preflop_allin_game_t *game;
    pe_storage_t *storage = NULL;
    int reached = 0;

    put_i64(bytes, &at, 33487);
    put_i32(bytes, &at, 1);
    put_i32(bytes, &at, 3);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 0);
    put_i32(bytes, &at, 1000);
    put_i32(bytes, &at, 2000);
    put_i32(bytes, &at, 0);
    for (int p = 0; p < 3; ++p)
        put_i32(bytes, &at, 200000);
    for (size_t i = 0u; i < sizeof(edges) / sizeof(edges[0]); ++i)
        put_u16(bytes, &at, edges[i]);
    bytes[at++] = 0u;

    CHECK(load_tree("poker_eval_monker_wrap3.tree", bytes, at, &header,
                    &tree) == 0,
          "3-handed wrap fixture did not load");
    if (!tree)
        return;
    CHECK(tree->node_count > SB_AGAIN &&
              tree->nodes[SB_AGAIN].type == MPF_TREE_NODE_PLAYER &&
              tree->nodes[SB_AGAIN].acting_player == 0,
          "fixture drifted: node %d should be the reader's 'seat 0' guess",
          (int)SB_AGAIN);
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_OK,
          "3-handed header did not convert");

    base_rules(&rules, 3, tree);
    rules.has_root_posts = 1;
    rules.root_to_act = header.first_to_act;
    for (int p = 0; p < 3; ++p)
    {
        rules.root_posts[p] = posts[p];
        rules.stacks[p] = stacks[p];
    }

    /* The reader's labels alone: the SB node is bound to the folded BTN. */
    game = pe_preflop_allin_game_create(&rules, NULL);
    CHECK(game != NULL, "3-handed game was not created");
    if (game)
    {
        CHECK(pe_preflop_allin_tree_actor(game, SB_AGAIN) == 0,
              "without the replay the node should keep the reader's label");
        CHECK(run_solve(game, 20, 0x3u, &storage) != 0,
              "the reader's label on node %d did not break the solve",
              (int)SB_AGAIN);
        pe_storage_destroy(storage);
        storage = NULL;
        pe_preflop_allin_game_destroy(game);
    }

    rules.tree_actors_from_betting = 1;
    game = pe_preflop_allin_game_create(&rules, NULL);
    CHECK(game != NULL, "replayed 3-handed game was not created");
    if (!game)
    {
        mpf_tree_free(tree);
        return;
    }
    CHECK(pe_preflop_allin_tree_actor(game, SB_AGAIN) == 1,
          "node %d replayed to seat %d, want the SB (1)", (int)SB_AGAIN,
          pe_preflop_allin_tree_actor(game, SB_AGAIN));
    for (int n = 0; n < tree->node_count; ++n)
        if (n != SB_AGAIN && tree->nodes[n].type == MPF_TREE_NODE_PLAYER)
            CHECK(pe_preflop_allin_tree_actor(game, n) ==
                      tree->nodes[n].acting_player,
                  "node %d moved from seat %d to %d", n,
                  tree->nodes[n].acting_player,
                  pe_preflop_allin_tree_actor(game, n));
    CHECK(run_solve(game, 300, 0x3u, &storage) == 0,
          "the replayed 3-handed tree failed a traversal");
    for (size_t i = 0u; i < pe_preflop_allin_infodesc_count(game); ++i)
    {
        pe_preflop_infodesc_view_t view;
        if (pe_preflop_allin_infodesc_view_at(game, i, &view) != 0 ||
            view.tree_node_index != SB_AGAIN)
            continue;
        reached = 1;
        CHECK(view.actor == 1 && view.action_count == 2u,
              "node %d played by seat %d with %u actions, want SB with 2",
              (int)SB_AGAIN, view.actor, (unsigned)view.action_count);
    }
    CHECK(reached, "the solve never reached node %d", (int)SB_AGAIN);
    pe_storage_destroy(storage);
    pe_preflop_allin_game_destroy(game);
    mpf_tree_free(tree);
    printf("  3-handed wrap past a fold: OK (node %d bound to the SB)\n",
           (int)SB_AGAIN);
}

/* Seat-by-seat posts the classic blinds cannot express: a live straddle sets
 * the minimum raise, and a post that covers a whole stack puts that seat all
 * in from the start instead of refusing the game. */
static void test_straddle_and_allin_posts(void)
{
    pe_preflop_allin_rules_t rules;
    pe_preflop_allin_game_t *game;
    pe_preflop_root_view_t root;
    pe_storage_t *storage = NULL;

    /* UTG straddles to 2: the minimum raise is 2 (to 4), not the CLI's 1. */
    base_rules(&rules, 4, NULL);
    rules.tree_showdown = 0;
    rules.has_root_posts = 1;
    rules.root_to_act = 0;
    rules.root_posts[1] = 0.5;
    rules.root_posts[2] = 1.0;
    rules.root_posts[3] = 2.0;
    game = pe_preflop_allin_game_create(&rules, NULL);
    CHECK(game != NULL, "straddled root was not created");
    if (game)
    {
        CHECK(pe_preflop_allin_root_view(game, &root) == 0 &&
                  fabs(root.min_raise - 2.0) < 1e-12 &&
                  fabs(root.pot - 3.5) < 1e-12 && root.first_to_act == 0,
              "straddled root: min raise %g pot %g opener %d, want 2, 3.5, 0",
              root.min_raise, root.pot, root.first_to_act);
        pe_preflop_allin_game_destroy(game);
    }

    /* An ante on every seat is not part of the raise: 1.05 - 0.05. */
    for (int p = 0; p < 4; ++p)
        rules.root_posts[p] = 0.05;
    rules.root_posts[1] = 0.55;
    rules.root_posts[2] = 1.05;
    game = pe_preflop_allin_game_create(&rules, NULL);
    CHECK(game != NULL && pe_preflop_allin_root_view(game, &root) == 0 &&
              fabs(root.min_raise - 1.0) < 1e-12,
          "ante root: min raise %g, want 1", game ? root.min_raise : -1.0);
    pe_preflop_allin_game_destroy(game);

    /* Seat 0 all in for less than the ante: still a minimum raise of 1. */
    rules.root_posts[0] = 0.025;
    rules.stacks[0] = 0.025;
    game = pe_preflop_allin_game_create(&rules, NULL);
    CHECK(game != NULL && pe_preflop_allin_root_view(game, &root) == 0 &&
              fabs(root.min_raise - 1.0) < 1e-12,
          "short all-in ante root: min raise %g, want 1",
          game ? root.min_raise : -1.0);
    pe_preflop_allin_game_destroy(game);

    /* The SB is all in from its 0.5 post and would open: the action skips it
     * to the BB, and the solve plays around the all-in seat. */
    base_rules(&rules, 4, NULL);
    rules.tree_showdown = 0;
    rules.has_root_posts = 1;
    rules.root_to_act = 1;
    rules.root_posts[1] = 0.5;
    rules.root_posts[2] = 1.0;
    rules.stacks[1] = 0.5;
    game = pe_preflop_allin_game_create(&rules, NULL);
    CHECK(game != NULL, "a seat all in from its post refused the game");
    if (game)
    {
        CHECK(pe_preflop_allin_root_view(game, &root) == 0 &&
                  root.first_to_act == 2 && root.posts[1] == 0.5 &&
                  root.behind[1] == 0.0 && root.behind[2] == 4.0,
              "all-in root: opener %d, SB posts %g behind %g",
              root.first_to_act, root.posts[1], root.behind[1]);
        CHECK(run_solve(game, 200, 0xA11u, &storage) == 0,
              "the solve failed around a seat all in from its post");
        for (size_t i = 0u; i < pe_preflop_allin_infodesc_count(game); ++i)
        {
            pe_preflop_infodesc_view_t view;
            if (pe_preflop_allin_infodesc_view_at(game, i, &view) == 0)
                CHECK(view.actor != 1, "the all-in SB was asked to act");
        }
        pe_storage_destroy(storage);
        pe_preflop_allin_game_destroy(game);
    }

    /* A post above the stack is still invalid, and so is a table where no
     * seat is left to act. */
    rules.root_posts[1] = 0.6;
    CHECK(pe_preflop_allin_game_create(&rules, NULL) == NULL,
          "a post above its stack was accepted");
    base_rules(&rules, 2, NULL);
    rules.tree_showdown = 0;
    rules.has_root_posts = 1;
    rules.root_to_act = 0;
    rules.root_posts[0] = rules.stacks[0];
    rules.root_posts[1] = rules.stacks[1];
    CHECK(pe_preflop_allin_game_create(&rules, NULL) == NULL,
          "a table all in from its posts was accepted");
    printf("  straddle and all-in posts: OK\n");
}

/* A postflop header and a header with nothing posted have nothing to seat. */
static void test_header_rejections(void)
{
    pe_monker_tree_header_t header;
    double posts[PE_MONKER_MAX_PLAYERS];
    double stacks[PE_MONKER_MAX_PLAYERS];
    double dead;

    memset(&header, 0, sizeof(header));
    header.player_count = 2u;
    header.stacks[0] = header.stacks[1] = 100.0;
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_ERR_INVALID_HEADER,
          "a header with no post was seated");
    header.committed[0] = 1.0;
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_ERR_INVALID_HEADER,
          "a heads-up header with nothing on the big blind seat was seated");
    header.committed[1] = 2.0;
    header.street = 1;
    CHECK(pe_monker_tree_preflop_posts(&header, 1.0, posts, stacks, &dead) ==
              PE_MONKER_ERR_INVALID_HEADER,
          "a flop header was seated as preflop");
    header.street = 0;
    CHECK(pe_monker_tree_preflop_posts(&header, 0.0, posts, stacks, &dead) ==
              PE_MONKER_ERR_INVALID_HEADER,
          "a zero big blind was accepted");
    CHECK(pe_monker_tree_preflop_posts(NULL, 1.0, posts, stacks, &dead) ==
              PE_MONKER_ERR_NULL_ARGUMENT,
          "a NULL header was accepted");
    CHECK(pe_preflop_allin_root_view(NULL, NULL) == -1,
          "a NULL root view was accepted");
}

int main(void)
{
    printf("test_preflop_monker_multiway\n");
    test_header_rejections();
    test_header_scaling();
    test_dead_money_in_pot();
    test_straddle_and_allin_posts();
    test_four_handed_monker_seats();
    test_actor_wraps_past_fold();
    test_heads_up_unchanged();
    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
