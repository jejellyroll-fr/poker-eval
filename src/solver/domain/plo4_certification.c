/*
 * plo4_certification.c - heads-up PLO4 river exploitability certificate
 *
 * Issue #283, first tranche.  See pe_plo4_certification.h for the game
 * contract and for the definition of every reported number.
 *
 * The module never samples: the declared river tree is traversed completely
 * and every private-combo pair is either counted by the showdown kernel's
 * compatible mass or removed by a card blocker.  The fixed strategy is the
 * caller's callback, so an abstraction-expanded strategy is measured exactly
 * as declared.
 */

#include <poker_eval/solver/pe_plo4_certification.h>

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_betting_vector.h>

#include "finite_double.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PE_PLO4_CERT_PLAYERS 2u
#define PE_PLO4_CERT_BOARD_CARDS 5u
#define PE_PLO4_CERT_HOLE_CARDS 4u
#define PE_PLO4_CERT_SUM_EPSILON 1e-9

typedef struct
{
    const pe_plo4_cert_spec_t *spec;
    pe_combo_count_t combo_count;
    size_t live_combos[PE_PLO4_CERT_PLAYERS];
    mask_t dead;
    /* Player-major arrays: index player * combo_count + combo. */
    mask_t *masks;
    int64_t *strength;
    double *prior;
    double bet_size;
    /* Scratch owned by the terminal: both are combo_count doubles. */
    double *shares;
    double *mass;
} plo4_cert_ctx_t;

static int finite_nonnegative(double value)
{
    return value >= 0.0 && value <= DBL_MAX;
}

static StdDeck_CardMask to_std_mask(mask_t cards)
{
    StdDeck_CardMask result;
    int card;

    StdDeck_CardMask_RESET(result);
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(cards, card))
            StdDeck_CardMask_SET(result, card);
    return result;
}

static int64_t omaha_high_strength(mask_t hole, mask_t board)
{
    HandVal value = HandVal_NOTHING;

    if (StdDeck_OmahaHi_EVAL(to_std_mask(hole), to_std_mask(board), &value) != 0)
        return 0;
    return (int64_t)value;
}

/* Four board cards, used only to pad a shorter range with hands the dead-card
 * mask removes.  Keeping four cards on the padding keeps the range width
 * classifier on the four-card path. */
static mask_t board_padding(mask_t board)
{
    mask_t padding = MASK_EMPTY;
    int taken = 0;
    int card;

    for (card = 0; card < MODERN_DECK_SIZE && taken < (int)PE_PLO4_CERT_HOLE_CARDS;
         ++card)
        if (mask_is_set(board, card))
        {
            padding = mask_set(padding, card);
            ++taken;
        }
    return padding;
}

static int valid_spec(const pe_plo4_cert_spec_t *spec)
{
    uint8_t player;

    if (!spec || !spec->ranges || !spec->strategy)
        return 0;
    if (!mask_is_valid(spec->board) ||
        mask_popcount(spec->board) != (int)PE_PLO4_CERT_BOARD_CARDS)
        return 0;
    if (!finite_nonnegative(spec->pot) || spec->pot <= 0.0 ||
        !finite_nonnegative(spec->big_blind) || spec->big_blind <= 0.0 ||
        !finite_nonnegative(spec->bet_size) || spec->bet_size <= 0.0)
        return 0;
    for (player = 0u; player < PE_PLO4_CERT_PLAYERS; ++player)
    {
        const pe_omaha_range_t *range = &spec->ranges[player];
        size_t index;

        if (!finite_nonnegative(spec->invested[player]) ||
            !finite_nonnegative(spec->stacks[player]) ||
            spec->stacks[player] <= 0.0 || range->count == 0u ||
            range->count > (size_t)PE_COMBO_COUNT_MAX || !range->combos)
            return 0;
        if (!(spec->bet_size <= spec->stacks[player]))
            return 0;
        for (index = 0u; index < range->count; ++index)
        {
            mask_t hole = range->combos[index].cards;
            double weight = range->combos[index].weight;

            if (!mask_is_valid(hole) ||
                mask_popcount(hole) != (int)PE_PLO4_CERT_HOLE_CARDS ||
                (hole & spec->board) != 0u || !finite_nonnegative(weight))
                return 0;
        }
    }
    return 1;
}

static pe_solver_status_t strategy_is_normalized(plo4_cert_ctx_t *ctx)
{
    pe_vec_t row = {0};
    uint16_t infoset;
    int ok = 1;

    if (pe_vec_alloc(&row, ctx->combo_count) != PE_SOLVER_OK)
        return PE_SOLVER_ERR_OUT_OF_MEMORY;
    for (infoset = 0u; infoset < PE_PLO4_CERT_INFOSET_COUNT && ok; ++infoset)
    {
        pe_vec_t column = {0};
        uint16_t action;

        pe_vec_fill(&row, 0.0);
        for (action = 0u; action < PE_PLO4_CERT_ACTION_COUNT && ok; ++action)
        {
            pe_combo_id_t combo;

            if (pe_vec_alloc(&column, ctx->combo_count) != PE_SOLVER_OK)
            {
                ok = 0;
                break;
            }
            if (ctx->spec->strategy(ctx->spec->strategy_user, infoset, action,
                                    &column) != 0)
                ok = 0;
            for (combo = 0u; ok && combo < ctx->combo_count; ++combo)
            {
                double probability = column.v[combo];

                if (!(probability >= 0.0) || !(probability <= 1.0))
                    ok = 0;
                else
                    row.v[combo] += probability;
            }
            pe_vec_free(&column);
        }
        if (ok)
            for (pe_combo_id_t combo = 0u; combo < ctx->combo_count; ++combo)
                if (fabs(row.v[combo] - 1.0) > PE_PLO4_CERT_SUM_EPSILON)
                    ok = 0;
    }
    pe_vec_free(&row);
    return ok ? PE_SOLVER_OK : PE_SOLVER_ERR_INVALID_CONFIG;
}

static pe_solver_status_t build_ranges(plo4_cert_ctx_t *ctx)
{
    const pe_plo4_cert_spec_t *spec = ctx->spec;
    mask_t padding = board_padding(spec->board);
    size_t width = 0u;
    uint8_t player;

    for (player = 0u; player < PE_PLO4_CERT_PLAYERS; ++player)
        if (spec->ranges[player].count > width)
            width = spec->ranges[player].count;
    if (width == 0u)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    ctx->combo_count = (pe_combo_count_t)width;
    ctx->dead = spec->board;
    ctx->masks = (mask_t *)calloc((size_t)PE_PLO4_CERT_PLAYERS * width,
                                  sizeof(*ctx->masks));
    ctx->strength = (int64_t *)calloc((size_t)PE_PLO4_CERT_PLAYERS * width,
                                      sizeof(*ctx->strength));
    ctx->prior = (double *)calloc((size_t)PE_PLO4_CERT_PLAYERS * width,
                                  sizeof(*ctx->prior));
    ctx->shares = (double *)calloc(width, sizeof(*ctx->shares));
    ctx->mass = (double *)calloc(width, sizeof(*ctx->mass));
    if (!ctx->masks || !ctx->strength || !ctx->prior || !ctx->shares ||
        !ctx->mass)
        return PE_SOLVER_ERR_OUT_OF_MEMORY;

    for (player = 0u; player < PE_PLO4_CERT_PLAYERS; ++player)
    {
        const pe_omaha_range_t *range = &spec->ranges[player];
        size_t base = (size_t)player * width;
        size_t index;

        ctx->live_combos[player] = 0u;
        for (index = 0u; index < width; ++index)
        {
            if (index < range->count)
            {
                mask_t hole = range->combos[index].cards;
                double weight = range->combos[index].weight;

                ctx->masks[base + index] = hole;
                ctx->strength[base + index] =
                    omaha_high_strength(hole, spec->board);
                ctx->prior[base + index] = weight;
                if (weight > 0.0)
                    ctx->live_combos[player]++;
            }
            else
            {
                /* A four-card mask the board blocks: the showdown kernel
                 * gives it zero compatible mass, so the padding cannot leak
                 * into a value. */
                ctx->masks[base + index] = padding;
                ctx->strength[base + index] = 0;
                ctx->prior[base + index] = 0.0;
            }
        }
        if (ctx->live_combos[player] == 0u)
            return PE_SOLVER_ERR_INVALID_CONFIG;
    }
    return PE_SOLVER_OK;
}

static void free_ctx(plo4_cert_ctx_t *ctx)
{
    free(ctx->mass);
    free(ctx->shares);
    free(ctx->prior);
    free(ctx->strength);
    free(ctx->masks);
    memset(ctx, 0, sizeof(*ctx));
}

/* Conditional-on-own-combo showdown value, net of `invested`, for one player.
 * `opp_reach` carries the opponent's prior times its behavior reach, exactly
 * what the vector lane accumulates. */
static pe_solver_status_t showdown_values(plo4_cert_ctx_t *ctx, uint8_t hero,
                                          const pe_reach_vec_t *opp_reach,
                                          double pot, double invested,
                                          pe_value_vec_t *out)
{
    size_t width = (size_t)ctx->combo_count;
    size_t base = (size_t)hero * width;
    size_t opponent_base = (size_t)(1u - hero) * width;
    pe_value_vec_t shares = pe_vec_wrap(ctx->shares, width);
    pe_solver_status_t status;
    pe_combo_id_t combo;

    if (opp_reach->n != width || out->n != width)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    status = pe_showdown_vector_mass(
        ctx->masks + base, ctx->strength + base, width, ctx->masks + opponent_base,
        ctx->strength + opponent_base, opp_reach->v, width, ctx->dead, pot,
        &shares, ctx->mass, NULL);
    if (status != PE_SOLVER_OK)
        return status;
    for (combo = 0u; combo < ctx->combo_count; ++combo)
    {
        double mass = ctx->mass[combo];

        /* A combo whose every opponent is blocked has no conditional payoff;
         * zero is the contribution such a branch carries. */
        out->v[combo] =
            mass > 0.0 ? (shares.v[combo] - invested * mass) / mass : 0.0;
    }
    return PE_SOLVER_OK;
}

static int plo4_terminal_values(const pe_betting_state_t *state,
                                const pe_reach_vec_t *reach,
                                pe_value_vec_t *out_values,
                                uint8_t player_count, void *user)
{
    plo4_cert_ctx_t *ctx = (plo4_cert_ctx_t *)user;
    uint8_t player;

    if (!ctx || !state || !reach || !out_values ||
        player_count != PE_PLO4_CERT_PLAYERS)
        return -1;
    if (state->winner >= 0)
    {
        for (player = 0u; player < PE_PLO4_CERT_PLAYERS; ++player)
        {
            double value = state->winner == (int)player
                               ? state->pot - state->invested[player]
                               : -state->invested[player];
            pe_vec_fill(&out_values[player], value);
        }
        return 0;
    }
    for (player = 0u; player < PE_PLO4_CERT_PLAYERS; ++player)
    {
        uint8_t opponent = (uint8_t)(1u - player);

        if (showdown_values(ctx, player, &reach[opponent], state->pot,
                            state->invested[player], &out_values[player]) !=
            PE_SOLVER_OK)
            return -1;
    }
    return 0;
}

static int plo4_combo_compatible(const void *state, uint8_t player,
                                 pe_combo_id_t player_combo, uint8_t opponent,
                                 pe_combo_id_t opponent_combo, void *user)
{
    plo4_cert_ctx_t *ctx = (plo4_cert_ctx_t *)user;
    mask_t left;
    mask_t right;

    (void)state;
    if (!ctx || player >= PE_PLO4_CERT_PLAYERS ||
        opponent >= PE_PLO4_CERT_PLAYERS ||
        player_combo >= ctx->combo_count ||
        opponent_combo >= ctx->combo_count)
        return -1;
    left = ctx->masks[(size_t)player * ctx->combo_count + player_combo];
    right = ctx->masks[(size_t)opponent * ctx->combo_count + opponent_combo];
    if ((left & ctx->dead) != 0u || (right & ctx->dead) != 0u)
        return 0;
    return (left & right) == 0u ? 1 : 0;
}

/* The vector lane calls this callback with the betting adapter as its user;
 * the adapter keeps this module's context in its own `user` field. */
static int plo4_combo_prior(uint8_t player, pe_combo_id_t combo, double *out,
                            void *user)
{
    const pe_betting_vector_game_t *game =
        (const pe_betting_vector_game_t *)user;
    const plo4_cert_ctx_t *ctx;

    if (!game || !out)
        return -1;
    ctx = (const plo4_cert_ctx_t *)game->user;
    if (!ctx || player >= PE_PLO4_CERT_PLAYERS ||
        combo >= ctx->combo_count)
        return -1;
    *out = ctx->prior[(size_t)player * ctx->combo_count + combo];
    return 0;
}

static uint16_t plo4_action_count(const pe_betting_state_t *state, void *user)
{
    (void)user;
    return state ? (uint16_t)PE_PLO4_CERT_ACTION_COUNT : 0u;
}

static pe_action_status_t plo4_action_at(const pe_betting_state_t *state,
                                         uint16_t action, pe_action_t *out,
                                         void *user)
{
    plo4_cert_ctx_t *ctx = (plo4_cert_ctx_t *)user;

    if (!ctx || !state || !out || action >= PE_PLO4_CERT_ACTION_COUNT)
        return PE_ACTION_ERR_OUT_OF_RANGE;
    *out = pe_action_invalid();
    if (state->to_call <= 1e-9)
    {
        if (action == 0u)
        {
            out->kind = PE_ACTION_CHECK;
            out->amount_kind = PE_AMOUNT_NONE;
        }
        else
        {
            out->kind = PE_ACTION_BET;
            out->amount_kind = PE_AMOUNT_CHIPS;
            out->amount = ctx->bet_size;
        }
    }
    else if (action == 0u)
        out->kind = PE_ACTION_FOLD;
    else
        out->kind = PE_ACTION_CALL;
    return PE_ACTION_OK;
}

static uint64_t plo4_infoset_key(const pe_betting_state_t *state, void *user)
{
    uint64_t facing;

    (void)user;
    if (!state || state->to_act < 0)
        return 0u;
    facing = state->to_call > 1e-9 ? 1u : 0u;
    return ((uint64_t)(uint8_t)state->to_act << 1) | facing;
}

static int plo4_strategy(const pe_betting_state_t *state, uint64_t infoset_key,
                         uint16_t action, pe_value_vec_t *out, void *user)
{
    plo4_cert_ctx_t *ctx = (plo4_cert_ctx_t *)user;

    (void)state;
    if (!ctx || !out || action >= PE_PLO4_CERT_ACTION_COUNT ||
        infoset_key >= (uint64_t)PE_PLO4_CERT_INFOSET_COUNT)
        return -1;
    return ctx->spec->strategy(ctx->spec->strategy_user,
                               (uint16_t)infoset_key, action, out);
}

/* Root compatible mass: player 0's prior times the mass of legal player 1
 * hands, so the pair normaliser costs one showdown sweep rather than n0*n1
 * pair visits. */
static pe_solver_status_t root_joint_mass(plo4_cert_ctx_t *ctx,
                                          double *out_mass,
                                          pe_showdown_path_t *out_path)
{
    size_t width = (size_t)ctx->combo_count;
    pe_value_vec_t shares = pe_vec_wrap(ctx->shares, width);
    pe_reach_vec_t opponent_reach = pe_vec_wrap(ctx->prior + width, width);
    pe_solver_status_t status;
    pe_combo_id_t combo;
    double total = 0.0;

    status = pe_showdown_vector_mass(
        ctx->masks, ctx->strength, width, ctx->masks + width,
        ctx->strength + width, opponent_reach.v, width, ctx->dead, 1.0, &shares,
        ctx->mass, out_path);
    if (status != PE_SOLVER_OK)
        return status;
    for (combo = 0u; combo < ctx->combo_count; ++combo)
        total += ctx->prior[combo] * ctx->mass[combo];
    *out_mass = total;
    return total > 0.0 && pe_finite_double(total) ? PE_SOLVER_OK
                                                  : PE_SOLVER_ERR_INVALID_CONFIG;
}

static void fill_units(pe_plo4_cert_result_t *result,
                       const pe_plo4_cert_spec_t *spec)
{
    result->pot_reference = spec->pot;
    result->big_blind = spec->big_blind;
    result->exploitability_mbb_per_game =
        result->exploitability / spec->big_blind * 1000.0;
    result->exploitability_pct_of_pot =
        result->exploitability / spec->pot * 100.0;
}

pe_solver_status_t pe_plo4_cert_evaluate(
    const pe_plo4_cert_spec_t *spec,
    const pe_best_response_vector_config_t *config,
    pe_plo4_cert_result_t *out_result)
{
    pe_best_response_vector_config_t defaults;
    pe_betting_rules_t rules;
    pe_betting_state_t root;
    pe_betting_vector_ops_t ops;
    pe_betting_vector_game_t game;
    pe_exploitability_vector_result_t measured;
    plo4_cert_ctx_t ctx;
    pe_solver_status_t status;
    double joint_mass = 0.0;
    double scale;
    pe_showdown_path_t path = PE_SHOWDOWN_PATH_NONE;
    uint8_t player;

    if (!out_result)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    memset(out_result, 0, sizeof(*out_result));
    out_result->verdict = PE_PLO4_CERT_VERDICT_NOT_EVALUATED;
    out_result->reason = PE_PLO4_CERT_REASON_INVALID_GAME;
    out_result->mode = "not_certified";
    out_result->guarantee = "none";
    out_result->game_scope =
        "heads-up PLO4 river, one fixed bet size, no rake, declared private ranges";
    if (!valid_spec(spec))
        return PE_SOLVER_ERR_INVALID_CONFIG;
    if (!config)
    {
        defaults = pe_best_response_vector_config_default();
        config = &defaults;
    }
    if (config->max_iterations == 0u)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    memset(&ctx, 0, sizeof(ctx));
    memset(&game, 0, sizeof(game));
    memset(&measured, 0, sizeof(measured));
    ctx.spec = spec;
    ctx.bet_size = spec->bet_size;
    status = build_ranges(&ctx);
    if (status != PE_SOLVER_OK)
    {
        free_ctx(&ctx);
        return status;
    }
    status = strategy_is_normalized(&ctx);
    if (status != PE_SOLVER_OK)
    {
        free_ctx(&ctx);
        return status;
    }
    status = root_joint_mass(&ctx, &joint_mass, &path);
    if (status != PE_SOLVER_OK)
    {
        free_ctx(&ctx);
        return status;
    }

    pe_betting_rules_default(&rules, PE_PLO4_CERT_PLAYERS);
    rules.min_raise = spec->bet_size;
    if (pe_betting_state_init(&root, &rules, spec->stacks,
                              PE_PLO4_CERT_PLAYERS, 0, spec->pot, 0.0) !=
        PE_BETTING_OK)
    {
        free_ctx(&ctx);
        return PE_SOLVER_ERR_INVALID_CONFIG;
    }
    root.invested[0] = spec->invested[0];
    root.invested[1] = spec->invested[1];

    memset(&ops, 0, sizeof(ops));
    ops.action_count = plo4_action_count;
    ops.action_at = plo4_action_at;
    ops.infoset_key = plo4_infoset_key;
    ops.strategy = plo4_strategy;
    ops.terminal_values = plo4_terminal_values;
    ops.combo_compatible = plo4_combo_compatible;
    if (pe_betting_vector_game_init(&game, &rules, &root, ctx.combo_count, &ops,
                                    &ctx) != PE_BETTING_OK)
    {
        free_ctx(&ctx);
        return PE_SOLVER_ERR_INVALID_CONFIG;
    }
    game.vector.combo_prior = plo4_combo_prior;

    status = pe_exploitability_vector(&game.vector, config, &measured);
    pe_betting_vector_game_destroy(&game);
    if (status != PE_SOLVER_OK)
    {
        free_ctx(&ctx);
        return status;
    }

    /* The vector lane averages over the combo width; the declared game is
     * normalised over legal pairs instead, so one factor turns its sums into
     * chips per hand under the joint range distribution. */
    scale = (double)ctx.combo_count / joint_mass;
    out_result->converged = measured.converged != 0 ? 1 : 0;
    out_result->showdown_path = path;
    out_result->combo_count = ctx.combo_count;
    out_result->live_combos[0] = ctx.live_combos[0];
    out_result->live_combos[1] = ctx.live_combos[1];
    out_result->joint_mass = joint_mass;
    for (player = 0u; player < PE_PLO4_CERT_PLAYERS; ++player)
    {
        out_result->policy_value[player] =
            measured.policy_value[player] * scale;
        out_result->br_value[player] = measured.br_value[player] * scale;
        out_result->br_gap[player] = measured.br_gap[player] * scale;
        out_result->br_iterations[player] = measured.br_iterations[player];
        out_result->nashconv += out_result->br_gap[player];
    }
    out_result->exploitability = out_result->nashconv * 0.5;
    fill_units(out_result, spec);

    if (!pe_finite_double(out_result->policy_value[0]) ||
        !pe_finite_double(out_result->policy_value[1]) ||
        !pe_finite_double(out_result->br_value[0]) ||
        !pe_finite_double(out_result->br_value[1]) ||
        !pe_finite_double(out_result->nashconv))
    {
        out_result->verdict = PE_PLO4_CERT_VERDICT_NOT_CERTIFIED;
        out_result->reason = PE_PLO4_CERT_REASON_INCOMPLETE;
        out_result->mode = "not_certified";
        out_result->guarantee = "none";
        free_ctx(&ctx);
        return PE_SOLVER_OK;
    }
    if (out_result->converged)
    {
        out_result->verdict = PE_PLO4_CERT_VERDICT_CERTIFIED;
        out_result->reason = PE_PLO4_CERT_REASON_NONE;
        out_result->mode = "exact";
        out_result->guarantee = "nash";
    }
    else
    {
        out_result->verdict = PE_PLO4_CERT_VERDICT_NOT_CERTIFIED;
        out_result->reason = PE_PLO4_CERT_REASON_NOT_CONVERGED;
        out_result->mode = "not_certified";
        out_result->guarantee = "none";
    }
    free_ctx(&ctx);
    return PE_SOLVER_OK;
}

const char *pe_plo4_cert_verdict_name(pe_plo4_cert_verdict_t verdict)
{
    switch (verdict)
    {
    case PE_PLO4_CERT_VERDICT_CERTIFIED: return "certified";
    case PE_PLO4_CERT_VERDICT_NOT_CERTIFIED: return "not_certified";
    case PE_PLO4_CERT_VERDICT_NOT_EVALUATED:
    default: return "not_evaluated";
    }
}

const char *pe_plo4_cert_reason_name(pe_plo4_cert_reason_t reason)
{
    switch (reason)
    {
    case PE_PLO4_CERT_REASON_INVALID_GAME: return "invalid_game";
    case PE_PLO4_CERT_REASON_NOT_CONVERGED: return "not_converged";
    case PE_PLO4_CERT_REASON_INCOMPLETE: return "incomplete";
    case PE_PLO4_CERT_REASON_SAMPLED: return "sampled";
    case PE_PLO4_CERT_REASON_NONE:
    default: return "none";
    }
}
