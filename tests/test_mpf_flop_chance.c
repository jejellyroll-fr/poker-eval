/*
 * @file test_mpf_flop_chance.c
 * @brief CHN-02: the flop is one chance node over C(48,3) combinations.
 *
 * A flop is three cards dealt at once. The adapter used to reach the flop by
 * revealing three cards the configuration had already fixed, which is right
 * when the caller pinned a board and wrong when nobody did — the unset slots
 * are zeros, and zero is a real card index, so an unpinned preflop solve dealt
 * the same card three times and every player held a board card.
 *
 * The checks below are about what a chance node owes its traversal:
 *
 *  1. The node offers exactly C(48,3) = 17296 outcomes when four cards are
 *     spoken for, and it announces itself as PE_CHANCE_FLOP_THREE rather than
 *     as three board-card deals in a row.
 *  2. Every outcome yields a distinct set of three cards. This is checked with
 *     a bitset over the whole flop space rather than by counting distinct
 *     results: two outcomes could collide while a third produced a flop
 *     outside the deck, and a count of 17296 would hide both.
 *  3. No flop touches a card a player holds.
 *  4. A configuration that pinned its own flop keeps it. Chance deals what is
 *     unknown; it does not overrule the caller.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/engine/solvers/cfr/board_canonical.h>
#include <poker_eval/solver/pe_chance.h>
#include <poker_eval/solver/pe_combinations.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void base_config(mpf_config_t *cfg, const EvalContext *ctx)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->ctx = ctx;
    cfg->rules = MPF_RULE_HOLDEM;
    cfg->num_players = 2;
    cfg->button_index = 0;
    cfg->start_street = MPF_STREET_PREFLOP;
    cfg->stacks[0] = 100.0;
    cfg->stacks[1] = 100.0;
    cfg->sb = 0.5;
    cfg->bb = 1.0;
    cfg->raise_cap = 0;
    cfg->enable_chance_nodes = 1;
    /* Four known cards, so 48 remain and the answer is a number we can name:
       C(48,3) = 17296. */
    cfg->hole[0] = mask_set(mask_set(MASK_EMPTY, 51), 50);
    cfg->hole[1] = mask_set(mask_set(MASK_EMPTY, 49), 48);
    cfg->hole_specified[0] = 1;
    cfg->hole_specified[1] = 1;
}

/*
 * Check the preflop round down until a chance node appears. Folding would end
 * the hand before any board is dealt, so the walk calls rather than taking
 * whatever action happens to come first.
 */
static uint64_t walk_to_chance(cfr_game_t *game, uint64_t key)
{
    for (int step = 0; step < 32; ++step)
    {
        int actions[MPF_TREE_ACTION_MAX];
        int n;
        int chosen = -1;
        if (game->is_chance(game, key, NULL))
            return key;
        if (game->is_terminal(game, key, NULL))
            return 0;
        n = game->get_actions(game, key, actions, MPF_TREE_ACTION_MAX, NULL);
        for (int i = 0; i < n; ++i)
        {
            if (actions[i] == MPF_ACTION_CALL)
                chosen = actions[i];
        }
        if (chosen < 0)
            return 0;
        key = game->apply_action(game, key, chosen, NULL);
        if (key == 0)
            return 0;
    }
    return 0;
}

static int test_flop_is_a_combination(const EvalContext *ctx)
{
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t root;
    uint64_t chance_key;
    const mpf_state_t *node;
    int outcomes;
    int failures = 0;
    unsigned char *seen;
    int distinct = 0;
    int duplicates = 0;
    int collisions_with_holes = 0;
    int wrong_width = 0;
    int not_ascending = 0;
    mask_t holes;

    base_config(&cfg, ctx);
    if (mpf_build_game(&cfg, &game, &root) != 0)
    {
        fprintf(stderr, "FAILED: preflop build\n");
        return 1;
    }
    holes = cfg.hole[0] | cfg.hole[1];

    /* The root itself is a decision: nobody deals before the blinds act. */
    if (mpf_state_chance_kind(&root) != PE_CHANCE_NONE)
    {
        fprintf(stderr, "FAILED: the preflop root reports %s\n",
                pe_chance_kind_name(mpf_state_chance_kind(&root)));
        failures++;
    }

    chance_key = walk_to_chance(&game, (uint64_t)(uintptr_t)game.initial_state);
    if (chance_key == 0)
    {
        fprintf(stderr, "FAILED: the preflop round never reached a chance node\n");
        mpf_state_cleanup(&root);
        return failures + 1;
    }

    node = mpf_state_for_key(&game, chance_key);
    if (!node || mpf_state_chance_kind(node) != PE_CHANCE_FLOP_THREE)
    {
        fprintf(stderr, "FAILED: the flop node reports %s, expected flop-three\n",
                pe_chance_kind_name(node ? mpf_state_chance_kind(node) : PE_CHANCE_NONE));
        mpf_state_cleanup(&root);
        return failures + 1;
    }

    outcomes = game.get_chance_outcomes(&game, chance_key, NULL);
    if (outcomes != 17296)
    {
        fprintf(stderr, "FAILED: the flop node offers %d outcomes, expected C(48,3) = 17296\n",
                outcomes);
        failures++;
    }
    if (outcomes <= 0)
    {
        mpf_state_cleanup(&root);
        return failures + 1;
    }

    /* One bit per unordered triple of card indices. Indexing by the cards
       themselves rather than by the outcome number is the point: it catches an
       enumeration that emits the same flop under two indices, and one that
       emits a permutation of a flop already dealt. */
    seen = (unsigned char *)calloc(52u * 52u * 52u, 1);
    if (!seen)
    {
        mpf_state_cleanup(&root);
        return failures + 1;
    }

    for (int i = 0; i < outcomes; ++i)
    {
        uint64_t child_key = game.apply_chance(&game, chance_key, i, NULL);
        const mpf_state_t *child = child_key ? mpf_state_for_key(&game, child_key) : NULL;
        int a, b, c;
        size_t slot;
        if (!child)
        {
            fprintf(stderr, "FAILED: outcome %d produced no state\n", i);
            failures++;
            break;
        }
        if (child->board_revealed != 3 || child->street != MPF_STREET_FLOP ||
            mpf_state_chance_kind(child) != PE_CHANCE_NONE)
        {
            wrong_width++;
            continue;
        }
        a = child->board_cards[0];
        b = child->board_cards[1];
        c = child->board_cards[2];
        if (!(a < b && b < c))
        {
            not_ascending++;
            continue;
        }
        if (mask_is_set(holes, a) || mask_is_set(holes, b) || mask_is_set(holes, c))
            collisions_with_holes++;
        slot = ((size_t)a * 52u + (size_t)b) * 52u + (size_t)c;
        if (seen[slot])
            duplicates++;
        else
        {
            seen[slot] = 1;
            distinct++;
        }
    }
    free(seen);

    if (wrong_width)
    {
        fprintf(stderr, "FAILED: %d children did not land on a three-card flop\n", wrong_width);
        failures++;
    }
    if (not_ascending)
    {
        fprintf(stderr, "FAILED: %d flops came back unordered\n", not_ascending);
        failures++;
    }
    if (duplicates)
    {
        fprintf(stderr, "FAILED: %d flops were dealt more than once\n", duplicates);
        failures++;
    }
    if (collisions_with_holes)
    {
        fprintf(stderr, "FAILED: %d flops used a card a player holds\n", collisions_with_holes);
        failures++;
    }
    if (distinct != outcomes)
    {
        fprintf(stderr, "FAILED: %d distinct flops for %d outcomes\n", distinct, outcomes);
        failures++;
    }

    printf("  flop node: %d outcomes, %d distinct boards, none blocked\n", outcomes, distinct);
    mpf_state_cleanup(&root);
    return failures;
}

/*
 * The other half of the rule. A caller who names the flop has named it; the
 * transition must reveal that board and not enumerate a deck that still
 * contains it.
 */
static int test_a_pinned_flop_is_not_dealt(const EvalContext *ctx)
{
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t root;
    uint64_t key;
    const mpf_state_t *node;
    int failures = 0;

    base_config(&cfg, ctx);
    cfg.board_cards[0] = 2;
    cfg.board_cards[1] = 9;
    cfg.board_cards[2] = 17;
    cfg.board_card_count = 3;

    if (mpf_build_game(&cfg, &game, &root) != 0)
    {
        fprintf(stderr, "FAILED: pinned-flop build\n");
        return 1;
    }

    /* Checking down from preflop reaches the turn deal, not a flop deal: the
       flop was already on the table when the round began. Asserting that the
       node is not FLOP_THREE would pass just as well if the walk fell off the
       tree, so what is checked is the node it did reach and the board it
       carries. */
    key = walk_to_chance(&game, (uint64_t)(uintptr_t)game.initial_state);
    node = key ? mpf_state_for_key(&game, key) : NULL;
    if (!node)
    {
        fprintf(stderr, "FAILED: a pinned-flop game reached no chance node\n");
        mpf_state_cleanup(&root);
        return 1;
    }
    if (mpf_state_chance_kind(node) != PE_CHANCE_BOARD_ONE)
    {
        fprintf(stderr, "FAILED: the pinned-flop game deals %s, expected one board card\n",
                pe_chance_kind_name(mpf_state_chance_kind(node)));
        failures++;
    }
    if (node->board_revealed != 3 || node->board_cards[0] != 2 ||
        node->board_cards[1] != 9 || node->board_cards[2] != 17)
    {
        fprintf(stderr, "FAILED: the configured flop became %d cards [%d %d %d]\n",
                node->board_revealed, node->board_cards[0], node->board_cards[1],
                node->board_cards[2]);
        failures++;
    }
    if (!failures)
        printf("  a pinned flop stays pinned, and the turn is what chance deals\n");

    mpf_state_cleanup(&root);
    return failures;
}

/*
 * CHN-03: the sampler must reproduce the enumerated distribution.
 *
 * The flop chance node enumerates C(48,3) = 17296 combinations over the 48
 * cards still in the deck. Direct sampling draws one of those combinations
 * uniformly, so over many draws the empirical frequency of every suit-
 * isomorphism class (flops grouped by pe_board_canonical_key) must converge to
 * the theoretical frequency — class cardinal / C(48,3) — that the enumeration
 * table implies. The check uses both a per-class absolute bound and Pearson's
 * chi-square statistic; an L1 histogram distance is not suitable here because
 * summing the independent sampling noise over 1,755 classes naturally makes
 * it several percent even when the sampler is correct.
 *
 * The theoretical distribution is not hard-coded: it is rebuilt by brute
 * enumeration over the same 48-card deck, using the same canonical key. The
 * sampler and the reference cannot agree by sharing a bug, because neither
 * reads the other's numbers.
 */

/* Small open-addressed table from canonical key to class index. Even the full
   52-card flop space has only 1755 classes, so 16384 slots keep the load low
   and the probe bounded. */
#define ISO_SLOTS 16384u
#define ISO_KEY_MAX 24

typedef struct
{
    char key[ISO_KEY_MAX];
    int used;
    long cardinal;  /* flops in the class, from the enumeration table */
    long empirical; /* draws observed in the class, from the sampler */
} iso_slot_t;

static iso_slot_t g_iso[ISO_SLOTS];

static uint32_t iso_hash(const char *key)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p)
        h = (uint32_t)((h ^ *p) * 16777619u);
    return h & (ISO_SLOTS - 1u);
}

/* Find the slot for `key`, inserting it when `create` is nonzero. Returns -1
   when the table is full (it will not be), or when `create` is 0 and the key
   is absent. */
static long iso_slot(const char *key, int create)
{
    uint32_t base = iso_hash(key);
    for (uint32_t probe = 0; probe < ISO_SLOTS; ++probe)
    {
        uint32_t idx = (base + probe) & (ISO_SLOTS - 1u);
        if (!g_iso[idx].used)
        {
            if (!create)
                return -1;
            g_iso[idx].used = 1;
            strncpy(g_iso[idx].key, key, ISO_KEY_MAX - 1);
            g_iso[idx].key[ISO_KEY_MAX - 1] = '\0';
            g_iso[idx].cardinal = 0;
            g_iso[idx].empirical = 0;
            return (long)idx;
        }
        if (strcmp(g_iso[idx].key, key) == 0)
            return (long)idx;
    }
    return -1;
}

/* The three cards of a flop, as a board mask. `pos` holds deck indices. */
static mask_t flop_mask(const unsigned pos[3])
{
    return mask_set(mask_set(mask_set(MASK_EMPTY, pos[0]), pos[1]), pos[2]);
}

static int test_flop_sampling_tracks_the_distribution(const EvalContext *ctx)
{
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t root;
    uint64_t chance_key;
    const mpf_state_t *node;
    int failures = 0;
    const int unused = 48;     /* the four Aces sit in the two hands */
    const long draws = 1000000L;
    unsigned pos[3];

    base_config(&cfg, ctx);
    if (mpf_build_game(&cfg, &game, &root) != 0)
    {
        fprintf(stderr, "FAILED: sampling build\n");
        return 1;
    }
    chance_key = walk_to_chance(&game, (uint64_t)(uintptr_t)game.initial_state);
    node = chance_key ? mpf_state_for_key(&game, chance_key) : NULL;
    if (!node || mpf_state_chance_kind(node) != PE_CHANCE_FLOP_THREE)
    {
        fprintf(stderr, "FAILED: sampling reached no flop chance node\n");
        mpf_state_cleanup(&root);
        return 1;
    }

    /* Theoretical distribution: every 3-subset of the 48-card deck. */
    memset(g_iso, 0, sizeof(g_iso));
    long total = 0;
    for (pos[0] = 0; pos[0] < (unsigned)unused; ++pos[0])
        for (pos[1] = pos[0] + 1; pos[1] < (unsigned)unused; ++pos[1])
            for (pos[2] = pos[1] + 1; pos[2] < (unsigned)unused; ++pos[2])
            {
                char key[ISO_KEY_MAX];
                long slot;
                if (pe_board_canonical_key(flop_mask(pos), 3, key, sizeof(key)) != 0)
                    continue;
                slot = iso_slot(key, 1);
                if (slot < 0)
                    continue;
                g_iso[slot].cardinal++;
                total++;
            }
    if (total != 17296)
    {
        fprintf(stderr, "FAILED: theoretical enumeration found %ld flops\n",
                total);
        mpf_state_cleanup(&root);
        return 1;
    }
/* Sample one million flop draws and count how many land in each class. */
    pe_rng_t rng;
    pe_rng_seed(&rng, 0xC0FFEEu);
    uint64_t ratio_violations = 0;
    long sampled = 0;
    for (long d = 0; d < draws; ++d)
    {
        pe_chance_sample_t sm;
        unsigned p[3];
        char key[ISO_KEY_MAX];
        long slot;
        if (mpf_chance_sample(node, &rng, &sm) != 0)
            break;
        if (sm.importance_ratio != 1.0)
            ratio_violations++;
        if (pe_comb_unrank((unsigned)unused, 3, (uint64_t)sm.outcome, p) != 0)
            break;
        if (pe_board_canonical_key(flop_mask(p), 3, key, sizeof(key)) != 0)
            break;
        slot = iso_slot(key, 0);
        if (slot < 0)
            break;
        g_iso[slot].empirical++;
        sampled++;
    }
    if (sampled != draws)
    {
        fprintf(stderr, "FAILED: only %ld of %ld draws produced a class\n",
                sampled, draws);
        mpf_state_cleanup(&root);
        return 1;
    }

    /* Every class must sit within 1% of the theoretical table. Pearson's
       statistic is a second aggregate check; the generous fixed threshold is
       many standard deviations above its expected value (about 1,754). */
    double max_abs = 0.0;
    double max_rel_big = 0.0;
    double chi2 = 0.0;
    long sampled_classes = 0;
    for (uint32_t i = 0; i < ISO_SLOTS; ++i)
    {
        double theo;
        double emp;
        double absdev;
        double expected;
        double diff;
        if (!g_iso[i].used)
            continue;
        theo = (double)g_iso[i].cardinal / (double)total;
        emp = (double)g_iso[i].empirical / (double)draws;
        absdev = fabs(emp - theo);
        expected = (double)draws * theo;
        diff = (double)g_iso[i].empirical - expected;
        if (expected > 0.0)
            chi2 += (diff * diff) / expected;
        if (absdev > max_abs)
            max_abs = absdev;
        /* Relative deviation is only meaningful where a class is big enough
           for finite-draw noise to be small; reported, not asserted. */
        if (g_iso[i].cardinal >= 500)
        {
            double rel = theo > 0.0 ? absdev / theo : 0.0;
            if (rel > max_rel_big)
                max_rel_big = rel;
        }
        if (g_iso[i].empirical > 0)
            sampled_classes++;
    }

    printf("  sampled %ld flops over %ld classes: max|dev|=%.4g"
           " chi2=%.4g maxrel(big)=%.4g\n",
           sampled, sampled_classes, max_abs, chi2, max_rel_big);

    if (max_abs > 0.01)
    {
        fprintf(stderr, "FAILED: a class deviates by %.4g (> 1%%)\n", max_abs);
        failures++;
    }
    if (chi2 > 2400.0)
    {
        fprintf(stderr, "FAILED: chi-square %.4g is too large\n", chi2);
        failures++;
    }
    if (ratio_violations)
    {
        fprintf(stderr, "FAILED: %llu draws carried an importance ratio != 1.0\n",
                (unsigned long long)ratio_violations);
        failures++;
    }

    mpf_state_cleanup(&root);
    return failures;
}

int main(void)
{
    EvalConfig eval_cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&eval_cfg);
    int failures = 0;

    if (!ctx)
    {
        fprintf(stderr, "FAILED: eval context\n");
        return 1;
    }

    failures += test_flop_is_a_combination(ctx);
    failures += test_a_pinned_flop_is_not_dealt(ctx);
    failures += test_flop_sampling_tracks_the_distribution(ctx);

    eval_context_destroy(ctx);
    if (failures)
    {
        fprintf(stderr, "test_mpf_flop_chance: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_mpf_flop_chance: the flop is dealt as one combination\n");
    return 0;
}
