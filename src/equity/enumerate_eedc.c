/* enumerate_eedc.c -- Efficient Exhaustive Deck Construction (EEDC)
 * Optimized board generation without ANY_SET filtering.
 * Support for Hold'em, Omaha, Stud, Draw, etc.
 */

#include <poker_eval/equity/enumerate_eedc.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_short.h>
#include <poker_eval/deck/deck_joker.h>
#include <poker_eval/core/universal_deck.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/core/eval_cache.h>
#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/core/modern_combinations.h>
#include <poker_eval/utils/omp_compat.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/games/eval_joker.h>
#include <poker_eval/games/eval_joker_low8.h>
#include <poker_eval/games/eval_low27.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <poker_eval/core/string_compat.h>
#include <limits.h>
#include <stdbool.h>
#include <assert.h>
#ifdef PE_EEDC_CANONICALIZE
#include <poker_eval/equity/canonicalize.h>
#endif

typedef struct
{
    enum_game_t game;
    StdDeck_CardMask *pockets;
    int npockets;
    enum_result_t *result;
    int target_cards;
} stud_eedc_context_t;

typedef struct
{
    enum_game_t game;
    JokerDeck_CardMask *pockets;
    int npockets;
    enum_result_t *result;
} draw_joker_context_t;

static inline LowHandVal apply_low_qualifier(LowHandVal value, low_qualifier_t qualifier)
{
    if (value == LowHandVal_NOTHING || qualifier == LOW_QUALIFIER_NONE)
        return value;
    return pe_low_qualify5(value, qualifier) ? value : LowHandVal_NOTHING;
}

static void eedc_accumulate_results(enum_result_t *result,
                                    HandVal hival[],
                                    LowHandVal loval[],
                                    int npockets)
{
    HandVal besthi = HandVal_NOTHING;
    LowHandVal bestlo = LowHandVal_NOTHING;
    int hishare = 0;
    int loshare = 0;
    for (int i = 0; i < npockets; i++)
    {
        if (hival[i] != HandVal_NOTHING)
        {
            if (hival[i] > besthi)
            {
                besthi = hival[i];
                hishare = 1;
            }
            else if (hival[i] == besthi)
            {
                hishare++;
            }
        }
        if (loval[i] != LowHandVal_NOTHING)
        {
            if (loval[i] < bestlo)
            {
                bestlo = loval[i];
                loshare = 1;
            }
            else if (loval[i] == bestlo)
            {
                loshare++;
            }
        }
    }

    double hipot, lopot;
    if (bestlo != LowHandVal_NOTHING && besthi != HandVal_NOTHING)
    {
        hipot = (hishare != 0) ? 0.5 / hishare : 0;
        lopot = (loshare != 0) ? 0.5 / loshare : 0;
    }
    else if (bestlo == LowHandVal_NOTHING && besthi != HandVal_NOTHING)
    {
        hipot = (hishare != 0) ? 1.0 / hishare : 0;
        lopot = 0;
    }
    else if (bestlo != LowHandVal_NOTHING && besthi == HandVal_NOTHING)
    {
        hipot = 0;
        lopot = (loshare != 0) ? 1.0 / loshare : 0;
    }
    else
    {
        hipot = lopot = 0;
    }

    for (int i = 0; i < npockets; i++)
    {
        double potfrac = 0;
        int H = 0, L = 0;
        if (hival[i] != HandVal_NOTHING)
        {
            if (hival[i] == besthi)
            {
                H = hishare;
                potfrac += hipot;
                if (hishare == 1)
                    result->nwinhi[i]++;
                else
                    result->ntiehi[i]++;
            }
            else
            {
                result->nlosehi[i]++;
            }
        }
        if (loval[i] != LowHandVal_NOTHING)
        {
            if (loval[i] == bestlo)
            {
                L = loshare;
                potfrac += lopot;
                if (loshare == 1)
                    result->nwinlo[i]++;
                else
                    result->ntielo[i]++;
            }
            else
            {
                result->nloselo[i]++;
            }
        }
        result->nsharehi[i][H]++;
        result->nsharelo[i][L]++;
        result->nshare[i][H][L]++;
        if (besthi != HandVal_NOTHING &&
            hival[i] == besthi && hishare == 1 &&
            (bestlo == LowHandVal_NOTHING || (loval[i] == bestlo && loshare == 1)))
        {
            result->nscoop[i]++;
        }
        result->ev[i] += potfrac;
    }

    if (result->ordering != NULL)
    {
        if (result->ordering->mode == enum_ordering_mode_hi)
        {
            int hiranks[ENUM_ORDERING_MAXPLAYERS];
            ENUM_ORDERING_RANK_HI(hival, HandVal_NOTHING, npockets, hiranks);
            ENUM_ORDERING_INCREMENT(result->ordering, npockets, hiranks);
        }
        else if (result->ordering->mode == enum_ordering_mode_lo)
        {
            int loranks[ENUM_ORDERING_MAXPLAYERS];
            ENUM_ORDERING_RANK_LO(loval, LowHandVal_NOTHING, npockets, loranks);
            ENUM_ORDERING_INCREMENT(result->ordering, npockets, loranks);
        }
        else if (result->ordering->mode == enum_ordering_mode_hilo)
        {
            int hiranks[ENUM_ORDERING_MAXPLAYERS_HILO];
            int loranks[ENUM_ORDERING_MAXPLAYERS_HILO];
            ENUM_ORDERING_RANK_HI(hival, HandVal_NOTHING, npockets, hiranks);
            ENUM_ORDERING_RANK_LO(loval, LowHandVal_NOTHING, npockets, loranks);
            ENUM_ORDERING_INCREMENT_HILO(result->ordering, npockets,
                                         hiranks, loranks);
        }
    }

    result->nsamples++;
}

static int eedc_eval_std_unshared_assignment(const stud_eedc_context_t *ctx,
                                             StdDeck_CardMask extra_masks[])
{
    HandVal hival[ENUM_MAXPLAYERS];
    LowHandVal loval[ENUM_MAXPLAYERS];
    for (int i = 0; i < ctx->npockets; ++i)
    {
        StdDeck_CardMask fullhand;
        StdDeck_CardMask_OR(fullhand, ctx->pockets[i], extra_masks[i]);
        if (ctx->game == game_7stud)
        {
            hival[i] = StdDeck_StdRules_EVAL_N(fullhand, ctx->target_cards);
            loval[i] = LowHandVal_NOTHING;
        }
        else if (ctx->game == game_7stud8)
        {
            enum_gameparams_t *gp = enumGameParams(ctx->game);
            hival[i] = StdDeck_StdRules_EVAL_N(fullhand, ctx->target_cards);
            loval[i] = StdDeck_Lowball8_EVAL(fullhand, ctx->target_cards);
            loval[i] = apply_low_qualifier(loval[i], gp ? gp->low_qualifier : LOW_QUALIFIER_8);
        }
        else if (ctx->game == game_7studnsq)
        {
            hival[i] = StdDeck_StdRules_EVAL_N(fullhand, ctx->target_cards);
            loval[i] = pe_eval_low_a5(fullhand);
        }
        else if (ctx->game == game_razz)
        {
            hival[i] = HandVal_NOTHING;
            loval[i] = pe_eval_low_a5(fullhand);
        }
        else if (ctx->game == game_lowball27 || ctx->game == game_27_triple_draw)
        {
            hival[i] = HandVal_NOTHING;
            loval[i] = StdDeck_Lowball27_EVAL_N(fullhand, ctx->target_cards);
        }
        else if (ctx->game == game_a5_triple_draw)
        {
            hival[i] = HandVal_NOTHING;
            loval[i] = pe_eval_low_a5(fullhand);
        }
        else
        {
            return 1;
        }
    }
    eedc_accumulate_results(ctx->result, hival, loval, ctx->npockets);
    return 0;
}

static int eedc_assign_std_unshared_player(const stud_eedc_context_t *ctx,
                                           StdDeck_CardMask extra_masks[],
                                           const int need[],
                                           int player,
                                           int *deck,
                                           int ndeck,
                                           uint8_t used_cards[]);

static int eedc_select_cards_for_player(const stud_eedc_context_t *ctx,
                                        StdDeck_CardMask extra_masks[],
                                        const int need[],
                                        int player,
                                        int start_idx,
                                        int remaining,
                                        int *deck,
                                        int ndeck,
                                        uint8_t used_cards[])
{
    if (remaining == 0)
    {
        return eedc_assign_std_unshared_player(ctx, extra_masks, need, player + 1, deck, ndeck, used_cards);
    }
    for (int idx = start_idx; idx <= ndeck - remaining; ++idx)
    {
        int card = deck[idx];
        if (used_cards[card])
            continue;
        used_cards[card] = 1;
        StdDeck_CardMask prev = extra_masks[player];
        StdDeck_CardMask_SET(extra_masks[player], card);
        int err = eedc_select_cards_for_player(ctx, extra_masks, need, player, idx + 1,
                                               remaining - 1, deck, ndeck, used_cards);
        extra_masks[player] = prev;
        used_cards[card] = 0;
        if (err)
            return err;
    }
    return 0;
}

static int eedc_assign_std_unshared_player(const stud_eedc_context_t *ctx,
                                           StdDeck_CardMask extra_masks[],
                                           const int need[],
                                           int player,
                                           int *deck,
                                           int ndeck,
                                           uint8_t used_cards[])
{
    if (player >= ctx->npockets)
    {
        return eedc_eval_std_unshared_assignment(ctx, extra_masks);
    }
    StdDeck_CardMask_RESET(extra_masks[player]);
    if (need[player] == 0)
    {
        return eedc_assign_std_unshared_player(ctx, extra_masks, need, player + 1, deck, ndeck, used_cards);
    }
    return eedc_select_cards_for_player(ctx, extra_masks, need, player, 0, need[player],
                                        deck, ndeck, used_cards);
}

static int eedc_enumerate_std_unshared(enum_game_t game,
                                       StdDeck_CardMask pockets[],
                                       int npockets,
                                       int *deck,
                                       int ndeck,
                                       enum_result_t *result,
                                       int target_cards)
{
    stud_eedc_context_t ctx = {
        .game = game,
        .pockets = pockets,
        .npockets = npockets,
        .result = result,
        .target_cards = target_cards};

    StdDeck_CardMask extra_masks[ENUM_MAXPLAYERS];
    int need[ENUM_MAXPLAYERS];
    int total_needed = 0;
    for (int i = 0; i < npockets; ++i)
    {
        StdDeck_CardMask_RESET(extra_masks[i]);
        int have = StdDeck_numCards(pockets[i]);
        if (have > target_cards)
            return 1;
        need[i] = target_cards - have;
        total_needed += need[i];
    }

    if (total_needed == 0)
    {
        return eedc_eval_std_unshared_assignment(&ctx, extra_masks);
    }
    if (ndeck < total_needed)
        return 1;

    uint8_t used_cards[StdDeck_N_CARDS];
    memset(used_cards, 0, sizeof(used_cards));
    return eedc_assign_std_unshared_player(&ctx, extra_masks, need, 0, deck, ndeck, used_cards);
}

static int eedc_eval_draw_joker_assignment(const draw_joker_context_t *ctx,
                                           JokerDeck_CardMask extra_masks[])
{
    HandVal hival[ENUM_MAXPLAYERS];
    LowHandVal loval[ENUM_MAXPLAYERS];
    for (int i = 0; i < ctx->npockets; ++i)
    {
        JokerDeck_CardMask fullhand;
        JokerDeck_CardMask_OR(fullhand, ctx->pockets[i], extra_masks[i]);
        switch (ctx->game)
        {
        case game_5draw:
            hival[i] = JokerDeck_JokerRules_EVAL_N(fullhand, 5);
            loval[i] = LowHandVal_NOTHING;
            break;
        case game_5draw8:
        {
            enum_gameparams_t *gp = enumGameParams(ctx->game);
            hival[i] = JokerDeck_JokerRules_EVAL_N(fullhand, 5);
            loval[i] = JokerDeck_Lowball8_EVAL(fullhand, 5);
            loval[i] = apply_low_qualifier(loval[i], gp ? gp->low_qualifier : LOW_QUALIFIER_8);
            break;
        }
        case game_5drawnsq:
            hival[i] = JokerDeck_JokerRules_EVAL_N(fullhand, 5);
            loval[i] = JokerDeck_Lowball_EVAL(fullhand, 5);
            break;
        case game_lowball:
            hival[i] = HandVal_NOTHING;
            loval[i] = JokerDeck_Lowball_EVAL(fullhand, 5);
            break;
        default:
            hival[i] = HandVal_NOTHING;
            loval[i] = LowHandVal_NOTHING;
            break;
        }
    }
    eedc_accumulate_results(ctx->result, hival, loval, ctx->npockets);
    return 0;
}

static int eedc_assign_draw_joker_player(const draw_joker_context_t *ctx,
                                         JokerDeck_CardMask extra_masks[],
                                         const int need[],
                                         int player,
                                         int *deck,
                                         int ndeck,
                                         uint8_t used_cards[]);

static int eedc_select_cards_for_player_joker(const draw_joker_context_t *ctx,
                                              JokerDeck_CardMask extra_masks[],
                                              const int need[],
                                              int player,
                                              int start_idx,
                                              int remaining,
                                              int *deck,
                                              int ndeck,
                                              uint8_t used_cards[])
{
    if (remaining == 0)
    {
        return eedc_assign_draw_joker_player(ctx, extra_masks, need, player + 1, deck, ndeck, used_cards);
    }
    for (int idx = start_idx; idx <= ndeck - remaining; ++idx)
    {
        int card = deck[idx];
        if (used_cards[card])
            continue;
        used_cards[card] = 1;
        JokerDeck_CardMask prev = extra_masks[player];
        JokerDeck_CardMask_SET(extra_masks[player], card);
        int err = eedc_select_cards_for_player_joker(ctx, extra_masks, need, player, idx + 1,
                                                     remaining - 1, deck, ndeck, used_cards);
        extra_masks[player] = prev;
        used_cards[card] = 0;
        if (err)
            return err;
    }
    return 0;
}

static int eedc_assign_draw_joker_player(const draw_joker_context_t *ctx,
                                         JokerDeck_CardMask extra_masks[],
                                         const int need[],
                                         int player,
                                         int *deck,
                                         int ndeck,
                                         uint8_t used_cards[])
{
    if (player >= ctx->npockets)
    {
        return eedc_eval_draw_joker_assignment(ctx, extra_masks);
    }
    JokerDeck_CardMask_RESET(extra_masks[player]);
    if (need[player] == 0)
    {
        return eedc_assign_draw_joker_player(ctx, extra_masks, need, player + 1, deck, ndeck, used_cards);
    }
    return eedc_select_cards_for_player_joker(ctx, extra_masks, need, player, 0, need[player],
                                              deck, ndeck, used_cards);
}

static int eedc_enumerate_draw_joker(enum_game_t game,
                                     StdDeck_CardMask pockets[],
                                     StdDeck_CardMask effective_dead,
                                     int npockets,
                                     enum_result_t *result)
{
    draw_joker_context_t ctx = {
        .game = game,
        .pockets = NULL,
        .npockets = npockets,
        .result = result};

    JokerDeck_CardMask jpockets[ENUM_MAXPLAYERS];
    JokerDeck_CardMask extra_masks[ENUM_MAXPLAYERS];
    int need[ENUM_MAXPLAYERS];
    int total_needed = 0;
    for (int i = 0; i < npockets; ++i)
    {
        Universal_ConvertStdToJoker(pockets[i], &jpockets[i]);
        JokerDeck_CardMask_RESET(extra_masks[i]);
        int have = JokerDeck_numCards(jpockets[i]);
        if (have > 5)
            return 1;
        need[i] = 5 - have;
        total_needed += need[i];
    }
    ctx.pockets = jpockets;

    JokerDeck_CardMask jdeads;
    Universal_ConvertStdToJoker(effective_dead, &jdeads);

    int deck[JokerDeck_N_CARDS];
    int ndeck = 0;
    for (int card = 0; card < JokerDeck_N_CARDS; ++card)
    {
        if (!JokerDeck_CardMask_CARD_IS_SET(jdeads, card))
        {
            deck[ndeck++] = card;
        }
    }

    if (total_needed == 0)
    {
        return eedc_eval_draw_joker_assignment(&ctx, extra_masks);
    }
    if (ndeck < total_needed)
        return 1;

    uint8_t used_cards[JokerDeck_N_CARDS];
    memset(used_cards, 0, sizeof(used_cards));
    return eedc_assign_draw_joker_player(&ctx, extra_masks, need, 0, deck, ndeck, used_cards);
}

// Génère toutes les combinaisons de k indices parmi n (lexicographique)
static int next_combination(int *indices, int k, int n) {
    int i = k - 1;
    while (i >= 0 && indices[i] == n - k + i) i--;
    if (i < 0) return 0;
    indices[i]++;
    for (int j = i + 1; j < k; j++)
        indices[j] = indices[j - 1] + 1;
    return 1;
}

#define EEDC_DEFAULT_BOARD_CHUNK 64
#define EEDC_DEFAULT_PARALLEL_THRESHOLD 400
#define EEDC_MAX_OMAHA_HOLE_COMBOS 15
#define EEDC_MAX_BOARD_TRIPLES 10

typedef struct
{
    StdDeck_CardMask combos[EEDC_MAX_OMAHA_HOLE_COMBOS];
    int count;
} eedc_hole_combo_cache_t;

typedef struct
{
    StdDeck_CardMask combos[EEDC_MAX_BOARD_TRIPLES];
    int count;
} eedc_board_triple_cache_t;

typedef struct
{
#ifdef PE_EEDC_CANONICALIZE
    uint64_t *seen;
    unsigned char *used;
    size_t cap;
#else
    int unused;
#endif
} eedc_canonicalizer_t;

static void eedc_result_prepare(enum_result_t *res, enum_game_t game, int npockets)
{
    memset(res, 0, sizeof(*res));
    res->game = game;
    res->nplayers = npockets;
    res->sampleType = ENUM_EXHAUSTIVE;
}

static void eedc_result_merge(enum_result_t *dst, const enum_result_t *src)
{
    dst->nsamples += src->nsamples;
    for (int i = 0; i < ENUM_MAXPLAYERS; ++i)
    {
        dst->nwinhi[i] += src->nwinhi[i];
        dst->ntiehi[i] += src->ntiehi[i];
        dst->nlosehi[i] += src->nlosehi[i];
        dst->nwinlo[i] += src->nwinlo[i];
        dst->ntielo[i] += src->ntielo[i];
        dst->nloselo[i] += src->nloselo[i];
        dst->nscoop[i] += src->nscoop[i];
        dst->ev[i] += src->ev[i];
        for (int h = 0; h <= ENUM_MAXPLAYERS; ++h)
        {
            dst->nsharehi[i][h] += src->nsharehi[i][h];
            dst->nsharelo[i][h] += src->nsharelo[i][h];
            for (int l = 0; l <= ENUM_MAXPLAYERS; ++l)
            {
                dst->nshare[i][h][l] += src->nshare[i][h][l];
            }
        }
    }
}

static void eedc_canonicalizer_init(eedc_canonicalizer_t *canon, int ndeck)
{
#ifdef PE_EEDC_CANONICALIZE
    if (!canon)
        return;
    memset(canon, 0, sizeof(*canon));
    size_t req = (size_t)ndeck * 128u;
    if (req < 1024u)
        req = 1024u;
    if (req > (1u << 22))
        req = (1u << 22);
    canon->cap = req;
    canon->seen = (uint64_t *)calloc(canon->cap, sizeof(uint64_t));
    canon->used = (unsigned char *)calloc(canon->cap, sizeof(unsigned char));
    if (!canon->seen || !canon->used) {
        free(canon->seen);
        free(canon->used);
        canon->cap = 0;
        return;
    }
#else
    (void)canon;
    (void)ndeck;
#endif
}

static bool eedc_canonicalizer_accept(eedc_canonicalizer_t *canon, StdDeck_CardMask board)
{
#ifdef PE_EEDC_CANONICALIZE
    if (!canon || !canon->seen || !canon->used || canon->cap == 0)
        return true;
    uint64_t h = pe_board5_canonical_hash64(board);
    if (!h)
        return true;
    size_t i0 = (size_t)(h % canon->cap);
    size_t slot = i0;
    for (size_t k = 0; k < canon->cap; ++k)
    {
        size_t idx = (i0 + k) % canon->cap;
        if (!canon->used[idx])
        {
            slot = idx;
            break;
        }
        if (canon->seen[idx] == h)
        {
            return false;
        }
    }
    canon->used[slot] = 1;
    canon->seen[slot] = h;
    return true;
#else
    (void)canon;
    (void)board;
    return true;
#endif
}

static void eedc_canonicalizer_destroy(eedc_canonicalizer_t *canon)
{
#ifdef PE_EEDC_CANONICALIZE
    if (!canon)
        return;
    free(canon->seen);
    free(canon->used);
    canon->seen = NULL;
    canon->used = NULL;
    canon->cap = 0;
#else
    (void)canon;
#endif
}

static void eedc_build_hole_combo_cache(const StdDeck_CardMask pocket,
                                        eedc_hole_combo_cache_t *cache)
{
    cache->count = 0;
    mask_t hole_mask = cardmask_to_mask_t(pocket);
    card_list_t cards = mask_to_list(hole_mask);
    if (cards.count < 2)
        return;
    for (int i = 0; i < cards.count - 1; ++i)
    {
        for (int j = i + 1; j < cards.count; ++j)
        {
            assert(cache->count < EEDC_MAX_OMAHA_HOLE_COMBOS);
            StdDeck_CardMask combo;
            StdDeck_CardMask_RESET(combo);
            StdDeck_CardMask_SET(combo, cards.cards[i]);
            StdDeck_CardMask_SET(combo, cards.cards[j]);
            cache->combos[cache->count++] = combo;
        }
    }
}

static void eedc_build_board_triple_cache(const StdDeck_CardMask board,
                                          eedc_board_triple_cache_t *cache)
{
    cache->count = 0;
    mask_t board_mask = cardmask_to_mask_t(board);
    card_list_t cards = mask_to_list(board_mask);
    if (cards.count < 3)
        return;
    for (int i = 0; i < cards.count - 2; ++i)
    {
        for (int j = i + 1; j < cards.count - 1; ++j)
        {
            for (int k = j + 1; k < cards.count; ++k)
            {
                assert(cache->count < EEDC_MAX_BOARD_TRIPLES);
                StdDeck_CardMask combo;
                StdDeck_CardMask_RESET(combo);
                StdDeck_CardMask_SET(combo, cards.cards[i]);
                StdDeck_CardMask_SET(combo, cards.cards[j]);
                StdDeck_CardMask_SET(combo, cards.cards[k]);
                cache->combos[cache->count++] = combo;
            }
        }
    }
}

typedef void (*eedc_board_eval_fn)(StdDeck_CardMask fullBoard,
                                   void *userdata,
                                   enum_result_t *local_result);

static int eedc_parse_positive_env(const char *name, int fallback)
{
    if (!name)
        return fallback;
    const char *env = getenv(name);
    if (!env || !*env)
        return fallback;
    char *endptr = NULL;
    long value = strtol(env, &endptr, 10);
    if (endptr == env || *endptr != '\0' || value <= 0)
        return fallback;
    if (value > INT_MAX)
        return fallback;
    return (int)value;
}

static int eedc_should_bind_threads(void)
{
    const char *env = getenv("PE_EEDC_PROC_BIND");
    if (!env || !*env)
        env = getenv("OMP_PROC_BIND");
    if (!env || !*env)
        return 0;
    if (strcasecmp(env, "true") == 0 || strcasecmp(env, "1") == 0 ||
        strcasecmp(env, "yes") == 0 || strcasecmp(env, "on") == 0)
        return 1;
    return 0;
}

static int eedc_get_chunk_size(void)
{
    static int cached = 0;
    if (cached > 0)
        return cached;
    int value = eedc_parse_positive_env("PE_EEDC_CHUNK_SIZE", EEDC_DEFAULT_BOARD_CHUNK);
    cached = value > 0 ? value : EEDC_DEFAULT_BOARD_CHUNK;
    return cached;
}

static int eedc_get_parallel_threshold(void)
{
    static int cached = 0;
    if (cached > 0)
        return cached;
    int value = eedc_parse_positive_env("PE_EEDC_PARALLEL_THRESHOLD", EEDC_DEFAULT_PARALLEL_THRESHOLD);
    cached = value > 0 ? value : EEDC_DEFAULT_PARALLEL_THRESHOLD;
    return cached;
}

static int eedc_resolve_worker_count(uint64_t total_boards)
{
    int max_threads = omp_get_max_threads();
    if (max_threads <= 0)
        max_threads = 1;
    const char *env = getenv("PE_EEDC_THREADS");
    if (env && *env)
    {
        /* strtol: atoi returns 0 for unparseable input, which was
         * indistinguishable from an explicit 0 and silently ignored. */
        char *end = NULL;
        long requested;
        errno = 0;
        requested = strtol(env, &end, 10);
        if (end != env && *end == '\0' && errno != ERANGE &&
            requested > 0 && requested < (long)max_threads)
            max_threads = (int)requested;
    }
    if (total_boards < eedc_get_parallel_threshold())
        return 1;
    return max_threads;
}

static void eedc_process_board_chunk(const StdDeck_CardMask *boards,
                                     int count,
                                     eedc_board_eval_fn eval_fn,
                                     void *userdata,
                                     enum_result_t *thread_results,
                                     int worker_count,
                                     enum_result_t *result)
{
#if defined(_OPENMP)
    if (worker_count > 1 && count >= worker_count)
    {
        int actual_workers = worker_count;
        int idx;
        if (eedc_should_bind_threads())
#if defined(_MSC_VER)
#pragma omp parallel for schedule(static) num_threads(actual_workers)
#else
#pragma omp parallel for schedule(static) num_threads(actual_workers) proc_bind(close)
#endif
        for (idx = 0; idx < count; ++idx)
        {
            int tid = omp_get_thread_num();
            eval_fn(boards[idx], userdata, &thread_results[tid]);
        }
        else
#pragma omp parallel for schedule(static) num_threads(actual_workers)
        for (idx = 0; idx < count; ++idx)
        {
            int tid = omp_get_thread_num();
            eval_fn(boards[idx], userdata, &thread_results[tid]);
        }
    }
    else
#endif
    {
        for (int idx = 0; idx < count; ++idx)
        {
            eval_fn(boards[idx], userdata, &thread_results[0]);
        }
    }

    for (int t = 0; t < worker_count; ++t)
    {
        eedc_result_merge(result, &thread_results[t]);
        eedc_result_prepare(&thread_results[t], result->game, result->nplayers);
    }
}

static int eedc_enumerate_board_slices(int *deck,
                                       int ndeck,
                                       int n_needed,
                                       StdDeck_CardMask board,
                                       uint64_t total_boards,
                                       eedc_board_eval_fn eval_fn,
                                       void *userdata,
                                       enum_result_t *result)
{
    if (n_needed == 0)
    {
        eval_fn(board, userdata, result);
        return 0;
    }

    if (ndeck < n_needed)
        return 1;

    int worker_count = eedc_resolve_worker_count(total_boards);
    enum_result_t *thread_results = (enum_result_t *)calloc(worker_count, sizeof(enum_result_t));
    if (!thread_results)
        return 1;
    for (int t = 0; t < worker_count; ++t)
    {
        eedc_result_prepare(&thread_results[t], result->game, result->nplayers);
    }

    int chunk_size = eedc_get_chunk_size();
    if (chunk_size <= 0)
        chunk_size = EEDC_DEFAULT_BOARD_CHUNK;
    StdDeck_CardMask *chunk_boards = (StdDeck_CardMask *)malloc(sizeof(StdDeck_CardMask) * (size_t)chunk_size);
    if (!chunk_boards)
    {
        free(thread_results);
        return 1;
    }
    int chunk_count = 0;
    int indices[5] = {0, 1, 2, 3, 4};
    for (int i = 0; i < n_needed; ++i)
        indices[i] = i;

    eedc_canonicalizer_t canon;
    eedc_canonicalizer_init(&canon, ndeck);

    do
    {
        StdDeck_CardMask sharedCards;
        StdDeck_CardMask_RESET(sharedCards);
        for (int i = 0; i < n_needed; ++i)
        {
            StdDeck_CardMask_SET(sharedCards, deck[indices[i]]);
        }
        StdDeck_CardMask fullBoard;
        StdDeck_CardMask_OR(fullBoard, board, sharedCards);
        if (!eedc_canonicalizer_accept(&canon, fullBoard))
            continue;
        chunk_boards[chunk_count++] = fullBoard;
        if (chunk_count == chunk_size)
        {
            eedc_process_board_chunk(chunk_boards, chunk_count, eval_fn, userdata,
                                     thread_results, worker_count, result);
            chunk_count = 0;
        }
    } while (next_combination(indices, n_needed, ndeck));

    if (chunk_count > 0)
    {
        eedc_process_board_chunk(chunk_boards, chunk_count, eval_fn, userdata,
                                 thread_results, worker_count, result);
    }

    eedc_canonicalizer_destroy(&canon);
    free(chunk_boards);
    free(thread_results);
    return 0;
}

typedef struct
{
    enum_game_t game;
    const StdDeck_CardMask *pockets;
    int npockets;
} eedc_holdem_eval_ctx_t;

static void eedc_evaluate_board_holdem(StdDeck_CardMask fullBoard,
                                       void *userdata,
                                       enum_result_t *local_result)
{
    const eedc_holdem_eval_ctx_t *ctx = (const eedc_holdem_eval_ctx_t *)userdata;
    HandVal hival[ENUM_MAXPLAYERS];
    LowHandVal loval[ENUM_MAXPLAYERS];
    for (int i = 0; i < ctx->npockets; ++i)
    {
        StdDeck_CardMask hand;
        StdDeck_CardMask_OR(hand, ctx->pockets[i], fullBoard);
        hival[i] = StdDeck_StdRules_EVAL_N_Cached(hand, 7);
        if (ctx->game == game_holdem8)
        {
            enum_gameparams_t *gp = enumGameParams(ctx->game);
            loval[i] = StdDeck_Lowball8_EVAL(hand, 7);
            loval[i] = apply_low_qualifier(loval[i], gp ? gp->low_qualifier : LOW_QUALIFIER_8);
        }
        else
        {
            loval[i] = LowHandVal_NOTHING;
        }
    }
    eedc_accumulate_results(local_result, hival, loval, ctx->npockets);
}

typedef struct
{
    enum_game_t game;
    const StdDeck_CardMask *pockets;
    const eedc_hole_combo_cache_t *hole_cache;
    int npockets;
    bool eval_low;
    low_qualifier_t qualifier;
} eedc_omaha_eval_ctx_t;

static void eedc_evaluate_board_omaha(StdDeck_CardMask fullBoard,
                                      void *userdata,
                                      enum_result_t *local_result)
{
    const eedc_omaha_eval_ctx_t *ctx = (const eedc_omaha_eval_ctx_t *)userdata;
    eedc_board_triple_cache_t board_cache;
    eedc_build_board_triple_cache(fullBoard, &board_cache);

    HandVal hival[ENUM_MAXPLAYERS];
    LowHandVal loval[ENUM_MAXPLAYERS];
    for (int p = 0; p < ctx->npockets; ++p)
    {
        HandVal besthi = HandVal_NOTHING;
        LowHandVal bestlo = LowHandVal_NOTHING;
        const eedc_hole_combo_cache_t *holes = &ctx->hole_cache[p];
        for (int h = 0; h < holes->count; ++h)
        {
            for (int b = 0; b < board_cache.count; ++b)
            {
                StdDeck_CardMask combo;
                StdDeck_CardMask_OR(combo, holes->combos[h], board_cache.combos[b]);
                HandVal hi = StdDeck_StdRules_EVAL_N(combo, 5);
                if (hi > besthi || besthi == HandVal_NOTHING)
                    besthi = hi;
                if (ctx->eval_low)
                {
                    LowHandVal lo = StdDeck_Lowball8_EVAL(combo, 5);
                    lo = apply_low_qualifier(lo, ctx->qualifier);
                    if (lo != LowHandVal_NOTHING &&
                        (bestlo == LowHandVal_NOTHING || lo < bestlo))
                    {
                        bestlo = lo;
                    }
                }
            }
        }
        hival[p] = besthi;
        loval[p] = ctx->eval_low ? bestlo : LowHandVal_NOTHING;
    }
    eedc_accumulate_results(local_result, hival, loval, ctx->npockets);
}

static int eedc_run_predecoded_holdem(enum_game_t game,
                                      StdDeck_CardMask pockets[],
                                      int npockets,
                                      StdDeck_CardMask board,
                                      int *deck,
                                      int ndeck,
                                      int nboard,
                                      enum_result_t *result)
{
    int n_needed = 5 - nboard;
    if (n_needed < 0 || ndeck < n_needed)
        return 1;
    uint64_t total_boards = combo_binomial((uint32_t)ndeck, (uint32_t)n_needed);
    eedc_holdem_eval_ctx_t ctx = {
        .game = game,
        .pockets = pockets,
        .npockets = npockets};
    return eedc_enumerate_board_slices(deck, ndeck, n_needed, board, total_boards,
                                       eedc_evaluate_board_holdem, &ctx, result);
}

static int eedc_run_predecoded_omaha(enum_game_t game,
                                     StdDeck_CardMask pockets[],
                                     int npockets,
                                     StdDeck_CardMask board,
                                     int *deck,
                                     int ndeck,
                                     int nboard,
                                     enum_result_t *result)
{
    eedc_hole_combo_cache_t hole_cache[ENUM_MAXPLAYERS];
    for (int i = 0; i < npockets; ++i)
    {
        eedc_build_hole_combo_cache(pockets[i], &hole_cache[i]);
    }

    int n_needed = 5 - nboard;
    if (n_needed < 0 || ndeck < n_needed)
        return 1;

    bool eval_low = (game == game_omaha8 || game == game_omaha85 || game == game_omaha86);
    enum_gameparams_t *gp = enumGameParams(game);
    low_qualifier_t qualifier = (eval_low && gp) ? gp->low_qualifier : LOW_QUALIFIER_NONE;

    eedc_omaha_eval_ctx_t ctx = {
        .game = game,
        .pockets = pockets,
        .hole_cache = hole_cache,
        .npockets = npockets,
        .eval_low = eval_low,
        .qualifier = qualifier};

    uint64_t total_boards = combo_binomial((uint32_t)ndeck, (uint32_t)n_needed);
    return eedc_enumerate_board_slices(deck, ndeck, n_needed, board, total_boards,
                                       eedc_evaluate_board_omaha, &ctx, result);
}


int enumExhaustive_eedc(
    enum_game_t game,
    StdDeck_CardMask pockets[],
    StdDeck_CardMask board,
    StdDeck_CardMask dead,
    int npockets,
    int nboard,
    int orderflag,
    enum_result_t *result
) {
    enumResultClear(result);
    result->game = game;
    result->nplayers = npockets;
    result->sampleType = ENUM_EXHAUSTIVE;

    if (orderflag) {
        enum_ordering_mode_t mode;
        switch (game) {
            case game_holdem:
            case game_omaha:
            case game_omaha5:
            case game_omaha6:
            case game_7stud:
            case game_5draw:
            case game_sdholdem:
                mode = enum_ordering_mode_hi;
                break;
            case game_razz:
            case game_lowball:
            case game_lowball27:
                mode = enum_ordering_mode_lo;
                break;
            case game_holdem8:
            case game_omaha8:
            case game_omaha85:
            case game_omaha86:
            case game_7stud8:
            case game_7studnsq:
            case game_5draw8:
            case game_5drawnsq:
                mode = enum_ordering_mode_hilo;
                break;
            case game_doubleflop_holdem:
                mode = enum_ordering_mode_hihi;
                break;
            default:
                mode = enum_ordering_mode_hi;
                break;
        }
        if (enumResultAlloc(result, npockets, mode))
            return 1;
    }

    // Construire l'ensemble des cartes interdites (dead + pockets + board)
    StdDeck_CardMask effective_dead = dead;
    for (int i = 0; i < npockets; ++i) {
        StdDeck_CardMask_OR(effective_dead, effective_dead, pockets[i]);
    }
    StdDeck_CardMask_OR(effective_dead, effective_dead, board);

    // Construire le deck restant (cartes non dans effective_dead)
    int deck[StdDeck_N_CARDS];
    int ndeck = 0;
    for (int c = 0; c < StdDeck_N_CARDS; ++c) {
        if (!StdDeck_CardMask_CARD_IS_SET(effective_dead, c)) {
            deck[ndeck++] = c;
        }
    }

    // Hold'em & Hold'em8
    if (game == game_holdem || game == game_holdem8)
    {
        return eedc_run_predecoded_holdem(game, pockets, npockets, board,
                                          deck, ndeck, nboard, result);
    }
    // ShortDeck Hold'em stays on the legacy path (different evaluator)
    else if (game == game_sdholdem)
    {
        int n_needed = 5 - nboard;
        if (n_needed < 0 || ndeck < n_needed)
            return 1;
        int indices[5] = {0, 1, 2, 3, 4};
        for (int i = 0; i < n_needed; ++i)
            indices[i] = i;
        do
        {
            StdDeck_CardMask sharedCards;
            StdDeck_CardMask_RESET(sharedCards);
            for (int i = 0; i < n_needed; ++i)
            {
                StdDeck_CardMask_SET(sharedCards, deck[indices[i]]);
            }
            StdDeck_CardMask fullBoard;
            StdDeck_CardMask_OR(fullBoard, board, sharedCards);
            int i;
            HandVal hival[ENUM_MAXPLAYERS];
            LowHandVal loval[ENUM_MAXPLAYERS];
            for (i = 0; i < npockets; i++)
            {
                StdDeck_CardMask hand;
                StdDeck_CardMask_OR(hand, pockets[i], fullBoard);
                hival[i] = StdDeck_StdRules_EVAL_N(hand, 7);
                loval[i] = LowHandVal_NOTHING;
            }
            eedc_accumulate_results(result, hival, loval, npockets);
        } while (next_combination(indices, n_needed, ndeck));
        return 0;
    }
    // Omaha, Omaha5, Omaha6, Omaha8, Omaha85, Omaha86
    else if (game == game_omaha || game == game_omaha5 || game == game_omaha6 || game == game_omaha8 || game == game_omaha85 || game == game_omaha86)
    {
        return eedc_run_predecoded_omaha(game, pockets, npockets, board,
                                         deck, ndeck, nboard, result);
    }
    // 7stud, 7stud8, 7studnsq, Razz
    else if (game == game_7stud || game == game_7stud8 || game == game_7studnsq || game == game_razz) {
        return eedc_enumerate_std_unshared(game, pockets, npockets, deck, ndeck, result, 7);
    }
    // 5draw, 5draw8, 5drawnsq, Lowball (Joker deck)
    else if (game == game_5draw || game == game_5draw8 || game == game_5drawnsq || game == game_lowball) {
        if (nboard != 0)
            return 1;
        return eedc_enumerate_draw_joker(game, pockets, effective_dead, npockets, result);
    }
    // Lowball27, Triple Draws (Std deck draw games)
    else if (game == game_lowball27 || game == game_27_triple_draw || game == game_a5_triple_draw) {
        if (nboard != 0)
            return 1;
        return eedc_enumerate_std_unshared(game, pockets, npockets, deck, ndeck, result, 5);
    }
    // Doubleflop Hold'em (deux boards communs de 5 cartes)
    else if (game == game_doubleflop_holdem) {
        int n_needed = 10 - nboard;
        if (n_needed < 0 || ndeck < n_needed) return 1;
        int indices[10];
        for (int i = 0; i < n_needed; ++i) indices[i] = i;
        do {
            StdDeck_CardMask board1, board2;
            StdDeck_CardMask_RESET(board1);
            StdDeck_CardMask_RESET(board2);
            for (int i = 0; i < 5; ++i) StdDeck_CardMask_SET(board1, deck[indices[i]]);
            for (int i = 5; i < 10; ++i) StdDeck_CardMask_SET(board2, deck[indices[i]]);
            // Évaluation double board (simplifiée)
            int i;
            HandVal hival1[ENUM_MAXPLAYERS], hival2[ENUM_MAXPLAYERS];
            HandVal besthi1 = HandVal_NOTHING, besthi2 = HandVal_NOTHING;
            int hishare1 = 0, hishare2 = 0;
            double hipot1, hipot2;
            for (i = 0; i < npockets; i++) {
                StdDeck_CardMask _hand1, _hand2;
                StdDeck_CardMask_OR(_hand1, pockets[i], board1);
                StdDeck_CardMask_OR(_hand2, pockets[i], board2);
                hival1[i] = StdDeck_StdRules_EVAL_N(_hand1, 7);
                hival2[i] = StdDeck_StdRules_EVAL_N(_hand2, 7);
                if (hival1[i] != HandVal_NOTHING) {
                    if (hival1[i] > besthi1) { besthi1 = hival1[i]; hishare1 = 1; }
                    else if (hival1[i] == besthi1) { hishare1++; }
                }
                if (hival2[i] != HandVal_NOTHING) {
                    if (hival2[i] > besthi2) { besthi2 = hival2[i]; hishare2 = 1; }
                    else if (hival2[i] == besthi2) { hishare2++; }
                }
            }
            hipot1 = besthi1 != HandVal_NOTHING ? 1.0 / hishare1 : 0;
            hipot2 = besthi2 != HandVal_NOTHING ? 1.0 / hishare2 : 0;
            for (i = 0; i < npockets; i++) {
                double potfrac1 = 0, potfrac2 = 0;
                if (hival1[i] == besthi1) potfrac1 += hipot1;
                if (hival2[i] == besthi2) potfrac2 += hipot2;
                result->ev[i] += (potfrac1 + potfrac2) / 2;
                if (hival1[i] == besthi1 && hishare1 == 1 &&
                    (besthi2 == HandVal_NOTHING || (hival2[i] == besthi2 && hishare2 == 1))) result->nscoop[i]++;
            }
            result->nsamples++;
        } while (next_combination(indices, n_needed, ndeck));
        return 0;
    }
    // Par défaut, fallback sur la version classique
    return enumExhaustive(game, pockets, board, dead, npockets, nboard, orderflag, result);
}
