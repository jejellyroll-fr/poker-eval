/*
 * @file test_mpf_infoset_privacy.c
 * @brief INF-01: a player's hole cards are private to everyone else.
 *
 * An infoset is what the acting player knows: their own cards, the board, the
 * pot, the opponent models. It must never carry information about hands the
 * acting player cannot see. mpf_infoset_key() hashes board_mask | hole[to_act]
 * and nothing else about any hand, so the property below is expected — the
 * test locks it, because it is the privacy guarantee that makes the storage a
 * map of infosets rather than a map of dealt states.
 *
 * The ticket asks for a mutation test rather than a re-reading of the hash:
 *
 *   - mutating a NON-active player's hand, over all substitutions that do not
 *     create a card conflict, leaves the infoset key strictly unchanged;
 *   - mutating the ACTIVE player's hand changes it.
 *
 * Cards are substituted one at a time and a card is only offered if it is not
 * already on the board or in any player's hand, so the mutated state is always
 * a legal deal. The scenarios cover Hold'em and PLO4, in 2 and 3 players —
 * the two hole sizes the adapter supports today, on the smallest and a
 * multiway table.
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/core/modern_cardmask.h>

#include <stdio.h>
#include <string.h>

/* One fixed scenario: a rule, a number of players, and hands that fit. */
typedef struct
{
    mpf_rule_t rules;
    int num_players;
    const char *name;
} privacy_scenario_t;

static mask_t make_hole(int a, int b)
{
    return mask_set(mask_set(MASK_EMPTY, a), b);
}

static mask_t make_hole4(int a, int b, int c, int d)
{
    return mask_set(mask_set(mask_set(mask_set(MASK_EMPTY, a), b), c), d);
}

/*
 * All single-card substitutions of hole[p] that do not create a card conflict:
 * the replacement is not on the board and not in any player's hand. The hand
 * keeps its card count (one hole card is removed, one is added).
 *
 * `cb` is called with a clone of `st` that carries the new hand; cb must not
 * free anything (clones are shallow copies, cleanup is the caller's).
 */
static int for_each_substitution(const mpf_state_t *st, int p,
                                 int (*cb)(const mpf_state_t *, void *),
                                 void *user)
{
    mask_t in_use = st->board_mask;
    for (int i = 0; i < st->num_players; ++i)
        in_use = mask_union(in_use, st->hole[i]);

    int probed = 0;
    for (int removed = 0; removed < 52; ++removed)
    {
        if (!mask_is_set(st->hole[p], removed))
            continue;
        for (int added = 0; added < 52; ++added)
        {
            if (mask_is_set(in_use, added))
                continue;
            if (added == removed)
                continue;
            mpf_state_t mut = *st; /* shallow: enough to re-hash */
            mut.hole[p] = mask_unset(mut.hole[p], removed);
            mut.hole[p] = mask_set(mut.hole[p], added);
            if (cb(&mut, user) != 0)
                return probed + 1;
            probed++;
        }
    }
    return probed;
}

typedef struct
{
    uint64_t baseline;
    int changed;
    int unchanged;
} privacy_probe_t;

/* Mutation of a non-active player's hand: the key must not move. */
static int probe_non_active(const mpf_state_t *mut, void *user)
{
    privacy_probe_t *probe = (privacy_probe_t *)user;
    if (mpf_state_infoset_key(mut) != probe->baseline)
        probe->changed++;
    else
        probe->unchanged++;
    return 0;
}

/* Mutation of the active player's hand: the key must move. */
static int probe_active(const mpf_state_t *mut, void *user)
{
    privacy_probe_t *probe = (privacy_probe_t *)user;
    if (mpf_state_infoset_key(mut) != probe->baseline)
        probe->changed++;
    else
        probe->unchanged++;
    return 0;
}

static void scenario_config(mpf_config_t *cfg, const EvalContext *ctx,
                            const privacy_scenario_t *sc)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->ctx = ctx;
    cfg->rules = sc->rules;
    cfg->num_players = sc->num_players;
    cfg->button_index = 0;
    /* Start on the flop with a pinned board: no chance node to walk through,
       and the root is a fully-formed decision state. */
    cfg->start_street = MPF_STREET_FLOP;
    cfg->board_card_count = 3;
    cfg->board_cards[0] = 0;  /* 2c */
    cfg->board_cards[1] = 20; /* 8h */
    cfg->board_cards[2] = 38; /* Qs */
    cfg->enable_chance_nodes = 0;
    cfg->raise_cap = 0;
    for (int i = 0; i < sc->num_players; ++i)
        cfg->stacks[i] = 100.0;
    cfg->preflop.defined = 1;
    cfg->preflop.has_invested = 1;
    for (int i = 0; i < sc->num_players; ++i)
        cfg->preflop.invested[i] = 3.0;
    cfg->preflop.has_pot = 1;
    cfg->preflop.pot = 6.0;
    cfg->preflop.has_to_call = 1;
    cfg->preflop.to_call = 0.0;
    cfg->preflop.has_current_bet = 1;
    cfg->preflop.current_bet = 0.0;
    cfg->preflop.has_raises = 1;
    cfg->preflop.raises_made = 0;
}
static void scenario_holes(mpf_config_t *cfg, const privacy_scenario_t *sc)
{
    int n = sc->num_players;
    if (sc->rules == MPF_RULE_PLO4)
    {
        cfg->hole[0] = make_hole4(1, 2, 28, 29);  /* 3c 4c Ah Ad */
        cfg->hole_specified[0] = 1;
        cfg->hole[1] = make_hole4(4, 18, 30, 44);  /* 6c 6h Ac Kd */
        cfg->hole_specified[1] = 1;
        if (n > 2)
        {
            cfg->hole[2] = make_hole4(9, 23, 36, 50); /* 10c 10h Qc Qd */
            cfg->hole_specified[2] = 1;
        }
    }
    else
    {
        cfg->hole[0] = make_hole(2, 3);  /* 3d 4d */
        cfg->hole_specified[0] = 1;
        cfg->hole[1] = make_hole(48, 49); /* Kh Kc */
        cfg->hole_specified[1] = 1;
        if (n > 2)
        {
            cfg->hole[2] = make_hole(9, 10); /* 10c Jc */
            cfg->hole_specified[2] = 1;
        }
    }
}

static int run_scenario(const EvalContext *ctx, const privacy_scenario_t *sc)
{
    mpf_config_t cfg;
    cfr_game_t game;
    mpf_state_t root;
    int failures = 0;

    scenario_config(&cfg, ctx, sc);
    scenario_holes(&cfg, sc);

    if (mpf_build_game(&cfg, &game, &root) != 0)
    {
        fprintf(stderr, "FAILED [%s]: build\n", sc->name);
        return 1;
    }

    int active = root.to_act;
    if (active < 0 || active >= root.num_players || !root.active[active])
    {
        fprintf(stderr, "FAILED [%s]: no valid acting player (to_act=%d)\n",
                sc->name, active);
        mpf_state_cleanup(&root);
        return 1;
    }

    privacy_probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe.baseline = mpf_state_infoset_key(&root);

    /* Every player except the acting one must be invisible to the key. */
    for (int p = 0; p < root.num_players; ++p)
    {
        if (p == active || !root.active[p])
            continue;
        memset(&probe, 0, sizeof(probe));
        probe.baseline = mpf_state_infoset_key(&root);
        int n = for_each_substitution(&root, p, probe_non_active, &probe);
        if (n == 0)
            fprintf(stderr, "WARN [%s]: no substitution probed for player %d\n",
                    sc->name, p);
        if (probe.changed != 0)
        {
            fprintf(stderr,
                    "FAILED [%s]: %d player-%d substitution(s) moved the key\n",
                    sc->name, probe.changed, p);
            failures++;
        }
    }

    /* The acting player's own hand must be part of the key: every legal
       substitution changes it (the canonical pattern encodes the card set). */
    memset(&probe, 0, sizeof(probe));
    probe.baseline = mpf_state_infoset_key(&root);
    int n = for_each_substitution(&root, active, probe_active, &probe);
    (void)n;
    if (probe.unchanged != 0)
    {
        fprintf(stderr,
                "FAILED [%s]: %d active-player substitution(s) left the key\n",
                sc->name, probe.unchanged);
        failures++;
    }

    if (!failures)
        printf("  %s: private hands inert, own hand in the key\n", sc->name);

    mpf_state_cleanup(&root);
    return failures;
}

int main(void)
{
    static const privacy_scenario_t scenarios[] = {
        { MPF_RULE_HOLDEM, 2, "holdem-2p" },
        { MPF_RULE_HOLDEM, 3, "holdem-3p" },
        { MPF_RULE_PLO4,   2, "plo4-2p"   },
        { MPF_RULE_PLO4,   3, "plo4-3p"   },
    };
    EvalConfig eval_cfg = eval_config_holdem();
    EvalContext *ctx = eval_context_create(&eval_cfg);
    int failures = 0;

    if (!ctx)
    {
        fprintf(stderr, "FAILED: eval context\n");
        return 1;
    }

    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
        failures += run_scenario(ctx, &scenarios[i]);

    eval_context_destroy(ctx);
    if (failures)
    {
        fprintf(stderr, "test_mpf_infoset_privacy: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_mpf_infoset_privacy: non-active holes stay off the key\n");
    return 0;
}

