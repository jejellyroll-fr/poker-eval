/*
 * test_plo4_certification.c - independent oracle for issue #283 tranche 1.
 *
 * The fixture is a two-combo ranges game on a river board.  The oracle below
 * enumerates every legal ordered pair, derives hand strengths with the product
 * Omaha evaluator, and computes the policy value and both best responses by
 * hand instead of reusing the certification module or the showdown kernel.
 * It therefore checks the whole contract: weighted ranges, blockers, the
 * check/bet/fold tree, net-of-investment payoffs, the NashConv definition and
 * the exact/not_certified labels.
 */

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/eval_omaha.h>
#include <poker_eval/solver/pe_plo4_certification.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, message)                                      \
    do                                                                 \
    {                                                                  \
        if (!(condition))                                              \
        {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                    message);                                          \
            failures++;                                                \
        }                                                              \
    } while (0)

#define FIXTURE_MAX_COMBOS 4u

typedef struct
{
    mask_t board;
    pe_omaha_combo_t combos[2][FIXTURE_MAX_COMBOS];
    size_t count[2];
    HandVal strength[2][FIXTURE_MAX_COMBOS];
    double pot;
    double invested[2];
    double bet_size;
    double big_blind;
} fixture_t;

typedef enum
{
    TERMINAL_SHOWDOWN_10 = 0,
    TERMINAL_SHOWDOWN_20,
    TERMINAL_FOLD_P0,
    TERMINAL_FOLD_P1
} terminal_t;

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static StdDeck_CardMask to_std_mask(mask_t cards)
{
    StdDeck_CardMask result;
    int card_index;

    StdDeck_CardMask_RESET(result);
    for (card_index = 0; card_index < MODERN_DECK_SIZE; ++card_index)
        if (mask_is_set(cards, card_index))
            StdDeck_CardMask_SET(result, card_index);
    return result;
}

/* The strategy the module is handed, mirrored here as a pure function so the
 * oracle can read it without calling the module. */
static double strategy_prob(uint16_t infoset, size_t combo, uint16_t action)
{
    double second;
    switch (infoset)
    {
    case PE_PLO4_CERT_INFOSET_OPEN_P0: second = combo == 0u ? 0.0 : 1.0; break;
    case PE_PLO4_CERT_INFOSET_FACING_P0: second = combo == 0u ? 0.5 : 0.2; break;
    case PE_PLO4_CERT_INFOSET_OPEN_P1: second = combo == 0u ? 0.4 : 0.9; break;
    case PE_PLO4_CERT_INFOSET_FACING_P1:
    default: second = 0.5; break;
    }
    return action == 1u ? second : 1.0 - second;
}

static int fixture_strategy(void *user, uint16_t infoset, uint16_t action,
                            pe_value_vec_t *out)
{
    size_t combo;

    (void)user;
    if (!out || action >= PE_PLO4_CERT_ACTION_COUNT)
        return -1;
    for (combo = 0u; combo < out->n; ++combo)
        out->v[combo] = strategy_prob(infoset, combo, action);
    return 0;
}

static int all_fold_strategy(void *user, uint16_t infoset, uint16_t action,
                             pe_value_vec_t *out)
{
    size_t combo;

    (void)user;
    (void)infoset;
    if (!out || action >= PE_PLO4_CERT_ACTION_COUNT)
        return -1;
    for (combo = 0u; combo < out->n; ++combo)
        out->v[combo] = 1.0;
    return 0;
}

static int setup_fixture(fixture_t *fixture, int weighted)
{
    size_t player;
    size_t combo;

    memset(fixture, 0, sizeof(*fixture));
    fixture->board = card(MODERN_RANK_2, MODERN_SUIT_CLUBS) |
                     card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS) |
                     card(MODERN_RANK_T, MODERN_SUIT_HEARTS) |
                     card(MODERN_RANK_J, MODERN_SUIT_SPADES) |
                     card(MODERN_RANK_Q, MODERN_SUIT_CLUBS);
    /* P0: Broadway, then a pair of nines. */
    fixture->combos[0][0].cards =
        card(MODERN_RANK_A, MODERN_SUIT_SPADES) |
        card(MODERN_RANK_K, MODERN_SUIT_SPADES) |
        card(MODERN_RANK_Q, MODERN_SUIT_HEARTS) |
        card(MODERN_RANK_J, MODERN_SUIT_HEARTS);
    fixture->combos[0][1].cards =
        card(MODERN_RANK_9, MODERN_SUIT_SPADES) |
        card(MODERN_RANK_9, MODERN_SUIT_HEARTS) |
        card(MODERN_RANK_8, MODERN_SUIT_SPADES) |
        card(MODERN_RANK_8, MODERN_SUIT_HEARTS);
    /* P1: Broadway, then a pair of sevens. */
    fixture->combos[1][0].cards =
        card(MODERN_RANK_A, MODERN_SUIT_DIAMONDS) |
        card(MODERN_RANK_K, MODERN_SUIT_DIAMONDS) |
        card(MODERN_RANK_Q, MODERN_SUIT_DIAMONDS) |
        card(MODERN_RANK_J, MODERN_SUIT_DIAMONDS);
    fixture->combos[1][1].cards =
        card(MODERN_RANK_7, MODERN_SUIT_SPADES) |
        card(MODERN_RANK_7, MODERN_SUIT_HEARTS) |
        card(MODERN_RANK_6, MODERN_SUIT_SPADES) |
        card(MODERN_RANK_6, MODERN_SUIT_HEARTS);
    fixture->count[0] = 2u;
    fixture->count[1] = 2u;
    fixture->combos[0][0].weight = weighted ? 1.0 : 1.0;
    fixture->combos[0][1].weight = weighted ? 2.0 : 1.0;
    fixture->combos[1][0].weight = weighted ? 0.5 : 1.0;
    fixture->combos[1][1].weight = weighted ? 1.5 : 1.0;
    fixture->pot = 10.0;
    fixture->invested[0] = 5.0;
    fixture->invested[1] = 5.0;
    fixture->bet_size = 5.0;
    fixture->big_blind = 2.0;
    for (player = 0u; player < 2u; ++player)
        for (combo = 0u; combo < fixture->count[player]; ++combo)
        {
            HandVal value = HandVal_NOTHING;

            if (StdDeck_OmahaHi_EVAL(
                    to_std_mask(fixture->combos[player][combo].cards),
                    to_std_mask(fixture->board), &value) != 0)
                return -1;
            fixture->strength[player][combo] = value;
        }
    return 0;
}

static void build_spec(const fixture_t *fixture, pe_plo4_cert_spec_t *spec,
                       pe_omaha_range_t ranges[2],
                       pe_plo4_cert_strategy_fn strategy)
{
    memset(spec, 0, sizeof(*spec));
    ranges[0].combos = fixture->combos[0];
    ranges[0].count = fixture->count[0];
    ranges[1].combos = fixture->combos[1];
    ranges[1].count = fixture->count[1];
    spec->board = fixture->board;
    spec->ranges = ranges;
    spec->pot = fixture->pot;
    spec->invested[0] = fixture->invested[0];
    spec->invested[1] = fixture->invested[1];
    spec->stacks[0] = 5.0;
    spec->stacks[1] = 5.0;
    spec->bet_size = fixture->bet_size;
    spec->big_blind = fixture->big_blind;
    spec->strategy = strategy;
    spec->strategy_user = NULL;
}

static int pair_legal(const fixture_t *fixture, size_t h0, size_t h1)
{
    mask_t left = fixture->combos[0][h0].cards;
    mask_t right = fixture->combos[1][h1].cards;

    if ((left & fixture->board) != 0u || (right & fixture->board) != 0u)
        return 0;
    return (left & right) == 0u;
}

static double joint_mass(const fixture_t *fixture)
{
    double mass = 0.0;
    size_t h0;
    size_t h1;

    for (h0 = 0u; h0 < fixture->count[0]; ++h0)
        for (h1 = 0u; h1 < fixture->count[1]; ++h1)
            if (pair_legal(fixture, h0, h1))
                mass += fixture->combos[0][h0].weight *
                        fixture->combos[1][h1].weight;
    return mass;
}

/* P0's utility at one terminal, in chips, net of its investment. */
static double terminal_u0(const fixture_t *fixture, terminal_t terminal,
                          size_t h0, size_t h1)
{
    double pot;
    double invested;
    double share;
    HandVal left = fixture->strength[0][h0];
    HandVal right = fixture->strength[1][h1];

    switch (terminal)
    {
    case TERMINAL_FOLD_P0:
        return -fixture->invested[0];
    case TERMINAL_FOLD_P1:
        return fixture->pot - fixture->invested[0];
    case TERMINAL_SHOWDOWN_10:
        pot = fixture->pot;
        invested = fixture->invested[0];
        break;
    case TERMINAL_SHOWDOWN_20:
    default:
        pot = fixture->pot + 2.0 * fixture->bet_size;
        invested = fixture->invested[0] + fixture->bet_size;
        break;
    }
    share = left > right ? pot : (left == right ? pot * 0.5 : 0.0);
    return share - invested;
}

/* P0 utility under the policy for one legal pair. */
static double policy_u0(const fixture_t *fixture, size_t h0, size_t h1)
{
    double value = 0.0;
    double p_check = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P0, h0, 0u);
    double p_bet = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P0, h0, 1u);
    double p1_check = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P1, h1, 0u);
    double p1_bet = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P1, h1, 1u);
    double p0_fold = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P0, h0, 0u);
    double p0_call = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P0, h0, 1u);
    double p1_fold = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P1, h1, 0u);
    double p1_call = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P1, h1, 1u);

    if (p_check > 0.0)
    {
        value += p_check * p1_check *
                 terminal_u0(fixture, TERMINAL_SHOWDOWN_10, h0, h1);
        value += p_check * p1_bet *
                 (p0_fold * terminal_u0(fixture, TERMINAL_FOLD_P0, h0, h1) +
                  p0_call * terminal_u0(fixture, TERMINAL_SHOWDOWN_20, h0, h1));
    }
    if (p_bet > 0.0)
        value += p_bet *
                 (p1_fold * terminal_u0(fixture, TERMINAL_FOLD_P1, h0, h1) +
                  p1_call * terminal_u0(fixture, TERMINAL_SHOWDOWN_20, h0, h1));
    return value;
}

/* P1 utility under the policy, measured independently from P0's. */
static double policy_u1(const fixture_t *fixture, size_t h0, size_t h1)
{
    double value = 0.0;
    double p_check = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P0, h0, 0u);
    double p_bet = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P0, h0, 1u);
    double p1_check = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P1, h1, 0u);
    double p1_bet = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P1, h1, 1u);
    double p0_fold = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P0, h0, 0u);
    double p0_call = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P0, h0, 1u);
    double p1_fold = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P1, h1, 0u);
    double p1_call = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P1, h1, 1u);

    if (p_check > 0.0)
    {
        value += p_check * p1_check *
                 (-terminal_u0(fixture, TERMINAL_SHOWDOWN_10, h0, h1));
        value += p_check * p1_bet *
                 (p0_fold * (-terminal_u0(fixture, TERMINAL_FOLD_P0, h0, h1)) +
                  p0_call *
                      (-terminal_u0(fixture, TERMINAL_SHOWDOWN_20, h0, h1)));
    }
    if (p_bet > 0.0)
        value += p_bet *
                 (p1_fold * (-terminal_u0(fixture, TERMINAL_FOLD_P1, h0, h1)) +
                  p1_call *
                      (-terminal_u0(fixture, TERMINAL_SHOWDOWN_20, h0, h1)));
    return value;
}

/* P0 best response: per-hand choice at its two information sets. */
static double best_response_u0(const fixture_t *fixture, size_t h0,
                               int open_action, int facing_action)
{
    double value = 0.0;
    size_t h1;

    for (h1 = 0u; h1 < fixture->count[1]; ++h1)
    {
        double weight;
        if (!pair_legal(fixture, h0, h1))
            continue;
        weight = fixture->combos[1][h1].weight;
        if (open_action == 0)
        {
            double p1_check = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P1, h1, 0u);
            double p1_bet = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P1, h1, 1u);
            double after_bet =
                facing_action == 0
                    ? terminal_u0(fixture, TERMINAL_FOLD_P0, h0, h1)
                    : terminal_u0(fixture, TERMINAL_SHOWDOWN_20, h0, h1);
            value += weight * (p1_check *
                                   terminal_u0(fixture, TERMINAL_SHOWDOWN_10, h0,
                                               h1) +
                               p1_bet * after_bet);
        }
        else
        {
            double p1_fold = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P1, h1, 0u);
            double p1_call = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P1, h1, 1u);
            value += weight * (p1_fold *
                                   terminal_u0(fixture, TERMINAL_FOLD_P1, h0, h1) +
                               p1_call *
                                   terminal_u0(fixture, TERMINAL_SHOWDOWN_20, h0,
                                               h1));
        }
    }
    return value;
}

/* P1 best response: per-hand choice at its two information sets. */
static double best_response_u1(const fixture_t *fixture, size_t h1,
                               int open_action, int facing_action)
{
    double value = 0.0;
    size_t h0;

    for (h0 = 0u; h0 < fixture->count[0]; ++h0)
    {
        double weight;
        if (!pair_legal(fixture, h0, h1))
            continue;
        weight = fixture->combos[0][h0].weight;
        {
            double p0_check = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P0, h0, 0u);
            double p0_bet = strategy_prob(PE_PLO4_CERT_INFOSET_OPEN_P0, h0, 1u);
            double p0_fold = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P0, h0, 0u);
            double p0_call = strategy_prob(PE_PLO4_CERT_INFOSET_FACING_P0, h0, 1u);
            /* P1's first decision follows a check; its second follows P0's
             * bet.  The two branches are disjoint in the tree. */
            double after_check =
                open_action == 0
                    ? -terminal_u0(fixture, TERMINAL_SHOWDOWN_10, h0, h1)
                    : p0_fold *
                              (-terminal_u0(fixture, TERMINAL_FOLD_P0, h0, h1)) +
                          p0_call * (-terminal_u0(fixture, TERMINAL_SHOWDOWN_20,
                                                  h0, h1));
            double after_bet =
                facing_action == 0
                    ? -terminal_u0(fixture, TERMINAL_FOLD_P1, h0, h1)
                    : -terminal_u0(fixture, TERMINAL_SHOWDOWN_20, h0, h1);
            value += weight * (p0_check * after_check + p0_bet * after_bet);
        }
    }
    return value;
}

typedef struct
{
    double policy[2];
    double br[2];
    double gap[2];
    double nashconv;
    double exploitability;
    double joint_mass;
} oracle_t;

static oracle_t oracle_measure(const fixture_t *fixture)
{
    oracle_t oracle;
    size_t h0;
    size_t h1;

    memset(&oracle, 0, sizeof(oracle));
    oracle.joint_mass = joint_mass(fixture);
    if (!(oracle.joint_mass > 0.0))
        return oracle;
    for (h0 = 0u; h0 < fixture->count[0]; ++h0)
        for (h1 = 0u; h1 < fixture->count[1]; ++h1)
        {
            double weight;

            if (!pair_legal(fixture, h0, h1))
                continue;
            weight = fixture->combos[0][h0].weight *
                     fixture->combos[1][h1].weight;
            oracle.policy[0] += weight * policy_u0(fixture, h0, h1);
            oracle.policy[1] += weight * policy_u1(fixture, h0, h1);
        }
    oracle.policy[0] /= oracle.joint_mass;
    oracle.policy[1] /= oracle.joint_mass;

    for (h0 = 0u; h0 < fixture->count[0]; ++h0)
    {
        double best = -INFINITY;
        int open_action;
        int facing_action;

        if (!(fixture->combos[0][h0].weight > 0.0))
            continue;
        for (open_action = 0; open_action < 2; ++open_action)
            for (facing_action = 0; facing_action < 2; ++facing_action)
            {
                double candidate =
                    best_response_u0(fixture, h0, open_action, facing_action);
                if (candidate > best)
                    best = candidate;
            }
        oracle.br[0] += fixture->combos[0][h0].weight * best;
    }
    for (h1 = 0u; h1 < fixture->count[1]; ++h1)
    {
        double best = -INFINITY;
        int open_action;
        int facing_action;

        if (!(fixture->combos[1][h1].weight > 0.0))
            continue;
        for (open_action = 0; open_action < 2; ++open_action)
            for (facing_action = 0; facing_action < 2; ++facing_action)
            {
                double candidate =
                    best_response_u1(fixture, h1, open_action, facing_action);
                if (candidate > best)
                    best = candidate;
            }
        oracle.br[1] += fixture->combos[1][h1].weight * best;
    }
    oracle.br[0] /= oracle.joint_mass;
    oracle.br[1] /= oracle.joint_mass;
    oracle.gap[0] = oracle.br[0] - oracle.policy[0];
    oracle.gap[1] = oracle.br[1] - oracle.policy[1];
    oracle.nashconv = oracle.gap[0] + oracle.gap[1];
    oracle.exploitability = oracle.nashconv * 0.5;
    return oracle;
}

static void check_against_oracle(const char *label, const fixture_t *fixture,
                                 const pe_plo4_cert_result_t *result)
{
    oracle_t oracle = oracle_measure(fixture);
    char message[256];

    snprintf(message, sizeof(message), "%s: policy value P0", label);
    CHECK(fabs(result->policy_value[0] - oracle.policy[0]) <= 1e-9, message);
    snprintf(message, sizeof(message), "%s: policy value P1", label);
    CHECK(fabs(result->policy_value[1] - oracle.policy[1]) <= 1e-9, message);
    snprintf(message, sizeof(message), "%s: BR value P0", label);
    CHECK(fabs(result->br_value[0] - oracle.br[0]) <= 1e-9, message);
    snprintf(message, sizeof(message), "%s: BR value P1", label);
    CHECK(fabs(result->br_value[1] - oracle.br[1]) <= 1e-9, message);
    snprintf(message, sizeof(message), "%s: gap P0", label);
    CHECK(fabs(result->br_gap[0] - oracle.gap[0]) <= 1e-9, message);
    snprintf(message, sizeof(message), "%s: gap P1", label);
    CHECK(fabs(result->br_gap[1] - oracle.gap[1]) <= 1e-9, message);
    snprintf(message, sizeof(message), "%s: NashConv", label);
    CHECK(fabs(result->nashconv - oracle.nashconv) <= 1e-9, message);
    snprintf(message, sizeof(message), "%s: exploitability", label);
    CHECK(fabs(result->exploitability - oracle.exploitability) <= 1e-9,
          message);
    snprintf(message, sizeof(message), "%s: joint mass", label);
    CHECK(fabs(result->joint_mass - oracle.joint_mass) <= 1e-12, message);
    snprintf(message, sizeof(message), "%s: zero-sum policy values", label);
    CHECK(fabs(result->policy_value[0] + result->policy_value[1]) <= 1e-9,
          message);
        /* Best responses are separate maxima against the same policy, so their
     * values are not opposite; each one must at least match the policy. */
    snprintf(message, sizeof(message), "%s: BR P0 dominates the policy", label);
    CHECK(result->br_value[0] >= result->policy_value[0] - 1e-9, message);
    snprintf(message, sizeof(message), "%s: BR P1 dominates the policy", label);
    CHECK(result->br_value[1] >= result->policy_value[1] - 1e-9, message);
    snprintf(message, sizeof(message), "%s: NashConv is non-negative", label);
    CHECK(result->nashconv >= -1e-9, message);
}

static void test_weighted_fixture(void)
{
    fixture_t fixture;
    pe_plo4_cert_spec_t spec;
    pe_omaha_range_t ranges[2];
    pe_plo4_cert_result_t result;
    pe_best_response_vector_config_t config =
        pe_best_response_vector_config_default();

    if (setup_fixture(&fixture, 1) != 0)
    {
        CHECK(0, "fixture strength evaluation");
        return;
    }
    build_spec(&fixture, &spec, ranges, fixture_strategy);
    CHECK(pe_plo4_cert_evaluate(&spec, &config, &result) == PE_SOLVER_OK,
          "weighted fixture evaluation");
    CHECK(result.verdict == PE_PLO4_CERT_VERDICT_CERTIFIED,
          "weighted fixture must be certified");
    CHECK(result.reason == PE_PLO4_CERT_REASON_NONE, "certified reason");
    CHECK(result.converged == 1, "both best responses must converge");
    CHECK(strcmp(result.mode, "exact") == 0, "certified mode is exact");
    CHECK(strcmp(result.guarantee, "nash") == 0, "certified guarantee is nash");
    CHECK(result.showdown_path == PE_SHOWDOWN_PATH_PAIRWISE,
          "two four-card combos take the pairwise path");
    CHECK(result.combo_count == 2u, "combo width");
    CHECK(result.live_combos[0] == 2u && result.live_combos[1] == 2u,
          "live combos");
    CHECK(fabs(result.pot_reference - fixture.pot) <= 1e-12, "pot reference");
    CHECK(fabs(result.exploitability_mbb_per_game -
               result.exploitability / fixture.big_blind * 1000.0) <= 1e-9,
          "mbb conversion");
    CHECK(fabs(result.exploitability_pct_of_pot -
               result.exploitability / fixture.pot * 100.0) <= 1e-9,
          "percent-of-pot conversion");
    check_against_oracle("weighted", &fixture, &result);
}

static void test_padded_range(void)
{
    fixture_t fixture;
    pe_plo4_cert_spec_t spec;
    pe_omaha_range_t ranges[2];
    pe_plo4_cert_result_t result;
    pe_best_response_vector_config_t config =
        pe_best_response_vector_config_default();

    if (setup_fixture(&fixture, 1) != 0)
    {
        CHECK(0, "padded fixture strength evaluation");
        return;
    }
    /* A one-combo range must be padded rather than rejected. */
    fixture.count[1] = 1u;
    build_spec(&fixture, &spec, ranges, fixture_strategy);
    CHECK(pe_plo4_cert_evaluate(&spec, &config, &result) == PE_SOLVER_OK,
          "padded range evaluation");
    CHECK(result.verdict == PE_PLO4_CERT_VERDICT_CERTIFIED,
          "padded range must be certified");
    CHECK(result.combo_count == 2u, "padded combo width");
    CHECK(result.live_combos[0] == 2u && result.live_combos[1] == 1u,
          "padded live combos");
    check_against_oracle("padded", &fixture, &result);
}

static void test_uniform_and_weighted_differ(void)
{
    fixture_t fixture;
    pe_plo4_cert_spec_t spec;
    pe_omaha_range_t ranges[2];
    pe_plo4_cert_result_t weighted;
    pe_plo4_cert_result_t uniform;
    pe_best_response_vector_config_t config =
        pe_best_response_vector_config_default();

    if (setup_fixture(&fixture, 1) != 0)
        return;
    build_spec(&fixture, &spec, ranges, fixture_strategy);
    if (pe_plo4_cert_evaluate(&spec, &config, &weighted) != PE_SOLVER_OK)
        return;
    fixture.combos[0][1].weight = 1.0;
    fixture.combos[1][0].weight = 1.0;
    fixture.combos[1][1].weight = 1.0;
    build_spec(&fixture, &spec, ranges, fixture_strategy);
    if (pe_plo4_cert_evaluate(&spec, &config, &uniform) != PE_SOLVER_OK)
        return;
    CHECK(fabs(weighted.policy_value[0] - uniform.policy_value[0]) > 1e-6,
          "range weights must move the policy value");
    CHECK(fabs(weighted.joint_mass - uniform.joint_mass) > 1e-6,
          "range weights must move the joint mass");
    check_against_oracle("uniform", &fixture, &uniform);
}

static void test_not_converged_label(void)
{
    fixture_t fixture;
    pe_plo4_cert_spec_t spec;
    pe_omaha_range_t ranges[2];
    pe_plo4_cert_result_t result;
    pe_best_response_vector_config_t config =
        pe_best_response_vector_config_default();

    if (setup_fixture(&fixture, 1) != 0)
        return;
    build_spec(&fixture, &spec, ranges, fixture_strategy);
    config.max_iterations = 1u;
    CHECK(pe_plo4_cert_evaluate(&spec, &config, &result) == PE_SOLVER_OK,
          "one-iteration evaluation");
    CHECK(result.verdict == PE_PLO4_CERT_VERDICT_NOT_CERTIFIED,
          "one iteration cannot be certified");
    CHECK(result.reason == PE_PLO4_CERT_REASON_NOT_CONVERGED,
          "one iteration reports not_converged");
    CHECK(strcmp(result.mode, "not_certified") == 0,
          "not-converged mode");
    CHECK(strcmp(result.guarantee, "none") == 0,
          "not-converged guarantee");
    CHECK(result.converged == 0, "not-converged flag");
}

static void test_invalid_inputs(void)
{
    fixture_t fixture;
    pe_plo4_cert_spec_t spec;
    pe_omaha_range_t ranges[2];
    pe_plo4_cert_result_t result;
    pe_best_response_vector_config_t config =
        pe_best_response_vector_config_default();

    if (setup_fixture(&fixture, 1) != 0)
        return;
    build_spec(&fixture, &spec, ranges, fixture_strategy);

    spec.board = card(MODERN_RANK_2, MODERN_SUIT_CLUBS) |
                 card(MODERN_RANK_7, MODERN_SUIT_DIAMONDS) |
                 card(MODERN_RANK_T, MODERN_SUIT_HEARTS) |
                 card(MODERN_RANK_J, MODERN_SUIT_SPADES);
    CHECK(pe_plo4_cert_evaluate(&spec, &config, &result) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "four-card board must be rejected");
    spec.board = fixture.board;

    fixture.combos[0][0].cards =
        card(MODERN_RANK_A, MODERN_SUIT_SPADES) |
        card(MODERN_RANK_K, MODERN_SUIT_SPADES) |
        card(MODERN_RANK_Q, MODERN_SUIT_HEARTS);
    build_spec(&fixture, &spec, ranges, fixture_strategy);
    CHECK(pe_plo4_cert_evaluate(&spec, &config, &result) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "three-card combo must be rejected");
    setup_fixture(&fixture, 1);
    build_spec(&fixture, &spec, ranges, fixture_strategy);

    spec.bet_size = 6.0;
    CHECK(pe_plo4_cert_evaluate(&spec, &config, &result) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "a bet larger than the stack must be rejected");
    spec.bet_size = fixture.bet_size;

    spec.strategy = all_fold_strategy;
    CHECK(pe_plo4_cert_evaluate(&spec, &config, &result) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "an unnormalised strategy must be rejected");
    spec.strategy = fixture_strategy;

    fixture.combos[0][0].weight = 0.0;
    fixture.combos[0][1].weight = 0.0;
    build_spec(&fixture, &spec, ranges, fixture_strategy);
    CHECK(pe_plo4_cert_evaluate(&spec, &config, &result) ==
              PE_SOLVER_ERR_INVALID_CONFIG,
          "a range with no live weight must be rejected");
}

int main(void)
{
    test_weighted_fixture();
    test_padded_range();
    test_uniform_and_weighted_differ();
    test_not_converged_label();
    test_invalid_inputs();
    if (failures)
        fprintf(stderr, "test_plo4_certification: %d failure(s)\n", failures);
    else
        puts("test_plo4_certification: certificate matches the oracle");
    return failures ? 1 : 0;
}
