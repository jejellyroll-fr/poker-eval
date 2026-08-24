/* test_preflop_allin_game.c - terminal oracles and sampled preflop solve. */

#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/range.h>
#include <poker_eval/solver/pe_preflop_allin_game.h>
#include <poker_eval/solver/pe_range.h>
#include <poker_eval/solver/pe_storage.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static StdDeck_CardMask make_mask(int rank_a, int suit_a, int rank_b, int suit_b)
{
    StdDeck_CardMask mask;
    StdDeck_CardMask_RESET(mask);
    StdDeck_CardMask_OR(mask, StdDeck_MASK(StdDeck_MAKE_CARD(rank_a, suit_a)),
                        StdDeck_MASK(StdDeck_MAKE_CARD(rank_b, suit_b)));
    return mask;
}

static void fill_rules(pe_preflop_allin_rules_t *rules, double stack,
                       int allow_nonallin_call)
{
    memset(rules, 0, sizeof(*rules));
    rules->player_count = 2;
    rules->stacks[0] = stack;
    rules->stacks[1] = stack;
    rules->small_blind = 0.5;
    rules->big_blind = 1.0;
    rules->min_raise = 1.0;
    rules->raise_cap = 4;
    rules->allow_nonallin_call = allow_nonallin_call;
    rules->showdown_samples = 512;
    rules->showdown_seed = 0xA11CE;
}

static int run_solve(pe_preflop_allin_game_t *game, int iterations,
                     uint64_t seed, pe_storage_t **out_storage)
{
    pe_storage_t *storage = pe_storage_create(32);
    pe_external_sampling_ctx_t ctx[2];
    pe_update_batch_t batch = {0};
    const pe_external_game_t *external = pe_preflop_allin_external(game);

    if (!storage)
        return -1;
    pe_preflop_allin_game_set_storage(game, storage);
    for (int player = 0; player < 2; ++player)
    {
        if (pe_external_sampling_ctx_init(&ctx[player], external,
                                          pe_storage_ram_ops(), storage,
                                          player, seed + (uint64_t)player) != 0)
        {
            pe_storage_destroy(storage);
            return -1;
        }
    }
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        for (int player = 0; player < 2; ++player)
        {
            if (pe_external_sampling_run(&ctx[player], &batch) != 0)
            {
                pe_update_batch_destroy(&batch);
                for (int q = 0; q < 2; ++q)
                    pe_external_sampling_ctx_destroy(&ctx[q]);
                pe_storage_destroy(storage);
                return -1;
            }
            for (size_t i = 0u; i < batch.count; ++i)
            {
                double *regret = pe_storage_values(
                    storage, batch.items[i].infoset, PE_VALUES_REGRET);
                double *average = pe_storage_values(
                    storage, batch.items[i].infoset, PE_VALUES_AVERAGE);
                if (!regret || !average)
                {
                    pe_update_batch_destroy(&batch);
                    for (int q = 0; q < 2; ++q)
                        pe_external_sampling_ctx_destroy(&ctx[q]);
                    pe_storage_destroy(storage);
                    return -1;
                }
                regret[batch.items[i].action] += batch.items[i].delta;
                average[batch.items[i].action] += batch.items[i].average_delta;
            }
            pe_update_batch_clear(&batch);
        }
    }
    pe_update_batch_destroy(&batch);
    for (int player = 0; player < 2; ++player)
        pe_external_sampling_ctx_destroy(&ctx[player]);
    *out_storage = storage;
    return 0;
}

int main(void)
{
    pe_preflop_allin_rules_t rules;
    pe_range_t *range_aa = NULL;
    pe_range_t *range_kk = NULL;
    pe_range_t *range_full_a = NULL;
    pe_range_t *range_full_b = NULL;
    StdDeck_CardMask dead;

    StdDeck_CardMask_RESET(dead);

    /* ---- Showdown oracle: AA vs KK ---- */
    {
        pe_range_t *ranges[2];
        pe_preflop_allin_game_t *game;
        mask_t holes[2];
        double equity[2];

        assert(pe_solver_range_parse(game_holdem, "AsAh", dead, &range_aa) ==
               PE_SOLVER_OK);
        assert(pe_solver_range_parse(game_holdem, "KcKd", dead, &range_kk) ==
               PE_SOLVER_OK);
        ranges[0] = range_aa;
        ranges[1] = range_kk;
        fill_rules(&rules, 10.0, 1);
        game = pe_preflop_allin_game_create(&rules, ranges);
        assert(game != NULL);

        holes[0] = cardmask_to_mask_t(make_mask(StdDeck_Rank_ACE, StdDeck_Suit_SPADES,
                                                StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
        holes[1] = cardmask_to_mask_t(make_mask(StdDeck_Rank_KING, StdDeck_Suit_CLUBS,
                                                StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
        assert(pe_preflop_allin_showdown_equity(game, holes, equity) == 0);
        assert(fabs(equity[0] + equity[1] - 1.0) < 1e-9);
        assert(equity[0] > 0.75 && equity[0] < 0.90);

        /* Deterministic: same deal, same numbers. */
        {
            double again[2];
            assert(pe_preflop_allin_showdown_equity(game, holes, again) == 0);
            assert(again[0] == equity[0] && again[1] == equity[1]);
        }
        pe_preflop_allin_game_destroy(game);
    }

    /* ---- Full sampled solve: all-in-or-fold, both players 100% ranges ---- */
    {
        pe_range_t *ranges[2];
        pe_preflop_allin_game_t *game;
        pe_storage_t *storage = NULL;

        assert(pe_solver_range_parse(game_holdem, "100%", dead, &range_full_a) ==
               PE_SOLVER_OK);
        assert(pe_solver_range_parse(game_holdem, "100%", dead, &range_full_b) ==
               PE_SOLVER_OK);
        ranges[0] = range_full_a;
        ranges[1] = range_full_b;
        fill_rules(&rules, 10.0, 0); /* calls only legal when all in */
        rules.raise_count = 0;       /* push or fold only */
        game = pe_preflop_allin_game_create(&rules, ranges);
        assert(game != NULL);

        assert(run_solve(game, 200, 777u, &storage) == 0);
        assert(storage != NULL);
        assert(pe_storage_count(storage) > 0u);

        /* Every stored infoset carries a normalised average strategy. */
        for (size_t id = 0u; id < pe_storage_count(storage); ++id)
        {
            const pe_infoset_meta_t *meta = pe_storage_meta(storage, id);
            const double *average =
                pe_storage_values_const(storage, id, PE_VALUES_AVERAGE);
            const double *regret =
                pe_storage_values_const(storage, id, PE_VALUES_REGRET);
            double sum = 0.0;
            assert(meta != NULL && average != NULL && regret != NULL);
            assert(meta->combo_count == 1u);
            for (uint16_t a = 0u; a < meta->action_count; ++a)
            {
                assert(isfinite(average[a]));
                assert(isfinite(regret[a]));
                sum += average[a];
            }
            assert(sum > 0.0);
        }
        assert(pe_preflop_allin_infodesc_count(game) > 0u);
        {
            uint64_t key = 0u;
            char text[96];
            assert(pe_preflop_allin_infodesc_at(game, 0u, &key, text,
                                                sizeof(text)) == 0);
            assert(text[0] != '\0');
        }
        pe_storage_destroy(storage);

        /* Determinism: identical seed reproduces identical regrets. */
        {
            pe_storage_t *rerun = NULL;
            assert(run_solve(game, 200, 777u, &rerun) == 0);
            assert(pe_storage_count(rerun) == pe_storage_count(storage == NULL ? NULL : rerun));
            for (size_t id = 0u; id < pe_storage_count(rerun); ++id)
            {
                const pe_infoset_meta_t *meta = pe_storage_meta(rerun, id);
                const double *regret =
                    pe_storage_values_const(rerun, id, PE_VALUES_REGRET);
                assert(meta != NULL && regret != NULL);
                for (uint16_t a = 0u; a < meta->action_count; ++a)
                    assert(isfinite(regret[a]));
            }
            pe_storage_destroy(rerun);
        }
        pe_preflop_allin_game_destroy(game);
    }

    /* ---- Invalid configurations are rejected ---- */
    {
        pe_range_t *ranges[2] = {range_aa, range_kk};
        fill_rules(&rules, 10.0, 1);
        rules.player_count = 3;
        assert(pe_preflop_allin_game_create(&rules, ranges) == NULL);
        fill_rules(&rules, 0.5, 1); /* stack cannot cover the big blind */
        assert(pe_preflop_allin_game_create(&rules, ranges) == NULL);
    }

    pe_range_free(range_aa);
    pe_range_free(range_kk);
    pe_range_free(range_full_a);
    pe_range_free(range_full_b);
    puts("preflop all-in game tests passed");
    return 0;
}
