/* All-in Hold'em showdown outcome model for the PKO range enumerator. */
#include <poker_eval/economics/pko.h>

#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/pcg_rng.h>

#include <string.h>

#define PE_PKO_SHOWDOWN_DEFAULT_SAMPLES 512
#define PE_PKO_SHOWDOWN_DEFAULT_SEED 0x9E3779B97F4A7C15ULL
#define PE_PKO_SHOWDOWN_MAX_PLAYERS 8

int pe_pko_outcome_showdown(const pe_pko_range_profile_t *profile,
                            int num_players,
                            double out_probability[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS],
                            void *user_data)
{
    const pe_pko_showdown_config_t *config =
        (const pe_pko_showdown_config_t *)user_data;
    EvalContext *context = NULL;
    int owns_context = 0;
    pe_rng_t rng;
    mask_t hole[PE_PKO_SHOWDOWN_MAX_PLAYERS];
    mask_t dead = MASK_EMPTY;
    uint64_t seed = PE_PKO_SHOWDOWN_DEFAULT_SEED;
    int samples = PE_PKO_SHOWDOWN_DEFAULT_SAMPLES;
    int player;
    int sample;

    if (!profile || !out_probability || num_players < 2 ||
        num_players > PE_PKO_SHOWDOWN_MAX_PLAYERS)
        return -1;
    memset(out_probability, 0,
           sizeof(double) * ICM_MAX_PLAYERS * ICM_MAX_PLAYERS);
    if (config)
    {
        seed = config->seed;
        if (config->board_samples > 0)
            samples = config->board_samples;
        context = (EvalContext *)config->context;
    }

    for (player = 0; player < num_players; ++player)
    {
        hole[player] = cardmask_to_mask_t(profile->hand[player]);
        if (mask_popcount(hole[player]) != 2 || (dead & hole[player]))
            return -1;
        dead |= hole[player];
    }

    if (!context)
    {
        EvalConfig eval_config = eval_config_holdem();
        context = eval_context_create(&eval_config);
        if (!context)
            return -1;
        owns_context = 1;
    }

    pe_rng_seed(&rng, seed);
    for (sample = 0; sample < samples; ++sample)
    {
        mask_t board = MASK_EMPTY;
        mask_t used = dead;
        int cards = 0;
        int winner = -1;
        int tied = 0;
        eval_t best = EVAL_INVALID;

        while (cards < 5)
        {
            int card = (int)pe_rng_below(&rng, 52u);
            mask_t bit = ((mask_t)1) << card;
            if (used & bit)
                continue;
            board |= bit;
            used |= bit;
            ++cards;
        }
        for (player = 0; player < num_players; ++player)
        {
            eval_t value = pe_eval_7c(context, hole[player] | board);
            if (value == EVAL_INVALID)
            {
                if (owns_context)
                    eval_context_destroy(context);
                return -1;
            }
            if (winner < 0 || value > best)
            {
                winner = player;
                best = value;
                tied = 0;
            }
            else if (value == best)
            {
                tied = 1;
            }
        }
        if (winner >= 0 && !tied)
        {
            int victim;
            for (victim = 0; victim < num_players; ++victim)
                if (victim != winner)
                    out_probability[winner][victim] += 1.0 / (double)samples;
        }
    }

    if (owns_context)
        eval_context_destroy(context);
    return 0;
}
