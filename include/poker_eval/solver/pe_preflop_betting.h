/* pe_preflop_betting.h - sampled private deals + one complete betting street. */

#ifndef PE_PREFLOP_BETTING_H
#define PE_PREFLOP_BETTING_H

#include <poker_eval/solver/pe_actions.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_external_traversal.h>
#include <poker_eval/solver/pe_preflop_deal_sampler.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pe_preflop_betting_state_t
{
    int is_chance;
    pe_betting_state_t betting;
    mask_t holes[PE_PREFLOP_MAX_PLAYERS];
} pe_preflop_betting_state_t;

typedef struct
{
    uint16_t (*action_count)(const pe_preflop_betting_state_t *state,
                             void *user);
    pe_action_status_t (*action_at)(const pe_preflop_betting_state_t *state,
                                    uint16_t action, pe_action_t *out,
                                    void *user);
    uint64_t (*infoset_key)(const pe_preflop_betting_state_t *state,
                            void *user);
    double (*terminal_value)(const pe_preflop_betting_state_t *state,
                             int player, void *user);
} pe_preflop_betting_ops_t;

typedef struct
{
    pe_external_game_t game;
    pe_preflop_deal_sampler_t sampler;
    pe_betting_rules_t rules;
    pe_preflop_betting_state_t root;
    pe_preflop_betting_ops_t ops;
    void *user;
    pe_preflop_betting_state_t **owned_states;
    size_t owned_count;
    size_t owned_capacity;
} pe_preflop_betting_game_t;

int pe_preflop_betting_game_init(
    pe_preflop_betting_game_t *out,
    const pe_preflop_deal_sampler_t *sampler,
    const pe_betting_rules_t *rules,
    const pe_preflop_betting_state_t *root_betting,
    const pe_preflop_betting_ops_t *ops,
    void *user);

void pe_preflop_betting_game_destroy(pe_preflop_betting_game_t *game);

const pe_external_game_t *pe_preflop_betting_external(
    const pe_preflop_betting_game_t *game);

#ifdef __cplusplus
}
#endif

#endif /* PE_PREFLOP_BETTING_H */
