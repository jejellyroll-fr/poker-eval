#include <poker_eval/equity.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/deck/deck_std.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PE_EQUITY_DEFAULT_ITERATIONS 200000

/* Helper structure to hold allocated arrays for PlayerRange conversion */
typedef struct {
    StdDeck_CardMask *hand_masks[10];
    double *weights[10];
    int num_allocated;
} RangeConversionBuffers;

static void free_conversion_buffers(RangeConversionBuffers *buffers) {
    for (int i = 0; i < buffers->num_allocated; i++) {
        free(buffers->hand_masks[i]);
        free(buffers->weights[i]);
    }
    buffers->num_allocated = 0;
}

/* Helper to convert pe_range_t to PlayerRange for internal engine (Legacy Path) */
static int convert_ranges(const pe_range_t **ranges, int num_players, PlayerRange *out_ranges, RangeConversionBuffers *buffers) {
    buffers->num_allocated = 0;
    
    for (int i = 0; i < num_players; i++) {
        const pe_range_t *src = ranges[i];
        if (!src || src->count == 0) {
            free_conversion_buffers(buffers);
            return 0;
        }
        
        /* Allocate arrays for this player */
        buffers->hand_masks[i] = (StdDeck_CardMask *)malloc(src->count * sizeof(StdDeck_CardMask));
        buffers->weights[i] = (double *)malloc(src->count * sizeof(double));
        
        if (!buffers->hand_masks[i] || !buffers->weights[i]) {
            free_conversion_buffers(buffers);
            return 0;
        }
        buffers->num_allocated = i + 1;
        
        /* Copy data from pe_range_t combos to arrays */
        double total_weight = 0.0;
        for (size_t j = 0; j < src->count; j++) {
            buffers->hand_masks[i][j] = src->combos[j].hand;
            buffers->weights[i][j] = src->combos[j].weight;
            total_weight += src->combos[j].weight;
        }
        
        /* Initialize PlayerRange to point to the allocated arrays */
        out_ranges[i].hand_masks = buffers->hand_masks[i];
        out_ranges[i].weights = buffers->weights[i];
        out_ranges[i].count = (int)src->count;
        out_ranges[i].total_weight = total_weight;
    }
    return 1;
}

/*
 * Work budget for the automatic method selection below, in evaluated
 * showdowns. Comparable to a default Monte Carlo run, so choosing exhaustive
 * enumeration never costs noticeably more than sampling would have.
 */
#define PE_EQUITY_EXACT_WORK_BUDGET 5000000.0

/*
 * Automatic method selection, used when is_monte_carlo is 0 — which equity.h
 * documents as "use automatic heuristic", not as a request for exhaustive
 * enumeration.
 *
 * Enumerating every board is preferable when it is cheap, but the cost is the
 * number of board completions multiplied by the number of matchups. Preflop
 * that is C(48,5) boards against the product of the range sizes, which reaches
 * billions and would make a bounded-looking call run effectively forever. So
 * estimate the work and only enumerate below the budget.
 *
 * The estimate is deliberately pessimistic: cards_remaining ignores hole and
 * dead cards, and matchups ignores conflicts between players, so both are
 * over-counted and the answer errs towards sampling.
 */
static int auto_selects_exact(const PlayerRange ranges[], int num_players,
                              int to_deal, int cards_remaining) {
    if (to_deal <= 0)
        return 1; /* board already complete — there is nothing to sample */
    if (to_deal > 2)
        return 0; /* turn and river at most; beyond that it is never cheap */

    double boards = 1.0;
    for (int i = 0; i < to_deal; ++i)
        boards *= (double)(cards_remaining - i) / (double)(i + 1);

    double matchups = 1.0;
    for (int p = 0; p < num_players; ++p) {
        matchups *= (ranges[p].count > 0) ? (double)ranges[p].count : 1.0;
        if (boards * matchups > PE_EQUITY_EXACT_WORK_BUDGET)
            return 0; /* stop early rather than overflow on wide ranges */
    }

    return (boards * matchups) <= PE_EQUITY_EXACT_WORK_BUDGET;
}

/* Local accumulator structure */
typedef struct {
    double nwinhi[ENUM_MAXPLAYERS];
    double ntiehi[ENUM_MAXPLAYERS];
    double nlosehi[ENUM_MAXPLAYERS];
    double nwinlo[ENUM_MAXPLAYERS];
    double ntielo[ENUM_MAXPLAYERS];
    double nloselo[ENUM_MAXPLAYERS];
    double nscoop[ENUM_MAXPLAYERS];
    double ev[ENUM_MAXPLAYERS];
    double total_weight;
} equity_accumulator_t;

/* Helper to determine if game is Hi/Lo */
static int is_hilo(enum_game_t game) {
    switch (game) {
        case game_omaha8:
        case game_omaha85:
        case game_omaha86:
        case game_7stud8:
        case game_5draw8:
        case game_holdem8:
        case game_courchevel8:
        case game_pineapple8:
        case game_archie:
        case game_badugi_hilo:
            return 1;
        case game_holdem:
        case game_omaha:
        case game_omaha5:
        case game_omaha6:
        case game_7stud:
        case game_7studnsq:
        case game_razz:
        case game_5draw:
        case game_5drawnsq:
        case game_lowball:
        case game_lowball27:
        case game_sdholdem:
        case game_doubleflop_holdem:
        case game_drawmaha:
        case game_pineapple:
        case game_pineapple_crazy:
        case game_pineapple_lazy:
        case game_27_triple_draw:
        case game_a5_triple_draw:
        case game_badacey:
        case game_badeucy:
        case game_badugi:
        case game_fusion:
        case game_courchevel:
        case game_irish:
        case game_ofc:
        case game_manila:
        case game_royal:
        case game_astud:
        case game_italian:
        case game_drawmaha49:
        case game_drawmaha_zero:
        case game_drawmaha_dugi:
        case game_doubleboard_omaha85:
        case game_chinese13:
        case game_NUMGAMES:
        default:
            return 0;
    }
}

/* Mask Conversion Helper */
static mask_t std_to_modern(StdDeck_CardMask std) {
    mask_t m = 0;
    for (int i = 0; i < 52; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(std, i)) {
            int r = StdDeck_RANK(i);
            int s = StdDeck_SUIT(i);

            /* Map Suits */
            int modern_s = -1;
            switch(s) {
                case StdDeck_Suit_HEARTS:   modern_s = MODERN_SUIT_HEARTS; break;
                case StdDeck_Suit_DIAMONDS: modern_s = MODERN_SUIT_DIAMONDS; break;
                case StdDeck_Suit_CLUBS:    modern_s = MODERN_SUIT_CLUBS; break;
                case StdDeck_Suit_SPADES:   modern_s = MODERN_SUIT_SPADES; break;
                default: break;
            }

            if (modern_s >= 0) {
                m |= (1ULL << MODERN_MAKE_CARD(r, modern_s));
            }
        }
    }
    return m;
}

static void run_engine_recursive(
    EvalContext *ctx,
    enum_game_t game_variant,
    const pe_range_t **ranges,
    int num_players,
    int player_idx,
    StdDeck_CardMask *hands,
    double current_weight,
    StdDeck_CardMask dead_cards,
    StdDeck_CardMask board,
    equity_accumulator_t *acc
) {
    if (player_idx == num_players) {
        /* Base case: Evaluate */
        eval_t evals_hi[10];
        eval_t evals_lo[10];
        bool has_lo[10];
        int num_qual_lo = 0;

        int use_hilo = is_hilo(game_variant);

        for (int i=0; i<num_players; i++) {
            StdDeck_CardMask full_hand;
            StdDeck_CardMask_RESET(full_hand);
            StdDeck_CardMask_OR(full_hand, hands[i], board);

            mask_t modern_mask = std_to_modern(full_hand);

            if (use_hilo) {
                bool q = false;
                pe_eval_7c_hilo(ctx, modern_mask, &evals_hi[i], &evals_lo[i], &q);
                has_lo[i] = q;
                if (q) num_qual_lo++;
            } else {
                evals_hi[i] = pe_eval_7c(ctx, modern_mask);
                has_lo[i] = false;
            }
        }

        /* High winners */
        eval_t best_hi = 0;
        int winners_hi = 0;
        for (int i=0; i<num_players; i++) {
            if (evals_hi[i] > best_hi) {
                best_hi = evals_hi[i];
                winners_hi = 1;
            } else if (evals_hi[i] == best_hi) {
                winners_hi++;
            }
        }

        /* Low winners */
        eval_t best_lo = 0;
        int winners_lo = 0;
        if (num_qual_lo > 0) {
            best_lo = 0xFFFFFFFF;
            for (int i=0; i<num_players; i++) {
                if (has_lo[i]) {
                    if (evals_lo[i] < best_lo) {
                        best_lo = evals_lo[i];
                        winners_lo = 1;
                    } else if (evals_lo[i] == best_lo) {
                        winners_lo++;
                    }
                }
            }
        }

        /* Distribute Pot */
        double pot_hi = (num_qual_lo > 0) ? 0.5 : 1.0;
        double pot_lo = (num_qual_lo > 0) ? 0.5 : 0.0;

        for (int i=0; i<num_players; i++) {
            double share = 0.0;
            /* Track local win/tie to decide bucket updates */
            /* High Share */
            if (evals_hi[i] == best_hi) {
                share += pot_hi / (double)winners_hi;
                if (winners_hi == 1) acc->nwinhi[i] += current_weight;
                else acc->ntiehi[i] += current_weight;
            } else {
                acc->nlosehi[i] += current_weight;
            }

            /* Low Share */
            if (has_lo[i]) {
                if (evals_lo[i] == best_lo) {
                    share += pot_lo / (double)winners_lo;
                    if (winners_lo == 1) acc->nwinlo[i] += current_weight;
                    else acc->ntielo[i] += current_weight;
                } else {
                    acc->nloselo[i] += current_weight;
                }
            } else if (num_qual_lo > 0) {
                acc->nloselo[i] += current_weight;
            }

            acc->ev[i] += share * current_weight;

            /* Approximate Scoop */
            if (share > 0.99) acc->nscoop[i] += current_weight;
        }
        acc->total_weight += current_weight;
        return;
    }

    const pe_range_t *range = ranges[player_idx];
    if (range->count == 0) return;

    for (size_t i = 0; i < range->count; i++) {
        if (StdDeck_CardMask_ANY_SET(range->combos[i].hand, dead_cards)) {
            continue;
        }

        hands[player_idx] = range->combos[i].hand;

        StdDeck_CardMask next_dead;
        StdDeck_CardMask_OR(next_dead, dead_cards, range->combos[i].hand);

        run_engine_recursive(
            ctx,
            game_variant,
            ranges,
            num_players,
            player_idx + 1,
            hands,
            current_weight * range->combos[i].weight,
            next_dead,
            board,
            acc
        );
    }
}

static void fill_result_from_acc(const equity_accumulator_t *acc, int num_players, pe_equity_result_multi_t *result) {
    memset(result, 0, sizeof(pe_equity_result_multi_t));
    result->num_players = num_players;
    result->exact = 1;
    result->samples = (long)acc->total_weight;

    double total = acc->total_weight;
    if (total <= 0.000001) total = 1.0;

    for (int i = 0; i < num_players; i++) {
        result->results[i].equity = acc->ev[i] / total;
        result->results[i].ev = acc->ev[i] / total;

        result->results[i].win_prob = acc->nwinhi[i] / total;
        result->results[i].tie_prob = acc->ntiehi[i] / total;

        result->hilo_results[i].win_hi_prob = acc->nwinhi[i] / total;
        result->hilo_results[i].tie_hi_prob = acc->ntiehi[i] / total;
        result->hilo_results[i].win_lo_prob = acc->nwinlo[i] / total;
        result->hilo_results[i].tie_lo_prob = acc->ntielo[i] / total;
        result->hilo_results[i].scoop_prob = acc->nscoop[i] / total;
        result->hilo_results[i].equity_hi = result->results[i].equity;
        result->hilo_results[i].equity_lo = 0;
    }
}

static void fill_result_legacy(const enum_result_t *agg_res, int num_players, int use_mc, pe_equity_result_multi_t *result) {
    memset(result, 0, sizeof(pe_equity_result_multi_t));
    result->num_players = num_players;
    result->samples = agg_res->nsamples;
    result->exact = !use_mc;

    double total_games = (double)(agg_res->nwinhi[0] + agg_res->ntiehi[0] + agg_res->nlosehi[0]);
    if (total_games <= 0.000001) total_games = 1.0;

    for (int i = 0; i < num_players; i++) {
        result->results[i].equity = agg_res->ev[i];
        result->results[i].ev = agg_res->ev[i];

        result->results[i].win_prob = (double)agg_res->nwinhi[i] / total_games;
        result->results[i].tie_prob = (double)agg_res->ntiehi[i] / total_games;

        result->hilo_results[i].win_hi_prob = (double)agg_res->nwinhi[i] / total_games;
        result->hilo_results[i].tie_hi_prob = (double)agg_res->ntiehi[i] / total_games;
        result->hilo_results[i].win_lo_prob = (double)agg_res->nwinlo[i] / total_games;
        result->hilo_results[i].tie_lo_prob = (double)agg_res->ntielo[i] / total_games;
        result->hilo_results[i].scoop_prob = (double)agg_res->nscoop[i] / total_games;

        double eq_hi = 0.0;
        double eq_lo = 0.0;

        for (int h = 1; h <= num_players; h++) {
            eq_hi += (double)agg_res->nsharehi[i][h] * (1.0 / (double)h);
            eq_lo += (double)agg_res->nsharelo[i][h] * (1.0 / (double)h);
        }
        eq_hi /= total_games;
        eq_lo /= total_games;

        result->hilo_results[i].equity_hi = eq_hi;
        result->hilo_results[i].equity_lo = eq_lo;
    }
}

static pe_status_t pe_equity_legacy_fallback(
    enum_game_t game_variant,
    const pe_range_t **ranges,
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *result
) {
    PlayerRange internal_ranges[10];
    RangeConversionBuffers conv_buffers;
    memset(&conv_buffers, 0, sizeof(conv_buffers));

    if (!convert_ranges(ranges, num_players, internal_ranges, &conv_buffers)) {
        return PE_STATUS_ERROR;
    }

    int iter = opts ? (int)opts->iterations : PE_EQUITY_DEFAULT_ITERATIONS;
    if (iter <= 0) iter = PE_EQUITY_DEFAULT_ITERATIONS;

    /* Cleared, not allocated: CalculateEquityForRanges() below reaches an
     * enumeration that starts with enumResultClear(), so an ordering allocated
     * here would be dropped without being freed. */
    enum_result_t agg_res;
    enumResultClear(&agg_res);

    int cards_on_board = StdDeck_numCards(board);
    int total_board_cards = 5;

    if (game_variant >= game_7stud && game_variant <= game_razz) {
         total_board_cards = 0;
    }

    int to_deal = (total_board_cards > cards_on_board) ? (total_board_cards - cards_on_board) : 0;
    if (to_deal < 0) to_deal = 0;

    /* is_monte_carlo == 1 forces sampling; 0 (or no options at all) asks for
     * the automatic heuristic documented in equity.h. The previous form set
     * use_mc = 1 in both branches whenever cards remained, so "automatic"
     * always meant sampling and result->exact was never 1 on an incomplete
     * board. It now enumerates when that is provably cheap and samples
     * otherwise. */
    int use_mc;
    if (opts && opts->is_monte_carlo) {
        use_mc = 1;
    } else {
        use_mc = !auto_selects_exact(internal_ranges, num_players, to_deal,
                                     StdDeck_N_CARDS - cards_on_board);
    }
    if (to_deal == 0) {
        /* Board already complete: there is nothing random left to sample, so
         * enumerate even when Monte Carlo was forced. */
        use_mc = 0;
    }

    int ret = CalculateEquityForRanges(
        game_variant,
        internal_ranges,
        num_players,
        board,
        dead_cards,
        to_deal,
        use_mc,
        iter,
        1, /* orderflag */
        &agg_res
    );

    if (ret < 0) {
        enumResultFree(&agg_res);
        free_conversion_buffers(&conv_buffers);
        return PE_STATUS_ERROR;
    }

    fill_result_legacy(&agg_res, num_players, use_mc, result);

    enumResultFree(&agg_res);
    free_conversion_buffers(&conv_buffers);
    return PE_STATUS_OK;
}

pe_status_t pe_equity_multiway(
    EvalContext *ctx,
    enum_game_t game_variant,
    const pe_range_t **ranges,
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *result
) {
    if (!ranges || !result || num_players < 2 || num_players > 10) {
        return PE_STATUS_INVALID_ARG;
    }

    if (ctx != NULL) {
        /* Check board size by converting to mask_t and counting bits */
        mask_t modern_board = std_to_modern(board);
        int num_board = mask_popcount(modern_board);

        if (num_board < 5) {
            return pe_equity_legacy_fallback(
                game_variant,
                ranges,
                num_players,
                board,
                dead_cards,
                opts,
                result
            );
        }

        /* Exact path only supports 7-card (hold'em style) evaluation. */
        enum_gameparams_t *params = enumGameParams(game_variant);
        if (!params || params->minpocket != 2 || params->maxpocket != 2 || params->maxboard != 5) {
            return pe_equity_legacy_fallback(
                game_variant,
                ranges,
                num_players,
                board,
                dead_cards,
                opts,
                result
            );
        }

        equity_accumulator_t acc;
        memset(&acc, 0, sizeof(equity_accumulator_t));

        StdDeck_CardMask hands[10];
        StdDeck_CardMask all_dead;
        StdDeck_CardMask_OR(all_dead, dead_cards, board);

        run_engine_recursive(
            ctx,
            game_variant,
            ranges,
            num_players,
            0,
            hands,
            1.0,
            all_dead,
            board,
            &acc
        );

        fill_result_from_acc(&acc, num_players, result);
        return PE_STATUS_OK;
    }

    return pe_equity_legacy_fallback(
        game_variant,
        ranges,
        num_players,
        board,
        dead_cards,
        opts,
        result
    );
}

pe_status_t pe_equity_preflop(
    enum_game_t game_variant,
    const pe_range_t **ranges,
    int num_players,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *result
) {
    if (!ranges || !result || num_players < 2 || num_players > 10) {
        return PE_STATUS_INVALID_ARG;
    }

    StdDeck_CardMask board;
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    pe_equity_opts_t local_opts;
    if (opts) {
        local_opts = *opts;
    } else {
        memset(&local_opts, 0, sizeof(local_opts));
    }
    local_opts.is_monte_carlo = 1;
    if (local_opts.iterations <= 0) {
        local_opts.iterations = PE_EQUITY_DEFAULT_ITERATIONS;
    }

    return pe_equity_legacy_fallback(
        game_variant,
        ranges,
        num_players,
        board,
        dead,
        &local_opts,
        result
    );
}

pe_status_t pe_equity_range_vs_range(
    EvalContext *ctx,
    enum_game_t game_variant,
    const pe_range_t *r1,
    const pe_range_t *r2,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *result
) {
    const pe_range_t *ranges[2] = {r1, r2};
    return pe_equity_multiway(ctx, game_variant, ranges, 2, board, dead_cards, opts, result);
}

pe_status_t pe_equity_calculate(
    enum_game_t game_variant,
    const pe_range_t **ranges,
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    const pe_equity_opts_t *opts,
    pe_equity_result_multi_t *result
) {
    return pe_equity_multiway(NULL, game_variant, ranges, num_players, board, dead_cards, opts, result);
}
