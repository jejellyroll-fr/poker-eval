/*
 * test_mpf_hilo8.c - Omaha Hi/Lo 8-or-better showdowns in the multiway
 * postflop adapter (issue #237).
 *
 * Hi/Lo is a payoff model on top of PLO4/5/6, selected by
 * mpf_config_t::showdown, so everything below goes through the production
 * game builder and the production utility callback.
 *
 *  1. Hand-built showdowns whose shares are worked out by hand: high only,
 *     low qualification (including the 8-low boundary and a 9-low that does
 *     not play), the wheel, a counterfeited low, scoop, chop, quarter, tied
 *     high, tied low, multiway and side pots, a folded low, rake, and the
 *     two-plus-three rule for both halves. Each is a PLO4 hand; the ones that
 *     matter for hand size are repeated with PLO5 and PLO6 holdings.
 *  2. Random deals for PLO4, PLO5 and PLO6, two to four players, uneven
 *     investments, folded players and rake, checked against an oracle written
 *     here. The oracle shares nothing with the code under test: it ranks the
 *     high with the legacy StdDeck evaluator (the adapter uses pe_eval_7c),
 *     ranks the low from card ranks directly (the adapter uses the legacy
 *     Omaha evaluator), and computes the side pots its own way.
 *  3. The build refuses Hi/Lo outside Omaha, with a high-strength
 *     abstraction, and for an unknown showdown value.
 *  4. Solver smoke on a heads-up river for each hand size: the player holding
 *     only the nut low loses the ante at equilibrium when the pot is high-only
 *     and breaks even when it is split, so the solve must land on those two
 *     values.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/core/cardmask_compat.h>
#include <poker_eval/games/eval_omaha.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        if (!(cond))                                                       \
        {                                                                  \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

#define MAX_P 4

/* ---------------------------------------------------------------- *
 * Cards
 * ---------------------------------------------------------------- */

static int parse_card(const char *text)
{
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "cdhs";
    const char *r = strchr(ranks, text[0]);
    const char *s = strchr(suits, text[1]);
    if (!text[0] || !text[1] || !r || !s)
        return -1;
    return MODERN_MAKE_CARD((int)(r - ranks), (int)(s - suits));
}

/* "AcKd..." (spaces allowed) into a mask; the card count goes to *count. */
static mask_t parse_cards(const char *text, int *out, int *count)
{
    mask_t mask = MASK_EMPTY;
    int n = 0;
    while (*text)
    {
        if (*text == ' ')
        {
            ++text;
            continue;
        }
        int card = parse_card(text);
        if (card < 0)
        {
            fprintf(stderr, "bad card in \"%s\"\n", text);
            exit(2);
        }
        if (out)
            out[n] = card;
        mask = mask_set(mask, card);
        ++n;
        text += 2;
    }
    if (count)
        *count = n;
    return mask;
}

static int hole_count(mpf_rule_t rules)
{
    return rules == MPF_RULE_PLO5 ? 5 : rules == MPF_RULE_PLO6 ? 6 : 4;
}

/* ---------------------------------------------------------------- *
 * Production showdown
 * ---------------------------------------------------------------- */

typedef struct
{
    mpf_rule_t rules;
    mpf_showdown_t showdown;
    int players;
    mask_t hole[MAX_P];
    int board[5];
    double invested[MAX_P];
    int active[MAX_P];
    rake_config_t rake;
} spot_t;

static void base_config(mpf_config_t *cfg, const EvalContext *ctx,
                        mpf_rule_t rules, mpf_showdown_t showdown, int players)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->ctx = ctx;
    cfg->rules = rules;
    cfg->showdown = showdown;
    cfg->num_players = players;
    cfg->start_street = MPF_STREET_RIVER;
    cfg->sb = 0.5;
    cfg->bb = 1.0;
    for (int p = 0; p < players; ++p)
        cfg->stacks[p] = 1000.0;
}

/* The utilities the production callback reports for a showdown at the
   spot's investments. The state is built by mpf_build_game, then moved to a
   showdown with the spot's investments and folds: the payoff under test
   reads nothing else. */
static int production_showdown(const EvalContext *ctx, const spot_t *spot,
                               double *out)
{
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t st;

    base_config(&cfg, ctx, spot->rules, spot->showdown, spot->players);
    cfg.rake = spot->rake;
    cfg.board_card_count = 5;
    memcpy(cfg.board_cards, spot->board, sizeof(spot->board));
    for (int p = 0; p < spot->players; ++p)
    {
        cfg.hole[p] = spot->hole[p];
        cfg.hole_specified[p] = 1;
    }
    if (mpf_build_game(&cfg, &game, &st) != 0)
        return -1;

    st.street = MPF_STREET_SHOWDOWN;
    st.util_ready = 0;
    st.pot = 0.0;
    for (int p = 0; p < spot->players; ++p)
    {
        st.invested[p] = spot->invested[p];
        st.active[p] = spot->active[p];
        st.pot += spot->invested[p];
    }
    for (int p = 0; p < spot->players; ++p)
        out[p] = game.get_utility(&game, (uint64_t)(uintptr_t)&st, p, NULL);
    mpf_state_cleanup(&st);
    return 0;
}

/* ---------------------------------------------------------------- *
 * Independent oracle
 * ---------------------------------------------------------------- */

/* High: best of every two-hole, three-board five-card hand, ranked by the
   legacy StdDeck evaluator. */
static HandVal oracle_high(const int *hole, int nh, const int *board)
{
    HandVal best = HandVal_NOTHING;
    for (int i = 0; i < nh; ++i)
        for (int j = i + 1; j < nh; ++j)
            for (int a = 0; a < 5; ++a)
                for (int b = a + 1; b < 5; ++b)
                    for (int c = b + 1; c < 5; ++c)
                    {
                        mask_t five = MASK_EMPTY;
                        five = mask_set(five, hole[i]);
                        five = mask_set(five, hole[j]);
                        five = mask_set(five, board[a]);
                        five = mask_set(five, board[b]);
                        five = mask_set(five, board[c]);
                        HandVal v = StdDeck_StdRules_EVAL_N(
                            mask_t_to_cardmask(five), 5);
                        if (v > best)
                            best = v;
                    }
    return best;
}

/* Low rank of a card: ace 1, two 2 ... eight 8, anything else 0 (no low). */
static int low_rank(int card)
{
    int rank = card % 13; /* MODERN_MAKE_CARD(rank, suit) = rank + 13 * suit */
    if (rank == MODERN_RANK_A)
        return 1;
    if (rank <= MODERN_RANK_8)
        return rank + 2;
    return 0;
}

/* Low: every two-hole, three-board hand whose five ranks are distinct and
   all A..8, compared highest card first (a smaller key is a better low).
   Returns 0 when there is no qualifying low, else writes the key. */
static int oracle_low(const int *hole, int nh, const int *board, int key[5])
{
    int found = 0;
    for (int i = 0; i < nh; ++i)
        for (int j = i + 1; j < nh; ++j)
            for (int a = 0; a < 5; ++a)
                for (int b = a + 1; b < 5; ++b)
                    for (int c = b + 1; c < 5; ++c)
                    {
                        int r[5] = {low_rank(hole[i]), low_rank(hole[j]),
                                    low_rank(board[a]), low_rank(board[b]),
                                    low_rank(board[c])};
                        int ok = 1;
                        for (int x = 0; x < 5 && ok; ++x)
                        {
                            if (r[x] == 0)
                                ok = 0;
                            for (int y = x + 1; y < 5 && ok; ++y)
                                if (r[x] == r[y])
                                    ok = 0;
                        }
                        if (!ok)
                            continue;
                        /* sort descending */
                        for (int x = 0; x < 5; ++x)
                            for (int y = x + 1; y < 5; ++y)
                                if (r[y] > r[x])
                                {
                                    int t = r[x];
                                    r[x] = r[y];
                                    r[y] = t;
                                }
                        int better = !found;
                        for (int x = 0; x < 5 && !better; ++x)
                        {
                            if (r[x] != key[x])
                            {
                                better = r[x] < key[x];
                                break;
                            }
                        }
                        if (better)
                        {
                            memcpy(key, r, sizeof(r));
                            found = 1;
                        }
                    }
    return found;
}

static int low_cmp(const int *a, const int *b)
{
    for (int x = 0; x < 5; ++x)
        if (a[x] != b[x])
            return a[x] < b[x] ? -1 : 1;
    return 0;
}

/* Net result per player. Each side pot is the slice of the investments
   between two consecutive distinct levels, taken from everyone who reached
   it and contested by the survivors among them; nobody left to contest it
   means it goes back. Rake is taken from each contested slice before the
   halves are made. */
static void oracle_showdown(const spot_t *spot, double *out)
{
    int holes[MAX_P][6];
    int nh[MAX_P];
    HandVal high[MAX_P];
    int low_ok[MAX_P];
    int low_key[MAX_P][5];
    const int n = spot->players;

    for (int p = 0; p < n; ++p)
    {
        nh[p] = 0;
        for (int card = 0; card < 52; ++card)
            if (mask_is_set(spot->hole[p], card))
                holes[p][nh[p]++] = card;
        high[p] = oracle_high(holes[p], nh[p], spot->board);
        low_ok[p] = spot->showdown == MPF_SHOWDOWN_HILO8 &&
                    oracle_low(holes[p], nh[p], spot->board, low_key[p]);
        out[p] = -spot->invested[p];
    }

    double done = 0.0;
    for (;;)
    {
        /* next level above what has been settled */
        double level = -1.0;
        for (int p = 0; p < n; ++p)
            if (spot->invested[p] > done + 1e-12 &&
                (level < 0.0 || spot->invested[p] < level))
                level = spot->invested[p];
        if (level < 0.0)
            break;

        int payers = 0;
        int contest[MAX_P];
        int nc = 0;
        for (int p = 0; p < n; ++p)
        {
            if (spot->invested[p] >= level - 1e-12)
            {
                payers++;
                if (spot->active[p])
                    contest[nc++] = p;
            }
        }
        double slice = (level - done) * payers;
        if (nc == 0)
        {
            for (int p = 0; p < n; ++p)
                if (spot->invested[p] >= level - 1e-12)
                    out[p] += level - done;
            done = level;
            continue;
        }
        slice = pe_apply_rake(slice, &spot->rake);

        HandVal best_high = HandVal_NOTHING;
        for (int k = 0; k < nc; ++k)
            if (high[contest[k]] > best_high)
                best_high = high[contest[k]];
        int nhw = 0;
        for (int k = 0; k < nc; ++k)
            if (high[contest[k]] == best_high)
                nhw++;

        const int *best_low = NULL;
        for (int k = 0; k < nc; ++k)
        {
            int p = contest[k];
            if (low_ok[p] && (!best_low || low_cmp(low_key[p], best_low) < 0))
                best_low = low_key[p];
        }
        int nlw = 0;
        if (best_low)
            for (int k = 0; k < nc; ++k)
                if (low_ok[contest[k]] &&
                    low_cmp(low_key[contest[k]], best_low) == 0)
                    nlw++;

        double high_half = nlw ? slice / 2.0 : slice;
        for (int k = 0; k < nc; ++k)
        {
            int p = contest[k];
            if (high[p] == best_high)
                out[p] += high_half / nhw;
            if (nlw && low_ok[p] && low_cmp(low_key[p], best_low) == 0)
                out[p] += (slice - high_half) / nlw;
        }
        done = level;
    }
}

/* ---------------------------------------------------------------- *
 * Hand-built showdowns
 * ---------------------------------------------------------------- */

typedef struct
{
    const char *name;
    mpf_rule_t rules;
    int players;
    const char *hole[MAX_P];
    const char *board;
    double invested[MAX_P];
    int folded[MAX_P];
    double rake_pct;
    double rake_cap;
    double hilo[MAX_P]; /* expected net result, Hi/Lo */
    int check_high;     /* also check the high-only game */
    double high[MAX_P]; /* expected net result, high only */
} hand_case_t;

static void spot_from_case(const hand_case_t *hc, mpf_showdown_t showdown,
                           spot_t *spot)
{
    int count;
    memset(spot, 0, sizeof(*spot));
    spot->rules = hc->rules;
    spot->showdown = showdown;
    spot->players = hc->players;
    parse_cards(hc->board, spot->board, &count);
    if (count != 5)
    {
        fprintf(stderr, "%s: board needs 5 cards\n", hc->name);
        exit(2);
    }
    for (int p = 0; p < hc->players; ++p)
    {
        spot->hole[p] = parse_cards(hc->hole[p], NULL, &count);
        if (count != hole_count(hc->rules))
        {
            fprintf(stderr, "%s: player %d holds %d cards\n", hc->name, p,
                    count);
            exit(2);
        }
        spot->invested[p] = hc->invested[p];
        spot->active[p] = !hc->folded[p];
    }
    spot->rake.percentage = hc->rake_pct;
    spot->rake.cap = hc->rake_cap;
}

static void check_case(const EvalContext *ctx, const hand_case_t *hc)
{
    for (int mode = 0; mode < 2; ++mode)
    {
        const mpf_showdown_t showdown =
            mode == 0 ? MPF_SHOWDOWN_HILO8 : MPF_SHOWDOWN_HIGH;
        const double *want = mode == 0 ? hc->hilo : hc->high;
        spot_t spot;
        double got[MAX_P], oracle[MAX_P];
        double sum = 0.0, invested = 0.0, raked = 0.0;

        if (mode == 1 && !hc->check_high)
            continue;
        spot_from_case(hc, showdown, &spot);
        if (production_showdown(ctx, &spot, got) != 0)
        {
            CHECK(0, "%s: the game did not build", hc->name);
            continue;
        }
        oracle_showdown(&spot, oracle);
        for (int p = 0; p < hc->players; ++p)
        {
            CHECK(fabs(got[p] - want[p]) < 1e-9,
                  "%s (%s): player %d nets %.6f, expected %.6f", hc->name,
                  mode == 0 ? "hi/lo" : "high", p, got[p], want[p]);
            CHECK(fabs(oracle[p] - want[p]) < 1e-9,
                  "%s (%s): the oracle nets player %d %.6f, expected %.6f "
                  "(the hand-worked value or the oracle is wrong)",
                  hc->name, mode == 0 ? "hi/lo" : "high", p, oracle[p],
                  want[p]);
            sum += got[p];
            invested += hc->invested[p];
        }
        /* Zero-sum up to the rake actually taken. */
        raked = invested - pe_apply_rake(invested, &spot.rake);
        if (hc->rake_pct == 0.0)
            CHECK(fabs(sum) < 1e-9, "%s: results sum to %.12f, not 0",
                  hc->name, sum);
        else
            CHECK(sum < 0.0 && sum >= -raked - 1e-9,
                  "%s: results sum to %.6f with at most %.6f raked",
                  hc->name, sum, raked);
    }
}

/* Board 2c 4d 7h Kc Qs carries three low ranks (2, 4, 7), so a low is
   possible; A and B below both make 7-5-4-3-2 with a 3 and a 5. */
static const hand_case_t k_cases[] = {
    /* No board with three low ranks, no low: the high takes it all. */
    {"high only, no low on board", MPF_RULE_PLO4, 2,
     {"As Ad 3c 4c", "Ks Kd 5c 6c"}, "Kh Qd 9c 7s 2h",
     {10, 10}, {0, 0}, 0, 0, {-10, 10}, 1, {-10, 10}},

    /* A: A3 + 258 is 8-5-3-2-A. B: trip kings, no low. */
    {"high and low split", MPF_RULE_PLO4, 2,
     {"Ac 3d Js Jd", "Kd Kh 9s Ts"}, "2c 5d 8h Kc Qs",
     {10, 10}, {0, 0}, 0, 0, {0, 0}, 1, {-10, 10}},

    /* A's only low cards are 8 and 9: 9-8-7-6-2 does not qualify. */
    {"a nine low does not qualify", MPF_RULE_PLO4, 2,
     {"9c 8d Js Jd", "Kd Kh Ts Td"}, "2c 6d 7h Kc Qs",
     {10, 10}, {0, 0}, 0, 0, {-10, 10}, 0, {0}},

    /* 8-7-6-3-2: the worst hand that still qualifies. */
    {"an eight low qualifies", MPF_RULE_PLO4, 2,
     {"8c 3d Js Jd", "Kd Kh Ts Td"}, "2c 6d 7h Kc Qs",
     {10, 10}, {0, 0}, 0, 0, {0, 0}, 0, {0}},

    /* A: A5 + 234 is both the nut low and a straight, which beats B's trip
       kings. The straight does not spoil the low, and A scoops. */
    {"the wheel scoops", MPF_RULE_PLO4, 2,
     {"As 5d Jh Js", "6s 7d Kd Kh"}, "2c 3d 4h Kc Qs",
     {10, 10}, {0, 0}, 0, 0, {10, -10}, 0, {0}},

    /* A holds A2 but the board has A2 too: every two-card choice pairs the
       board, so A has no low at all. B's 87 plays 8-7-3-2-A and takes the
       low; A's aces up take the high. */
    {"a counterfeited low", MPF_RULE_PLO4, 2,
     {"Ad 2d Ks Js", "7c 8h Tc Td"}, "As 2c 3d Kc Qh",
     {10, 10}, {0, 0}, 0, 0, {0, 0}, 1, {10, -10}},

    /* The issue's example: both tie the low, B also wins the high.
       A 25%, B 75%. */
    {"quartered heads-up", MPF_RULE_PLO4, 2,
     {"3c 5d Js 9s", "3h 5s Kd Kh"}, "2c 4d 7h Kc Qs",
     {10, 10}, {0, 0}, 0, 0, {-5, 5}, 1, {-10, 10}},

    /* Same kings-full-free high (K K Q J 7) and the same low: a chop. */
    {"chopped both ways", MPF_RULE_PLO4, 2,
     {"3c 5d Kd Js", "3h 5s Kh Jd"}, "2c 4d 7h Kc Qs",
     {10, 10}, {0, 0}, 0, 0, {0, 0}, 1, {0, 0}},

    /* Both make the ten-high straight; A's 7-6-5-2-A beats B's 7-6-5-4-3.
       A 75%, B 25%. */
    {"tied high, one low", MPF_RULE_PLO4, 2,
     {"9c Td As 2d", "9h Ts 3c 4h"}, "5c 6d 7h 8s Kc",
     {10, 10}, {0, 0}, 0, 0, {5, -5}, 1, {0, 0}},

    /* C takes the high half; A and B tie the low half. */
    {"multiway, tied low", MPF_RULE_PLO4, 3,
     {"3c 5d Js 9s", "3h 5s Td 9d", "Kd Kh Ts Jc"}, "2c 4d 7h Kc Qs",
     {10, 10, 10}, {0, 0, 0}, 0, 0, {-2.5, -2.5, 5}, 1, {-10, -10, 20}},

    /* A is all-in for 10. Main pot 30: B's high, A's low. Side pot 40
       between B and C: B's high, C's A-6 low (A's better low cannot reach
       it). A +5, B +5, C -10. */
    {"side pot, low in both pots", MPF_RULE_PLO4, 3,
     {"3c 5d Js 9s", "Kd Kh Ts Jc", "Ad 6s Qd Qh"}, "2c 4d 7h Kc Qs",
     {10, 30, 30}, {0, 0, 0}, 0, 0, {5, 5, -10}, 1, {-10, 40, -30}},

    /* As above, but C has no low: the side pot is high-only for B. */
    {"side pot without a low", MPF_RULE_PLO4, 3,
     {"3c 5d Js 9s", "Kd Kh Ts Jc", "Qd Qh 9c 8d"}, "2c 4d 7h Kc Qs",
     {10, 30, 30}, {0, 0, 0}, 0, 0, {5, 25, -30}, 1, {-10, 40, -30}},

    /* C folded the best low (7-4-3-2-A). Folded chips stay in, but C cannot
       win: A's 7-5-4-3-2 takes the low half. */
    {"a folded low cannot win", MPF_RULE_PLO4, 3,
     {"3c 5d Js 9s", "Kd Kh Ts Jc", "Ad 3d Qd Qh"}, "2c 4d 7h Kc Qs",
     {10, 10, 10}, {0, 0, 1}, 0, 0, {5, 5, -10}, 0, {0}},

    /* Rake comes off the pot before the halves: 5% of 20 leaves 19,
       9.5 each. */
    {"rake before the split", MPF_RULE_PLO4, 2,
     {"Ac 3d Js Jd", "Kd Kh 9s Ts"}, "2c 5d 8h Kc Qs",
     {10, 10}, {0, 0}, 0.05, 0, {-0.5, -0.5}, 1, {-10, 9}},

    /* Quartered after rake: 19 left, B 9.5 + 4.75, A 4.75. */
    {"rake then quarter", MPF_RULE_PLO4, 2,
     {"3c 5d Js 9s", "3h 5s Kd Kh"}, "2c 4d 7h Kc Qs",
     {10, 10}, {0, 0}, 0.05, 0, {-5.25, 4.25}, 0, {0}},

    /* Rake cap: 5% of 20 capped at 0.5 leaves 19.5, 9.75 each. */
    {"capped rake before the split", MPF_RULE_PLO4, 2,
     {"Ac 3d Js Jd", "Kd Kh 9s Ts"}, "2c 5d 8h Kc Qs",
     {10, 10}, {0, 0}, 0.05, 0.5, {-0.25, -0.25}, 0, {0}},

    /* Two plus three for the high: A holds one heart with four on board,
       which is no flush in Omaha. B's tens beat A's nines, and B's 7-5 low
       takes the other half: B scoops. */
    {"one suited card is no flush", MPF_RULE_PLO4, 2,
     {"Jh 9c 9d 4s", "Tc Ts 5c 7d"}, "Ah Kh Qh 2h 3c",
     {10, 10}, {0, 0}, 0, 0, {-10, 10}, 1, {-10, 10}},

    /* Two plus three for the low: A holds four low cards but the board has
       only two low ranks, so no hand can play a low. */
    {"hole cards alone make no low", MPF_RULE_PLO6, 2,
     {"Ah 4s 5d 6c 8h 9d", "Kd Ks Tc Td 7s 7d"}, "2c 3d Kh Qs Jc",
     {10, 10}, {0, 0}, 0, 0, {-10, 10}, 0, {0}},

    /* PLO5 and PLO6: the same quarter, with extra cards that change
       neither half. */
    {"quartered heads-up, PLO5", MPF_RULE_PLO5, 2,
     {"3c 5d Js 9s 9h", "3h 5s Kd Kh Tc"}, "2c 4d 7h Kc Qs",
     {10, 10}, {0, 0}, 0, 0, {-5, 5}, 1, {-10, 10}},
    {"quartered heads-up, PLO6", MPF_RULE_PLO6, 2,
     {"3c 5d Js 9s 9h Jd", "3h 5s Kd Kh Tc Td"}, "2c 4d 7h Kc Qs",
     {10, 10}, {0, 0}, 0, 0, {-5, 5}, 1, {-10, 10}},

    /* PLO5 and PLO6 side pots: the low in each pot is decided among the
       players who can win that pot. */
    {"side pot, low in both pots, PLO5", MPF_RULE_PLO5, 3,
     {"3c 5d Js 9s 9h", "Kd Kh Ts Jc Th", "Ad 6s Qd Qh Jh"},
     "2c 4d 7h Kc Qs",
     {10, 30, 30}, {0, 0, 0}, 0, 0, {5, 5, -10}, 0, {0}},
    {"multiway, tied low, PLO6", MPF_RULE_PLO6, 3,
     {"3c 5d Js 9s 9h 8c", "3h 5s Td 9d 9c 8s", "Kd Kh Ts Jc Jh Th"},
     "2c 4d 7h Kc Qs",
     {10, 10, 10}, {0, 0, 0}, 0, 0, {-2.5, -2.5, 5}, 0, {0}},
};

static void test_hand_cases(const EvalContext *ctx)
{
    printf("  hand-built showdowns (%zu)\n",
           sizeof(k_cases) / sizeof(k_cases[0]));
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); ++i)
        check_case(ctx, &k_cases[i]);
}

/* ---------------------------------------------------------------- *
 * Random deals against the oracle
 * ---------------------------------------------------------------- */

static uint64_t rng_next(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static void test_random_deals(const EvalContext *ctx, mpf_rule_t rules,
                              const char *label, int deals, uint64_t seed)
{
    static const double levels[] = {5.0, 10.0, 10.0, 20.0, 40.0};
    const int nh = hole_count(rules);
    int lows = 0, splits = 0, mismatches = 0;

    for (int d = 0; d < deals; ++d)
    {
        int deck[52];
        spot_t spot;
        double got[MAX_P], want[MAX_P];

        for (int c = 0; c < 52; ++c)
            deck[c] = c;
        for (int c = 51; c > 0; --c)
        {
            int j = (int)(rng_next(&seed) % (uint64_t)(c + 1));
            int t = deck[c];
            deck[c] = deck[j];
            deck[j] = t;
        }

        memset(&spot, 0, sizeof(spot));
        spot.rules = rules;
        spot.showdown = MPF_SHOWDOWN_HILO8;
        spot.players = 2 + (int)(rng_next(&seed) % 3u);
        int next = 0;
        for (int b = 0; b < 5; ++b)
            spot.board[b] = deck[next++];
        int survivors = 0;
        for (int p = 0; p < spot.players; ++p)
        {
            for (int k = 0; k < nh; ++k)
                spot.hole[p] = mask_set(spot.hole[p], deck[next++]);
            spot.invested[p] = levels[rng_next(&seed) % 5u];
            spot.active[p] = (rng_next(&seed) % 5u) != 0u;
            survivors += spot.active[p];
        }
        if (survivors < 2)
        {
            spot.active[0] = 1;
            spot.active[1] = 1;
        }
        if (d % 4 == 3)
        {
            spot.rake.percentage = 0.05;
            spot.rake.cap = 3.0;
        }

        if (production_showdown(ctx, &spot, got) != 0)
        {
            CHECK(0, "%s deal %d: the game did not build", label, d);
            continue;
        }
        oracle_showdown(&spot, want);
        int bad = 0;
        for (int p = 0; p < spot.players; ++p)
            if (fabs(got[p] - want[p]) > 1e-9)
                bad = 1;
        if (bad)
        {
            if (mismatches++ < 5)
            {
                fprintf(stderr, "%s deal %d disagrees with the oracle:\n",
                        label, d);
                for (int p = 0; p < spot.players; ++p)
                    fprintf(stderr,
                            "  player %d invested %.0f %s: got %.6f, oracle "
                            "%.6f\n",
                            p, spot.invested[p],
                            spot.active[p] ? "live" : "folded", got[p],
                            want[p]);
            }
        }

        /* Coverage: how often a low existed and changed the payoff. */
        spot_t high_spot = spot;
        double high_only[MAX_P];
        high_spot.showdown = MPF_SHOWDOWN_HIGH;
        oracle_showdown(&high_spot, high_only);
        int differs = 0;
        for (int p = 0; p < spot.players; ++p)
            if (fabs(high_only[p] - want[p]) > 1e-9)
                differs = 1;
        splits += differs;
        for (int p = 0; p < spot.players; ++p)
        {
            int holes[6], key[5], n = 0;
            for (int c = 0; c < 52; ++c)
                if (mask_is_set(spot.hole[p], c))
                    holes[n++] = c;
            if (spot.active[p] && oracle_low(holes, n, spot.board, key))
            {
                lows++;
                break;
            }
        }
    }

    CHECK(mismatches == 0, "%s: %d of %d random deals disagree with the "
          "oracle", label, mismatches, deals);
    /* Without enough lows the comparison above would mostly be testing the
       high-only path. */
    CHECK(lows >= deals / 5 && splits >= deals / 10,
          "%s: only %d deals had a live low and %d changed the payoff",
          label, lows, splits);
    printf("    %-6s %d deals, %d with a live low, %d where the low moved "
           "money\n", label, deals, lows, splits);
}

/* ---------------------------------------------------------------- *
 * Build contract
 * ---------------------------------------------------------------- */

static void test_build_contract(const EvalContext *ctx)
{
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t st;
    int board[5];

    printf("  build contract\n");
    parse_cards("2c 4d 7h Kc Qs", board, NULL);

    /* Hi/Lo is accepted for every Omaha hand size. */
    const mpf_rule_t omaha[] = {MPF_RULE_PLO4, MPF_RULE_PLO5, MPF_RULE_PLO6};
    const char *holes[3][2] = {
        {"3c 5d Js 9s", "3h 5s Kd Kh"},
        {"3c 5d Js 9s 9h", "3h 5s Kd Kh Tc"},
        {"3c 5d Js 9s 9h Jd", "3h 5s Kd Kh Tc Td"}};
    for (int v = 0; v < 3; ++v)
    {
        base_config(&cfg, ctx, omaha[v], MPF_SHOWDOWN_HILO8, 2);
        cfg.board_card_count = 5;
        memcpy(cfg.board_cards, board, sizeof(board));
        for (int p = 0; p < 2; ++p)
        {
            cfg.hole[p] = parse_cards(holes[v][p], NULL, NULL);
            cfg.hole_specified[p] = 1;
        }
        int rc = mpf_build_game(&cfg, &game, &st);
        CHECK(rc == 0, "Hi/Lo refused for Omaha rule %d", (int)omaha[v]);
        if (rc == 0)
        {
            CHECK(st.showdown == MPF_SHOWDOWN_HILO8,
                  "the built state lost the showdown model");
            mpf_state_cleanup(&st);
        }
    }

    /* Refused outside Omaha. */
    const mpf_rule_t other[] = {MPF_RULE_HOLDEM, MPF_RULE_SHORTDECK};
    for (int v = 0; v < 2; ++v)
    {
        base_config(&cfg, ctx, other[v], MPF_SHOWDOWN_HILO8, 2);
        cfg.board_card_count = 5;
        memcpy(cfg.board_cards, board, sizeof(board));
        CHECK(mpf_build_game(&cfg, &game, &st) != 0,
              "Hi/Lo accepted for non-Omaha rule %d", (int)other[v]);
    }

    /* Refused with a high-strength abstraction. */
    base_config(&cfg, ctx, MPF_RULE_PLO4, MPF_SHOWDOWN_HILO8, 2);
    cfg.board_card_count = 5;
    memcpy(cfg.board_cards, board, sizeof(board));
    cfg.strength_buckets_per_street = 8;
    CHECK(mpf_build_game(&cfg, &game, &st) != 0,
          "Hi/Lo accepted with strength buckets");

    /* An unknown showdown value is refused, not read as high-only. */
    base_config(&cfg, ctx, MPF_RULE_PLO4, (mpf_showdown_t)7, 2);
    cfg.board_card_count = 5;
    memcpy(cfg.board_cards, board, sizeof(board));
    CHECK(mpf_build_game(&cfg, &game, &st) != 0,
          "an unknown showdown model was accepted");
}

/* ---------------------------------------------------------------- *
 * Solver smoke
 * ---------------------------------------------------------------- */

/* Heads-up river, 5 each in the pot, bets of 5 and 10 allowed. Player 0 has
   only the nut low, player 1 the high. High-only, player 0 can do no better
   than check and fold: -5. Hi/Lo, every showdown returns half the pot to
   him, so calling any bet breaks even: 0. */
static double solve_river(const EvalContext *ctx, mpf_rule_t rules,
                          mpf_showdown_t showdown, const char *hole0,
                          const char *hole1, int iterations)
{
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t st;
    cfr_config_t sconf;
    double exploit = 0.0;

    base_config(&cfg, ctx, rules, showdown, 2);
    cfg.stacks[0] = 55.0;
    cfg.stacks[1] = 55.0;
    cfg.sb = 0.0;
    cfg.bb = 0.0;
    cfg.ante = 5.0;
    cfg.raise_cap = 2;
    cfg.bet_size_count_common = 2;
    cfg.bet_sizes_common[0] = 5.0;
    cfg.bet_sizes_common[1] = 10.0;
    cfg.board_card_count = 5;
    parse_cards("2c 5d 8h Kc Qs", cfg.board_cards, NULL);
    cfg.hole[0] = parse_cards(hole0, NULL, NULL);
    cfg.hole[1] = parse_cards(hole1, NULL, NULL);
    cfg.hole_specified[0] = 1;
    cfg.hole_specified[1] = 1;

    if (mpf_build_game(&cfg, &game, &st) != 0)
        return NAN;
    cfr_storage_t *storage = cfr_storage_create();
    if (!storage)
    {
        mpf_state_cleanup(&st);
        return NAN;
    }
    memset(&sconf, 0, sizeof(sconf));
    sconf.max_iterations = iterations;
    /* Discounted CFR: the uniform average keeps the early iterations' calls
       for a long time, and the checks below want the equilibrium value. */
    sconf.enable_dcfr = 1;
    sconf.dcfr_alpha = 1.5;
    sconf.dcfr_beta = 0.0;
    sconf.dcfr_gamma = 2.0;
    cfr_solve(&game, storage, &sconf, &exploit);
    double v0 = cfr_compute_policy_value(&game, storage, 0, NULL);
    double v1 = cfr_compute_policy_value(&game, storage, 1, NULL);
    cfr_storage_destroy(storage);
    mpf_state_cleanup(&st);
    if (fabs(v0 + v1) > 1e-6)
    {
        fprintf(stderr, "solve: values %.6f and %.6f are not zero-sum\n", v0,
                v1);
        return NAN;
    }
    return v0;
}

static void test_solver_smoke(const EvalContext *ctx)
{
    static const struct
    {
        mpf_rule_t rules;
        const char *name;
        const char *hole0; /* A3 + 258: 8-5-3-2-A, a pair of jacks */
        const char *hole1; /* trip kings, no low */
    } spots[] = {
        {MPF_RULE_PLO4, "PLO4", "Ac 3d Js Jd", "Kd Kh 9s Ts"},
        {MPF_RULE_PLO5, "PLO5", "Ac 3d Js Jd 9c", "Kd Kh 9s Ts 3h"},
        {MPF_RULE_PLO6, "PLO6", "Ac 3d Js Jd 9c Tc", "Kd Kh 9s Ts 3h Jc"},
    };

    printf("  solver smoke, heads-up river\n");
    for (size_t i = 0; i < sizeof(spots) / sizeof(spots[0]); ++i)
    {
        double high = solve_river(ctx, spots[i].rules, MPF_SHOWDOWN_HIGH,
                                  spots[i].hole0, spots[i].hole1, 300);
        double hilo = solve_river(ctx, spots[i].rules, MPF_SHOWDOWN_HILO8,
                                  spots[i].hole0, spots[i].hole1, 300);
        CHECK(isfinite(high) && fabs(high + 5.0) < 0.05,
              "%s high-only: the nut low is worth %.4f, expected -5",
              spots[i].name, high);
        CHECK(isfinite(hilo) && fabs(hilo) < 0.05,
              "%s Hi/Lo: the nut low is worth %.4f, expected 0",
              spots[i].name, hilo);
        printf("    %s  nut low worth %.4f high-only, %.4f Hi/Lo\n",
               spots[i].name, high, hilo);
    }
}

int main(void)
{
    EvalConfig config = eval_config_omaha();
    EvalContext *ctx = eval_context_create(&config);
    if (!ctx)
    {
        fprintf(stderr, "EvalContext create failed\n");
        return 1;
    }

    printf("test_mpf_hilo8: Omaha Hi/Lo 8-or-better showdowns\n");
    test_hand_cases(ctx);
    printf("  random deals against the oracle\n");
    test_random_deals(ctx, MPF_RULE_PLO4, "PLO4", 3000, 0x0A4B1ull);
    test_random_deals(ctx, MPF_RULE_PLO5, "PLO5", 3000, 0x0A5B1ull);
    test_random_deals(ctx, MPF_RULE_PLO6, "PLO6", 3000, 0x0A6B1ull);
    test_build_contract(ctx);
    test_solver_smoke(ctx);

    eval_context_destroy(ctx);
    if (g_failures)
    {
        fprintf(stderr, "test_mpf_hilo8: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_mpf_hilo8: all Hi/Lo checks passed\n");
    return 0;
}
