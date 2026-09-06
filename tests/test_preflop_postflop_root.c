/* test_preflop_postflop_root.c - Lane B rooted postflop instead of at the blinds.
 *
 * Runs the same spot from a flop root and from a river root.  Covers the
 * things a postflop root changes versus the classic blind-posted root:
 *   1. the fixed board is dead, so sampled hole cards never collide with it;
 *   2. the pot, stacks and first actor come from the rules, not from blinds;
 *   3. a tree "raise" edge played into a state with nothing to call means
 *      BET, not RAISE (which the betting state machine rejects outright).
 * Before (3) a flop root collapsed to a single check at every node and the
 * solve produced a uniform, zero-regret strategy.
 *
 * The river root additionally pins that a tree terminal on the last street
 * is the showdown itself.  Treating it as a chance node sent the traversal
 * looking for a sixth board card and failed the whole solve.
 */

#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/range.h>
#include <poker_eval/solver/pe_preflop_allin_game.h>
#include <poker_eval/solver/pe_range.h>
#include <poker_eval/solver/pe_rng.h>
#include <poker_eval/solver/pe_storage.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Heads-up one-street tree: P0 checks or bets 3, P1 folds or calls.  The
 * street name is substituted so the same shape is solved as a flop root and
 * as a river root. */
static const char *k_tree_json_template =
"{"
"  \"version\": 1,"
"  \"root\": \"flop_p0\","
"  \"betProfiles\": [ { \"id\": \"default\", \"sizes\": [3.0], \"pot_sizing\": false } ],"
"  \"nodes\": ["
"    { \"id\": \"flop_p0\", \"type\": \"player\", \"street\": \"%s\", \"player\": 0,"
"      \"bet_profile\": \"default\","
"      \"actions\": [ { \"type\": \"call\", \"next\": \"terminal_check\" },"
"                     { \"type\": \"raise\", \"size_index\": 0, \"next\": \"flop_p1\" } ] },"
"    { \"id\": \"flop_p1\", \"type\": \"player\", \"street\": \"%s\", \"player\": 1,"
"      \"bet_profile\": \"default\","
"      \"actions\": [ { \"type\": \"fold\", \"next\": \"terminal_fold\" },"
"                     { \"type\": \"call\", \"next\": \"terminal_call\" } ] },"
"    { \"id\": \"terminal_check\", \"type\": \"terminal\", \"street\": \"%s\" },"
"    { \"id\": \"terminal_fold\", \"type\": \"terminal\", \"street\": \"%s\" },"
"    { \"id\": \"terminal_call\", \"type\": \"terminal\", \"street\": \"%s\" }"
"  ]"
"}";

#define MAX_BOARD_CARDS 5

typedef struct
{
    const char *name;
    int street;                 /* PE_HOLDEM_FLOP .. PE_HOLDEM_RIVER */
    int card_count;
    int ranks[MAX_BOARD_CARDS];
    int suits[MAX_BOARD_CARDS];
} root_case_t;

/* Ah Ad 3c, then 7s 9h on the later streets. */
static const root_case_t k_cases[] = {
    { "flop", (int)PE_HOLDEM_FLOP, 3,
      { StdDeck_Rank_ACE, StdDeck_Rank_ACE, StdDeck_Rank_3 },
      { StdDeck_Suit_HEARTS, StdDeck_Suit_DIAMONDS, StdDeck_Suit_CLUBS } },
    { "river", (int)PE_HOLDEM_RIVER, 5,
      { StdDeck_Rank_ACE, StdDeck_Rank_ACE, StdDeck_Rank_3,
        StdDeck_Rank_7, StdDeck_Rank_9 },
      { StdDeck_Suit_HEARTS, StdDeck_Suit_DIAMONDS, StdDeck_Suit_CLUBS,
        StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS } }
};

static void board_cardmask(const root_case_t *c, StdDeck_CardMask *out)
{
    StdDeck_CardMask_RESET(*out);
    for (int i = 0; i < c->card_count; ++i)
        StdDeck_CardMask_SET(*out, StdDeck_MAKE_CARD(c->ranks[i], c->suits[i]));
}

static int run_solve(pe_preflop_allin_game_t *game, int iterations,
                     uint64_t seed, pe_storage_t **out_storage)
{
    pe_storage_t *storage = pe_storage_create(32);
    pe_external_sampling_ctx_t ctx[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    pe_update_batch_t batch = {0};
    const pe_external_game_t *external = pe_preflop_allin_external(game);
    int players = pe_preflop_allin_player_count(game);
    int failed = 0;

    if (!storage || players < 2 || players > PE_PREFLOP_ALLIN_MAX_PLAYERS)
        return -1;
    pe_preflop_allin_game_set_storage(game, storage);
    for (int player = 0; player < players; ++player)
    {
        if (pe_external_sampling_ctx_init(&ctx[player], external,
                                          pe_storage_ram_ops(), storage,
                                          player, seed + (uint64_t)player) != 0)
        {
            pe_storage_destroy(storage);
            return -1;
        }
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
    for (int player = 0; player < players; ++player)
        pe_external_sampling_ctx_destroy(&ctx[player]);
    if (failed)
    {
        pe_storage_destroy(storage);
        return -1;
    }
    *out_storage = storage;
    return 0;
}

static void run_case(const root_case_t *c)
{
    char tree_json[2048];
    mpf_tree_error_t tree_error;
    mpf_tree_def_t *tree;
    pe_preflop_allin_rules_t rules;
    pe_range_t *ranges[2] = {NULL, NULL};
    pe_preflop_allin_game_t *game;
    pe_storage_t *storage = NULL;
    StdDeck_CardMask board;
    size_t desc_count;
    int saw_bet = 0;
    int saw_call = 0;
    int saw_fold = 0;
    int saw_check = 0;
    int saw_board_card_in_hand = 0;
    int non_uniform = 0;
    const char *street_word = c->street == (int)PE_HOLDEM_FLOP ? "FLOP"
                            : c->street == (int)PE_HOLDEM_TURN ? "TURN" : "RIVER";

    /* The template carries one %s per node street (5 nodes). */
    snprintf(tree_json, sizeof(tree_json), k_tree_json_template,
             street_word, street_word, street_word, street_word, street_word);

    memset(&tree_error, 0, sizeof(tree_error));
    tree = mpf_tree_load_json(tree_json, strlen(tree_json), &tree_error);
    assert(tree != NULL);

    /* The board must be dead in the deal sampler, so parse the ranges with
     * it dead too: an AA range on an Ah Ad board leaves only Ac As. */
    board_cardmask(c, &board);
    assert(pe_solver_range_parse(game_holdem, "AA,KK,QQ,72o", board, &ranges[0]) ==
           PE_SOLVER_OK);
    assert(pe_solver_range_parse(game_holdem, "AA,KK,QQ,72o", board, &ranges[1]) ==
           PE_SOLVER_OK);

    memset(&rules, 0, sizeof(rules));
    rules.variant = PE_PREFLOP_HOLDEM;
    rules.player_count = 2;
    rules.stacks[0] = 97.0;   /* remaining stacks, not starting stacks */
    rules.stacks[1] = 97.0;
    rules.min_raise = 1.0;
    rules.raise_cap = 4;
    rules.allow_nonallin_call = 1;
    rules.showdown_samples = 64;
    rules.showdown_seed = 0xF10B00Du;
    rules.tree = tree;
    rules.tree_showdown = 1;
    rules.root_street = c->street;
    rules.root_board = (uint64_t)cardmask_to_mask_t(board);
    rules.root_pot = 6.0;
    rules.root_to_act = 0;

    game = pe_preflop_allin_game_create(&rules, ranges);
    assert(game != NULL);

    /* A complete-range caller is allowed to omit the range array entirely.
     * This is the public contract used by the direct PLO5/PLO6 path. */
    {
        pe_preflop_allin_rules_t complete_rules = rules;
        pe_preflop_allin_game_t *complete_game;
        complete_rules.complete_ranges = 1;
        complete_game = pe_preflop_allin_game_create(&complete_rules, NULL);
        assert(complete_game != NULL);
        pe_preflop_allin_game_destroy(complete_game);
    }

    /* The root pot is carried in betting.pot, not invested[].  Exercise the
     * fold payoff after a 3 BB bet: the winner must receive 6 BB of the
     * carried pot after returning the 3 BB bet, not zero. */
    {
        const pe_external_game_t *external = pe_preflop_allin_external(game);
        pe_rng_t rng = pe_solver_rng_root(0xBEEF);
        pe_chance_sample_t sample;
        const void *dealt = external->sample_chance_child(
            external->root, &rng, &sample, external->user);
        const void *bet = dealt != NULL
            ? external->apply_action(dealt, 1u, external->user) : NULL;
        const void *fold = bet != NULL
            ? external->apply_action(bet, 0u, external->user) : NULL;
        assert(dealt != NULL && bet != NULL && fold != NULL);
        assert(fabs(external->terminal_value(fold, 0, external->user) - 6.0) <
               1e-9);
        assert(fabs(external->terminal_value(fold, 1, external->user)) < 1e-9);
        external->release_state(fold, external->user);
        external->release_state(bet, external->user);
        external->release_state(dealt, external->user);
    }
    assert(run_solve(game, 400, 0xC0FFEEu, &storage) == 0);
    assert(storage != NULL);
    assert(pe_storage_count(storage) > 0u);

    /* Every observed decision: the board is dead, and both tree edges are
     * offered with the street-correct semantics. */
    desc_count = pe_preflop_allin_infodesc_count(game);
    assert(desc_count > 0u);
    for (size_t index = 0u; index < desc_count; ++index)
    {
        pe_preflop_infodesc_view_t view;
        if (pe_preflop_allin_infodesc_view_at(game, index, &view) != 0)
            continue;
        assert(view.action_count >= 2u);
        for (uint16_t a = 0u; a < view.action_count; ++a)
        {
            const char *label = view.actions[a];
            if (strstr(label, "bet")) saw_bet = 1;
            else if (strstr(label, "check")) saw_check = 1;
            else if (strstr(label, "call")) saw_call = 1;
            else if (strstr(label, "fold")) saw_fold = 1;
        }
        /* Hole cards are drawn from a deck with the board removed. */
        for (int i = 0; i < c->card_count; ++i)
        {
            static const char ranks[] = "23456789TJQKA";
            static const char suits[] = "hdcs";
            char card[3];
            card[0] = ranks[c->ranks[i]];
            card[1] = suits[c->suits[i]];
            card[2] = '\0';
            if (strstr(view.hand, card))
                saw_board_card_in_hand = 1;
        }
        /* The root pot is the rules value, not a blind-built one. */
        if (view.tree_node_index == 0)
            assert(fabs(view.pot - 6.0) < 1e-9 && fabs(view.to_call) < 1e-9);
    }
    assert(!saw_board_card_in_hand);
    /* The passive edge is a check (nothing to call) and the aggressive one a
     * bet.  Without the raise->bet translation the node kept only "check". */
    assert(saw_check && saw_bet);
    /* The second node faces a live bet, so its edges stay fold/call. */
    assert(saw_fold && saw_call);

    /* A solve that only ever saw one legal action would leave every average
     * strategy uniform; check at least one infoset actually moved. */
    for (size_t id = 0u; id < pe_storage_count(storage); ++id)
    {
        const pe_infoset_meta_t *meta = pe_storage_meta(storage, (pe_infoset_id_t)id);
        const double *average =
            pe_storage_values(storage, (pe_infoset_id_t)id, PE_VALUES_AVERAGE);
        double total = 0.0;
        if (!meta || !average || meta->action_count < 2u)
            continue;
        for (uint16_t a = 0u; a < meta->action_count; ++a)
            total += average[a];
        if (total <= 0.0)
            continue;
        if (fabs(average[0] / total - 0.5) > 0.05)
            non_uniform = 1;
    }
    assert(non_uniform);

    pe_storage_destroy(storage);
    pe_preflop_allin_game_destroy(game);
    pe_range_free(ranges[0]);
    pe_range_free(ranges[1]);
    mpf_tree_free(tree);

    printf("  %-6s root: OK (%zu observed decisions)\n", c->name, desc_count);
}

/* A single tree spanning two streets, Monker-style: the round-closing
 * action of the flop wires straight to a turn player node.  The engine must
 * deal the turn and resume AT THAT NODE.  Before this, any postflop chance
 * node rolled out to showdown regardless of what the tree still had waiting,
 * so only the root street was ever solved. */
static const char *k_two_street_json =
"{"
"  \"version\": 1,"
"  \"root\": \"flop_bb\","
"  \"betProfiles\": [ { \"id\": \"pot\", \"sizes\": [1.0], \"pot_sizing\": true } ],"
"  \"nodes\": ["
"    { \"id\": \"flop_bb\", \"type\": \"player\", \"street\": \"FLOP\", \"player\": 1,"
"      \"bet_profile\": \"pot\","
"      \"actions\": [ { \"type\": \"call\", \"next\": \"flop_sb\" },"
"                     { \"type\": \"raise\", \"size_index\": 0, \"next\": \"term_bet\" } ] },"
"    { \"id\": \"flop_sb\", \"type\": \"player\", \"street\": \"FLOP\", \"player\": 0,"
"      \"bet_profile\": \"pot\","
"      \"actions\": [ { \"type\": \"call\", \"next\": \"turn_bb\" },"
"                     { \"type\": \"raise\", \"size_index\": 0, \"next\": \"term_bet\" } ] },"
"    { \"id\": \"turn_bb\", \"type\": \"player\", \"street\": \"TURN\", \"player\": 1,"
"      \"bet_profile\": \"pot\","
"      \"actions\": [ { \"type\": \"call\", \"next\": \"term_turn\" },"
"                     { \"type\": \"raise\", \"size_index\": 0, \"next\": \"term_turn\" } ] },"
"    { \"id\": \"term_bet\",  \"type\": \"terminal\", \"street\": \"FLOP\" },"
"    { \"id\": \"term_turn\", \"type\": \"terminal\", \"street\": \"TURN\" }"
"  ]"
"}";

static void run_two_street_case(void)
{
    mpf_tree_error_t tree_error;
    mpf_tree_def_t *tree;
    pe_preflop_allin_rules_t rules;
    pe_range_t *ranges[2] = {NULL, NULL};
    pe_preflop_allin_game_t *game;
    pe_storage_t *storage = NULL;
    StdDeck_CardMask board;
    size_t desc_count;
    int saw_flop_node = 0;
    int saw_turn_node = 0;
    const root_case_t *flop = &k_cases[0];

    memset(&tree_error, 0, sizeof(tree_error));
    tree = mpf_tree_load_json(k_two_street_json, strlen(k_two_street_json),
                              &tree_error);
    assert(tree != NULL);

    board_cardmask(flop, &board);
    assert(pe_solver_range_parse(game_holdem, "AA,KK,QQ,72o", board, &ranges[0]) ==
           PE_SOLVER_OK);
    assert(pe_solver_range_parse(game_holdem, "AA,KK,QQ,72o", board, &ranges[1]) ==
           PE_SOLVER_OK);

    memset(&rules, 0, sizeof(rules));
    rules.variant = PE_PREFLOP_HOLDEM;
    rules.player_count = 2;
    rules.stacks[0] = 97.0;
    rules.stacks[1] = 97.0;
    rules.min_raise = 1.0;
    rules.raise_cap = 4;
    rules.allow_nonallin_call = 1;
    rules.showdown_samples = 64;
    rules.showdown_seed = 0x7EE5u;
    rules.tree = tree;
    rules.tree_showdown = 1;
    rules.root_street = (int)PE_HOLDEM_FLOP;
    rules.root_board = (uint64_t)cardmask_to_mask_t(board);
    rules.root_pot = 6.0;
    rules.root_to_act = 1;

    game = pe_preflop_allin_game_create(&rules, ranges);
    assert(game != NULL);
    assert(run_solve(game, 600, 0xA11Eu, &storage) == 0);

    desc_count = pe_preflop_allin_infodesc_count(game);
    for (size_t index = 0u; index < desc_count; ++index)
    {
        pe_preflop_infodesc_view_t view;
        if (pe_preflop_allin_infodesc_view_at(game, index, &view) != 0)
            continue;
        /* Node 2 is turn_bb: reaching it proves the tree survived the street
         * change instead of being abandoned to a rollout. */
        if (view.tree_node_index == 0 || view.tree_node_index == 1)
            saw_flop_node = 1;
        if (view.tree_node_index == 2)
            saw_turn_node = 1;
    }
    assert(saw_flop_node);
    assert(saw_turn_node);

    pe_storage_destroy(storage);
    pe_preflop_allin_game_destroy(game);
    pe_range_free(ranges[0]);
    pe_range_free(ranges[1]);
    mpf_tree_free(tree);

    printf("  two-street tree: OK (turn node entered from a flop root)\n");
}

int main(void)
{
    printf("test_preflop_postflop_root\n");
    for (size_t i = 0u; i < sizeof(k_cases) / sizeof(k_cases[0]); ++i)
        run_case(&k_cases[i]);
    run_two_street_case();
    return 0;
}
