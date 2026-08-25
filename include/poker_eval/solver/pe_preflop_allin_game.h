/* pe_preflop_allin_game.h - production preflop game over sampled deals (Lane B)
 *
 * One configurable betting street in front of sampled private deals. With
 * postflop_streets enabled, completed betting rounds continue through public
 * flop, turn, and river chance nodes before the showdown. Fold-outs and
 * called pots are evaluated with the same deterministic sampled-deal model;
 * known river boards use exact high-hand and side-pot resolution.
 */

#ifndef POKER_EVAL_PE_PREFLOP_ALLIN_GAME_H
#define POKER_EVAL_PE_PREFLOP_ALLIN_GAME_H

#include <poker_eval/range.h>
#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_preflop_betting.h>
#include <poker_eval/solver/pe_storage.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_PREFLOP_ALLIN_MAX_PLAYERS 6
#define PE_PREFLOP_ALLIN_MAX_RAISE_SIZES 6

struct mpf_tree_def_t;

typedef struct
{
    /* Hold'em or PLO4/PLO5/PLO6. */
    pe_preflop_variant_t variant;
    int player_count;
    /* Chip stacks before forced bets. */
    double stacks[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    /* Forced bets: player 0 posts small_blind, player 1 posts big_blind,
       every later player posts ante (0 disables). Heads-up player 0 acts
       first; multiway games start at player 2 (UTG abstraction). */
    double small_blind;
    double big_blind;
    double ante;
    /* Betting structure. */
    double min_raise;
    int raise_cap; /* 0 = unlimited */
    /* Raise increments above the call (chips) offered at every decision.
       Sizes illegal in a state are filtered out per state. */
    double raise_sizes[PE_PREFLOP_ALLIN_MAX_RAISE_SIZES];
    int raise_count;
    /* When zero, plain calls are only legal when they put the caller all in;
       this yields all-in-or-fold style trees. */
    int allow_nonallin_call;
    /* When set, a completed preflop round advances through flop, turn and
       river chance nodes before the terminal showdown. */
    int postflop_streets;
    /* Optional imported Monker preflop tree.  Its terminal betting nodes are
       followed by automatic flop/turn/river dealing and showdown when
       tree_showdown is set. */
    const struct mpf_tree_def_t *tree;
    int tree_showdown;
    /* Showdown resolution. */
    int showdown_samples; /* sampled boards per called terminal */
    uint64_t showdown_seed;
} pe_preflop_allin_rules_t;

typedef struct pe_preflop_allin_game_t pe_preflop_allin_game_t;

/* Create the game from rules and one prepared (deduplicated, normalised)
 * pe_range_t per player. The ranges are borrowed; the caller keeps them
 * alive until destroy. Returns NULL on invalid input. */
pe_preflop_allin_game_t *pe_preflop_allin_game_create(
    const pe_preflop_allin_rules_t *rules, pe_range_t *const *ranges);

void pe_preflop_allin_game_destroy(pe_preflop_allin_game_t *game);

/* External-sampling representation, owned by the game. */
const pe_external_game_t *pe_preflop_allin_external(
    const pe_preflop_allin_game_t *game);

int pe_preflop_allin_player_count(const pe_preflop_allin_game_t *game);

/* Attach the storage the solve loop updates. While set, the game answers
 * action_probability with the current regret-matching strategy so opponents
 * are sampled by policy instead of uniformly. */
void pe_preflop_allin_game_set_storage(pe_preflop_allin_game_t *game,
                                       pe_storage_t *storage);

/* Betting-context description recorded the first time an infoset key is
 * produced, for human-readable exports. Stable until destroy. */
size_t pe_preflop_allin_infodesc_count(const pe_preflop_allin_game_t *game);
int pe_preflop_allin_infodesc_at(const pe_preflop_allin_game_t *game,
                                 size_t index, uint64_t *out_key,
                                 char *out_text, size_t text_capacity);

/* Terminal-value oracles, exposed for tests. showdown_equity fills
 * out_equity[0..player_count-1] for the given hole masks. */
int pe_preflop_allin_showdown_equity(const pe_preflop_allin_game_t *game,
                                     const mask_t *holes, double *out_equity);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_PREFLOP_ALLIN_GAME_H */
