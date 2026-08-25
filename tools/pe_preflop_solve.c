/* pe_preflop_solve.c - product-facing Lane B preflop solver driver.
 *
 * This is the first complete vertical slice of the scalable preflop lane:
 * ranges are parsed once, private hands are sampled with card removal, the
 * sampled deal enters a real betting state, and called terminals are settled
 * by deterministic Monte-Carlo showdown.  Hold'em and PLO4/PLO5/PLO6 are
 * supported for two to six players.
 */

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/range.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/solver/pe_preflop_allin_game.h>
#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/solver/pe_range.h>
#include <poker_eval/solver/pe_solver.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_solver_plan.h>
#include <poker_eval/solver/pe_runtime.h>
#include <poker_eval/solver/pe_ports.h>
#include <poker_eval/solver/pe_rng.h>

#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_ITERATIONS 10000u
#define DEFAULT_SHOWDOWN_SAMPLES 128
#define DEFAULT_STACK 100.0
#define DEFAULT_SMALL_BLIND 0.5
#define DEFAULT_BIG_BLIND 1.0
#define DEFAULT_MIN_RAISE 1.0

typedef struct {
    const char *game;
    const char *range[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    int players;
    uint64_t iterations;
    int showdown_samples;
    double stack;
    double small_blind;
    double big_blind;
    double ante;
    double min_raise;
    double raise_sizes[PE_PREFLOP_ALLIN_MAX_RAISE_SIZES];
    int raise_count;
    int allow_nonallin_call;
    int postflop_streets;
    uint64_t br_samples;
    uint64_t exploitability_interval;
    double target_mbb;
    uint64_t seed;
    const char *output;
    const char *tree;
    pe_algorithm_preset_t algorithm;
    pe_policy_mode_t policy;
    double exponential_lambda;
    double dcfr_alpha;
    double dcfr_beta;
    double dcfr_gamma;
    int have_dcfr_alpha;
    int have_dcfr_beta;
    int have_dcfr_gamma;
    pe_compute_kind_t backend;
    pe_precision_mode_t precision;
    int cpu_threads;
    int show_capabilities;
} options_t;

typedef struct
{
    uint64_t key;
    size_t index;
} report_desc_ref_t;

static size_t find_desc_index(const report_desc_ref_t *refs, size_t count,
                              uint64_t key)
{
    for (size_t i = 0u; i < count; ++i)
        if (refs[i].key == key)
            return refs[i].index;
    return SIZE_MAX;
}

static int report_rank_index(char rank)
{
    const char *ranks = "23456789TJQKA";
    const char *found = strchr(ranks, rank);
    return found ? (int)(found - ranks) : -1;
}

static void report_tree_action_label(const mpf_tree_node_t *node, int index,
                                     char *out, size_t capacity)
{
    const mpf_tree_action_t *action;
    if (!node || !out || capacity == 0u || index < 0 || index >= node->action_count)
        return;
    action = &node->actions[index];
    if (action->type == MPF_TREE_ACTION_FOLD)
        snprintf(out, capacity, "FOLD");
    else if (action->type == MPF_TREE_ACTION_CALL)
        snprintf(out, capacity, "CALL/CHECK");
    else if (action->type == MPF_TREE_ACTION_RAISE &&
             action->size_index >= 0 && action->size_index < node->bet_size_count)
    {
        double size = node->bet_sizes[action->size_index];
        if (fabs(size + 1.0) < 1e-9) snprintf(out, capacity, "ALL-IN");
        else if (node->use_pot_sizing) snprintf(out, capacity, "RAISE %.0f%% POT", size * 100.0);
        else snprintf(out, capacity, "RAISE %.2f", size);
    }
    else
        snprintf(out, capacity, "ACTION");
}

/* A result EV is measured by replaying the sampled deal from the decision,
 * following the current regret-matching policy and sampling future chance.
 * It is deliberately labelled empirical: Lane B does not enumerate the full
 * game tree. */
static double rollout_value(const pe_external_game_t *external,
                            const void *state, int player, pe_rng_t *rng,
                            int depth)
{
    int actor;
    uint16_t count;
    if (!external || !state || !rng || depth > 48)
        return 0.0;
    if (external->is_terminal(state, external->user))
        return external->terminal_value(state, player, external->user);
    actor = external->acting_player(state, external->user);
    if (actor < 0)
    {
        pe_chance_sample_t sample;
        const void *child = external->sample_chance_child
            ? external->sample_chance_child(state, rng, &sample, external->user)
            : NULL;
        double value = child ? rollout_value(external, child, player, rng, depth + 1) : 0.0;
        if (child && external->release_state)
            external->release_state(child, external->user);
        return value * (child ? sample.importance_ratio : 0.0);
    }
    count = external->action_count(state, external->user);
    if (count == 0u)
        return external->terminal_value(state, player, external->user);
    {
        double total = 0.0;
        double draw;
        uint16_t selected = 0u;
        for (uint16_t action = 0u; action < count; ++action)
        {
            double probability = external->action_probability
                ? external->action_probability(state,
                                               external->infoset_key(state, external->user),
                                               action, external->user)
                : 1.0 / (double)count;
            if (probability > 0.0 && isfinite(probability))
                total += probability;
        }
        if (!(total > 0.0))
            total = (double)count;
        draw = pe_rng_uniform01(rng) * total;
        for (uint16_t action = 0u; action < count; ++action)
        {
            double probability = external->action_probability
                ? external->action_probability(state,
                                               external->infoset_key(state, external->user),
                                               action, external->user)
                : 1.0 / (double)count;
            if (!(probability > 0.0) || !isfinite(probability))
                probability = 0.0;
            draw -= probability;
            if (draw <= 0.0) { selected = action; break; }
        }
        {
            const void *child = external->apply_action(state, selected, external->user);
            double value = child ? rollout_value(external, child, player, rng, depth + 1) : 0.0;
            if (child && external->release_state)
                external->release_state(child, external->user);
            return value;
        }
    }
}

static double action_ev(const pe_external_game_t *external,
                        const pe_preflop_betting_state_t *state,
                        uint16_t action, int player, uint64_t seed)
{
    const void *child;
    double total = 0.0;
    /* Per-row EV is a display estimate.  Keep it cheap so report
     * materialisation cannot hide the strategy table for minutes after the
     * solver has reached its stop condition. */
    const int samples = 1;
    if (!external || !state)
        return 0.0;
    child = external->apply_action(state, action, external->user);
    if (!child)
        return 0.0;
    for (int sample = 0; sample < samples; ++sample)
    {
        pe_rng_t rng;
        pe_rng_seed(&rng, pe_rng_derive(seed, (uint64_t)sample +
                                         ((uint64_t)state->tree_node_index << 16) + action));
        total += rollout_value(external, child, player, &rng, 0);
    }
    if (external->release_state)
        external->release_state(child, external->user);
    return total / (double)samples;
}

static void print_strategy_report(const options_t *options,
                                  pe_preflop_allin_game_t *game,
                                  pe_solver_t *solver,
                                  const mpf_tree_def_t *tree)
{
    size_t desc_count = pe_preflop_allin_infodesc_count(game);
    size_t solver_count = pe_solver_strategy_count(solver);
    report_desc_ref_t *refs;
    const pe_external_game_t *external = pe_preflop_allin_external(game);
    size_t emitted = 0u;
    char grid[13][13][8];
    for (int row = 0; row < 13; ++row)
        for (int col = 0; col < 13; ++col)
            snprintf(grid[row][col], sizeof(grid[row][col]), "--");
    printf("report_phase=starting rows=%zu infosets=%zu\n", solver_count, desc_count);
    printf("STRATEGY REPORT variant=%s rows=%zu/%zu ev=empirical-rollout samples=1\n",
           options->game, solver_count, desc_count);
    fflush(stdout);
    if (desc_count == 0u || solver_count == 0u)
    {
        printf("No sampled decision infosets were materialised.\n");
        return;
    }
    refs = calloc(desc_count, sizeof(*refs));
    if (!refs)
        return;
    for (size_t i = 0u; i < desc_count; ++i)
    {
        pe_preflop_infodesc_view_t view;
        if (pe_preflop_allin_infodesc_view_at(game, i, &view) == 0)
        {
            refs[i].key = view.key;
            refs[i].index = i;
        }
    }
    printf("DECISION STEPS (tree branches)\n");
    if (tree)
    {
        int shown = 0;
        for (int node_index = 0; node_index < tree->node_count && shown < 64; ++node_index)
        {
            const mpf_tree_node_t *node = &tree->nodes[node_index];
            if (node->type != MPF_TREE_NODE_PLAYER)
                continue;
            printf("tree_step node=%d id=%s actor=P%d branches=", node_index,
                   node->id ? node->id : "?", node->acting_player + 1);
            for (int action = 0; action < node->action_count; ++action)
            {
                char label[80] = {0};
                report_tree_action_label(node, action, label, sizeof(label));
                printf("%s%s->%d", action ? "|" : "", label,
                       node->actions[action].next_index);
            }
            putchar('\n');
            ++shown;
        }
    }
    else
        printf("tree_step node=generated actor=sampled branches=from sampled decisions\n");
    printf("OBSERVED DECISIONS\n");
    for (size_t i = 0u; i < desc_count && emitted < 64u; ++i)
    {
        int duplicate = 0;
        pe_preflop_infodesc_view_t view;
        if (pe_preflop_allin_infodesc_view_at(game, i, &view) != 0)
            continue;
        for (size_t j = 0u; j < i; ++j)
        {
            pe_preflop_infodesc_view_t previous;
            if (pe_preflop_allin_infodesc_view_at(game, j, &previous) == 0 &&
                previous.tree_node_index == view.tree_node_index &&
                previous.actor == view.actor)
                duplicate = 1;
        }
        if (duplicate) continue;
        printf("step node=%d actor=P%d hand=%s pot=%.2f to_call=%.2f actions=",
               view.tree_node_index, view.actor + 1, view.hand,
               view.pot, view.to_call);
        for (uint16_t a = 0u; a < view.action_count; ++a)
            printf("%s%s", a ? "|" : "", view.actions[a]);
        putchar('\n');
        ++emitted;
    }
    printf("HAND TABLE\nhand\tnode\tactor\tfrequencies\tEV by action\n");
    for (size_t id = 0u; id < solver_count && emitted < 180u; ++id)
    {
        uint64_t key = 0u;
        pe_strategy_query_t query;
        pe_strategy_view_t strategy;
        pe_preflop_infodesc_view_t view;
        size_t desc_index;
        pe_preflop_betting_state_t state;
        if (pe_solver_strategy_key_at(solver, (uint32_t)id, &key) != PE_SOLVER_OK)
            continue;
        desc_index = find_desc_index(refs, desc_count, key);
        if (desc_index == SIZE_MAX ||
            pe_preflop_allin_infodesc_view_at(game, desc_index, &view) != 0 ||
            pe_preflop_allin_infodesc_state_at(game, desc_index, &state) != 0)
            continue;
        query.infoset = (uint32_t)id;
        if (pe_solver_strategy(solver, &query, &strategy) != PE_SOLVER_OK)
            continue;
        printf("%s\t%d\tP%d\t", view.hand, view.tree_node_index,
               view.actor + 1);
        for (uint16_t a = 0u; a < strategy.action_count; ++a)
            printf("%s%s=%.1f%%", a ? "," : "",
                   a < view.action_count ? view.actions[a] : "action",
                   strategy.values[a * strategy.combo_count] * 100.0);
        printf("\t");
        /* Publish the row before doing any rollout.  This makes the strategy
         * grid useful while the optional EV estimates are still being
         * materialised. */
        for (uint16_t a = 0u; a < strategy.action_count; ++a)
            printf("%s%s=pending", a ? "," : "",
                   a < view.action_count ? view.actions[a] : "action");
        putchar('\n');
        fflush(stdout);
        printf("ev_update\t%s\t%d\tP%d\t", view.hand,
               view.tree_node_index, view.actor + 1);
        for (uint16_t a = 0u; a < strategy.action_count; ++a)
            printf("%s%s=%.2f", a ? "," : "",
                   a < view.action_count ? view.actions[a] : "action",
                   action_ev(external, &state, a, view.actor, options->seed));
        putchar('\n');
        fflush(stdout);
        if (strcmp(options->game, "holdem") == 0 && strlen(view.hand) >= 4u)
        {
            int r0 = report_rank_index(view.hand[0]);
            int r1 = report_rank_index(view.hand[2]);
            int row = r0 >= r1 ? 12 - r0 : 12 - r1;
            int col = r0 >= r1 ? 12 - r1 : 12 - r0;
            int best = 0;
            for (uint16_t a = 1u; a < strategy.action_count; ++a)
                if (strategy.values[a * strategy.combo_count] >
                    strategy.values[best * strategy.combo_count])
                    best = a;
            if (r0 >= 0 && r1 >= 0 && row >= 0 && row < 13 && col >= 0 && col < 13)
                snprintf(grid[row][col], sizeof(grid[row][col]), "%c",
                         best < view.action_count && view.actions[best][0]
                             ? (char)toupper((unsigned char)view.actions[best][0])
                             : '?');
        }
        ++emitted;
    }
    if (strcmp(options->game, "holdem") == 0)
    {
        const char *ranks = "AKQJT98765432";
        printf("RANGE GRID (highest-frequency action; F=fold C=call R=raise)\n   ");
        for (int col = 0; col < 13; ++col) printf("%c ", ranks[col]);
        putchar('\n');
        for (int row = 0; row < 13; ++row)
        {
            printf("%c  ", ranks[row]);
            for (int col = 0; col < 13; ++col)
                printf("%s ", grid[row][col]);
            putchar('\n');
        }
    }
    if (emitted >= 180u)
        printf("... report capped at 180 visible rows; the solve storage still contains all %zu infosets.\n", solver_count);
    printf("report_phase=complete rows=%zu\n", emitted);
    fflush(stdout);
    free(refs);
}

static void usage(FILE *stream)
{
    fprintf(stream,
        "Usage: pe-preflop-solve [options]\n"
        "  --game holdem|plo4|plo5|plo6  preflop game variant\n"
        "  --players N                  2 to 6 players (default 2)\n"
        "  --rangeN TEXT                private range for player N (default 100%%)\n"
        "  --iterations N               MCCFR iterations (default %u)\n"
        "  --samples N                  showdown boards per called terminal\n"
        "  --stack BB                   effective stack for both players\n"
        "  --sb BB --bb BB --ante BB   forced bets\n"
        "  --min-raise BB               minimum raise increment\n"
        "  --raise AMOUNT[,AMOUNT...]   raise increments above the call\n"
        "  --allow-calls                allow calls before all-in\n"
        "  --postflop                   continue through flop, turn and river\n"
        "  --tree FILE                 import a Monker preflop tree and run it to showdown\n"
        "  --algorithm NAME             Lane B: external-mccfr, external-dcfr,\n"
        "                               outcome-mccfr or external-ecfr\n"
        "                               (full-tree cfr/cfr+/dcfr presets are\n"
        "                               rejected by this sampled driver)\n"
        "  --policy NAME                regret-matching or exponential\n"
        "  --lambda X                   exponential policy temperature (> 0)\n"
        "  --alpha X                   DCFR positive-regret discount exponent (>= 0)\n"
        "  --beta X                    DCFR negative-regret discount exponent (>= 0)\n"
        "  --gamma X                   DCFR average-strategy exponent (>= 0)\n"
        "  --backend NAME               auto, cpu_ref, cpu_par, cuda, opencl\n"
        "  --precision NAME             f64, f32, mixed, fixed16\n"
        "  --threads N                  worker threads for cpu_par\n"
        "  --show-capabilities          print detected CPU/SIMD/backend capabilities\n"
        "  --br-samples N               sampled unilateral BR rollouts\n"
        "  --target-mbb N               stop/report when empirical BR <= N mBB\n"
        "  --exploitability-interval N  measure/print convergence every N iterations\n"
        "  --seed N                     deterministic RNG seed\n"
        "  --output FILE                write a JSON run report\n"
        "  --help                       show this help\n", DEFAULT_ITERATIONS);
}

static int parse_u64(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value;
    if (!text || !out || !*text)
        return -1;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno || end == text || *end != '\0')
        return -1;
    *out = (uint64_t)value;
    return 0;
}

static int parse_positive_double(const char *text, double *out)
{
    char *end = NULL;
    double value;
    if (!text || !out || !*text)
        return -1;
    errno = 0;
    value = strtod(text, &end);
    if (errno || end == text || *end != '\0' || !(value > 0.0))
        return -1;
    *out = value;
    return 0;
}

static int parse_nonnegative_double(const char *text, double *out)
{
    char *end = NULL;
    double value;
    if (!text || !out || !*text)
        return -1;
    errno = 0;
    value = strtod(text, &end);
    if (errno || end == text || *end != '\0' || !isfinite(value) || value < 0.0)
        return -1;
    *out = value;
    return 0;
}

static int range_option_index(const char *arg)
{
    if (!arg || strncmp(arg, "--range", 7) != 0 ||
        arg[7] < '0' || arg[7] > '5' || arg[8] != '\0')
        return -1;
    return arg[7] - '0';
}

static pe_preflop_variant_t parse_variant(const char *name)
{
    if (name && strcmp(name, "plo4") == 0) return PE_PREFLOP_PLO4;
    if (name && strcmp(name, "plo5") == 0) return PE_PREFLOP_PLO5;
    if (name && strcmp(name, "plo6") == 0) return PE_PREFLOP_PLO6;
    return name && strcmp(name, "holdem") == 0
        ? PE_PREFLOP_HOLDEM : (pe_preflop_variant_t)-1;
}

static int preflop_algorithm_supported(pe_algorithm_preset_t algorithm)
{
    /* Lane B samples chance and opponent actions. The full-tree presets are
     * valid solver algorithms elsewhere, but this driver must not silently
     * reinterpret them as sampled CFR. */
    return algorithm == PE_PRESET_EXTERNAL_MCCFR ||
           algorithm == PE_PRESET_EXTERNAL_DCFR ||
           algorithm == PE_PRESET_OUTCOME_MCCFR ||
           algorithm == PE_PRESET_EXTERNAL_ECFR;
}

static int parse_raise_sizes(const char *text, options_t *options)
{
    char buffer[512];
    char *token;
    if (!text || !options || strlen(text) >= sizeof(buffer))
        return -1;
    snprintf(buffer, sizeof(buffer), "%s", text);
    token = strtok(buffer, ",");
    while (token != NULL) {
        double amount;
        if (options->raise_count >= PE_PREFLOP_ALLIN_MAX_RAISE_SIZES ||
            parse_positive_double(token, &amount) != 0)
            return -1;
        options->raise_sizes[options->raise_count++] = amount;
        token = strtok(NULL, ",");
    }
    return options->raise_count > 0 ? 0 : -1;
}

static int parse_options(int argc, char **argv, options_t *options)
{
    memset(options, 0, sizeof(*options));
    options->game = "holdem";
    for (int player = 0; player < PE_PREFLOP_ALLIN_MAX_PLAYERS; ++player)
        options->range[player] = "100%";
    options->players = 2;
    options->iterations = DEFAULT_ITERATIONS;
    options->showdown_samples = DEFAULT_SHOWDOWN_SAMPLES;
    options->stack = DEFAULT_STACK;
    options->small_blind = DEFAULT_SMALL_BLIND;
    options->big_blind = DEFAULT_BIG_BLIND;
    options->ante = 0.0;
    options->min_raise = DEFAULT_MIN_RAISE;
    options->br_samples = 256u;
    options->exploitability_interval = 256u;
    options->target_mbb = 1.0;
    options->seed = UINT64_C(0x50455f5052464c42);
    options->algorithm = PE_PRESET_EXTERNAL_MCCFR;
    options->policy = PE_POLICY_COUNT;
    options->exponential_lambda = 1.0;
    options->dcfr_alpha = 1.5;
    options->dcfr_beta = 0.0;
    options->dcfr_gamma = 2.0;
    options->backend = PE_COMPUTE_AUTO;
    options->precision = PE_PREC_F64;
    options->cpu_threads = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *value = NULL;
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(stdout);
            return 1;
        }
        if (i + 1 < argc)
            value = argv[i + 1];
        if ((strcmp(arg, "--game") == 0 || range_option_index(arg) >= 0 ||
             strcmp(arg, "--iterations") == 0 || strcmp(arg, "--players") == 0 ||
             strcmp(arg, "--samples") == 0 || strcmp(arg, "--stack") == 0 ||
             strcmp(arg, "--sb") == 0 || strcmp(arg, "--bb") == 0 ||
             strcmp(arg, "--ante") == 0 || strcmp(arg, "--br-samples") == 0 ||
             strcmp(arg, "--min-raise") == 0 || strcmp(arg, "--raise") == 0 ||
             strcmp(arg, "--seed") == 0 || strcmp(arg, "--output") == 0 ||
             strcmp(arg, "--tree") == 0 ||
             strcmp(arg, "--algorithm") == 0 ||
             strcmp(arg, "--policy") == 0 ||
             strcmp(arg, "--lambda") == 0 ||
             strcmp(arg, "--alpha") == 0 ||
             strcmp(arg, "--beta") == 0 ||
             strcmp(arg, "--gamma") == 0 ||
             strcmp(arg, "--backend") == 0 ||
             strcmp(arg, "--precision") == 0 ||
             strcmp(arg, "--threads") == 0 ||
             strcmp(arg, "--target-mbb") == 0 ||
             strcmp(arg, "--exploitability-interval") == 0) &&
            (!value || value[0] == '-')) {
            fprintf(stderr, "missing value for %s\n", arg);
            return -1;
        }
        if (strcmp(arg, "--game") == 0) options->game = value;
        else if (range_option_index(arg) >= 0)
            options->range[range_option_index(arg)] = value;
        else if (strcmp(arg, "--iterations") == 0) {
            if (parse_u64(value, &options->iterations) != 0 || options->iterations == 0u)
                return -1;
        } else if (strcmp(arg, "--players") == 0) {
            uint64_t players;
            if (parse_u64(value, &players) != 0 || players < 2u ||
                players > PE_PREFLOP_ALLIN_MAX_PLAYERS) return -1;
            options->players = (int)players;
        } else if (strcmp(arg, "--samples") == 0) {
            uint64_t samples;
            if (parse_u64(value, &samples) != 0 || samples == 0u || samples > 1000000u)
                return -1;
            options->showdown_samples = (int)samples;
        } else if (strcmp(arg, "--stack") == 0) {
            if (parse_positive_double(value, &options->stack) != 0) return -1;
        } else if (strcmp(arg, "--sb") == 0) {
            if (parse_positive_double(value, &options->small_blind) != 0) return -1;
        } else if (strcmp(arg, "--bb") == 0) {
            if (parse_positive_double(value, &options->big_blind) != 0) return -1;
        } else if (strcmp(arg, "--ante") == 0) {
            if (strcmp(value, "0") == 0) options->ante = 0.0;
            else if (parse_positive_double(value, &options->ante) != 0) return -1;
        } else if (strcmp(arg, "--min-raise") == 0) {
            if (parse_positive_double(value, &options->min_raise) != 0) return -1;
        } else if (strcmp(arg, "--raise") == 0) {
            if (parse_raise_sizes(value, options) != 0) return -1;
        } else if (strcmp(arg, "--allow-calls") == 0) {
            options->allow_nonallin_call = 1;
            continue;
        } else if (strcmp(arg, "--postflop") == 0) {
            options->postflop_streets = 1;
            continue;
        } else if (strcmp(arg, "--br-samples") == 0) {
            if (parse_u64(value, &options->br_samples) != 0 ||
                options->br_samples == 0u || options->br_samples > UINT32_MAX)
                return -1;
        } else if (strcmp(arg, "--target-mbb") == 0) {
            char *end = NULL;
            double target;
            errno = 0;
            target = strtod(value, &end);
            if (errno || end == value || *end != '\0' || target < 0.0)
                return -1;
            options->target_mbb = target;
        } else if (strcmp(arg, "--exploitability-interval") == 0) {
            if (parse_u64(value, &options->exploitability_interval) != 0 ||
                options->exploitability_interval == 0u)
                return -1;
        } else if (strcmp(arg, "--seed") == 0) {
            if (parse_u64(value, &options->seed) != 0) return -1;
        } else if (strcmp(arg, "--output") == 0) options->output = value;
        else if (strcmp(arg, "--tree") == 0) options->tree = value;
        else if (strcmp(arg, "--algorithm") == 0) {
            options->algorithm = pe_preset_from_name(value);
            if (options->algorithm == PE_PRESET_COUNT) return -1;
        } else if (strcmp(arg, "--policy") == 0) {
            options->policy = pe_policy_from_name(value);
            if (options->policy == PE_POLICY_COUNT) return -1;
        } else if (strcmp(arg, "--lambda") == 0) {
            if (parse_positive_double(value, &options->exponential_lambda) != 0)
                return -1;
        } else if (strcmp(arg, "--alpha") == 0) {
            if (parse_nonnegative_double(value, &options->dcfr_alpha) != 0)
                return -1;
            options->have_dcfr_alpha = 1;
        } else if (strcmp(arg, "--beta") == 0) {
            if (parse_nonnegative_double(value, &options->dcfr_beta) != 0)
                return -1;
            options->have_dcfr_beta = 1;
        } else if (strcmp(arg, "--gamma") == 0) {
            if (parse_nonnegative_double(value, &options->dcfr_gamma) != 0)
                return -1;
            options->have_dcfr_gamma = 1;
        } else if (strcmp(arg, "--backend") == 0) {
            options->backend = pe_compute_kind_from_name(value);
            if (options->backend == PE_COMPUTE_COUNT) return -1;
        } else if (strcmp(arg, "--precision") == 0) {
            options->precision = pe_precision_from_name(value);
            if (options->precision == PE_PREC_COUNT) return -1;
        } else if (strcmp(arg, "--threads") == 0) {
            uint64_t threads;
            if (parse_u64(value, &threads) != 0 || threads > INT_MAX) return -1;
            options->cpu_threads = (int)threads;
        } else if (strcmp(arg, "--show-capabilities") == 0) {
            options->show_capabilities = 1;
            continue;
        }
        else {
            fprintf(stderr, "unknown option: %s\n", arg);
            return -1;
        }
        ++i;
    }
    if (parse_variant(options->game) == (pe_preflop_variant_t)-1 ||
        options->players < 2 || options->players > PE_PREFLOP_ALLIN_MAX_PLAYERS ||
        options->big_blind < options->small_blind ||
        options->ante < 0.0 || options->ante >= options->big_blind ||
        options->stack <= options->big_blind || options->cpu_threads < 0)
        return -1;
    if (!preflop_algorithm_supported(options->algorithm)) {
        fprintf(stderr,
                "preflop Lane B requires a sampled preset: external-mccfr, "
                "external-dcfr, outcome-mccfr or external-ecfr; '%s' is a "
                "full-tree preset and is not silently remapped\n",
                pe_preset_name(options->algorithm));
        return -1;
    }
    return 0;
}

static const char *guarantee_name(pe_guarantee_t guarantee)
{
    switch (guarantee) {
    case PE_GUARANTEE_UNSPECIFIED: return "unspecified";
    case PE_GUARANTEE_NASH: return "nash";
    case PE_GUARANTEE_NO_REGRET_ONLY: return "no-regret-only";
    case PE_GUARANTEE_EMPIRICAL: return "empirical";
    default: return "unspecified";
    }
}

static void write_report(const char *path, const options_t *options,
                         const pe_metrics_t *metrics, pe_progress_t *progress,
                         size_t infosets)
{
    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        return;
    }
    fprintf(file,
        "{\"schema\":\"pe-preflop-solve/v1\","
        "\"game\":\"%s\",\"players\":%d,"
        "\"iterations\":%" PRIu64 ",\"showdown_samples\":%d,"
        "\"stack\":%.17g,\"small_blind\":%.17g,\"big_blind\":%.17g,\"ante\":%.17g,"
        "\"allow_nonallin_call\":%s,\"postflop_streets\":%s,\"br_samples\":%" PRIu64 ",\"infosets\":%zu,"
        "\"progress\":{\"iteration\":%" PRIu64 ",\"complete\":%s},"
        "\"metrics\":{\"guarantee\":\"%s\",\"exploitability_raw\":%.17g,"
        "\"exploitability_mbb_per_game\":%.17g,\"big_blind\":%.17g}}\n",
        options->game, options->players, options->iterations,
        options->showdown_samples, options->stack,
        options->small_blind, options->big_blind, options->ante,
        options->allow_nonallin_call ? "true" : "false",
        options->postflop_streets ? "true" : "false", options->br_samples,
        infosets,
        progress->iteration, progress->complete ? "true" : "false",
        guarantee_name(metrics->guarantee), metrics->exploitability_raw,
        metrics->exploitability_mbb_per_game, options->big_blind);
    fclose(file);
}

int main(int argc, char **argv)
{
    options_t options;
    pe_range_t *ranges[PE_PREFLOP_ALLIN_MAX_PLAYERS] = {NULL};
    pe_preflop_allin_rules_t rules;
    pe_preflop_allin_game_t *game = NULL;
    pe_solver_config_t config;
    pe_solver_deps_t deps;
    pe_solver_t *solver = NULL;
    pe_solver_status_t status;
    pe_progress_t progress = {0};
    pe_metrics_t metrics = {0};
    StdDeck_CardMask dead;
    pe_preflop_variant_t variant;
    mpf_tree_def_t *tree = NULL;
    pe_monker_tree_header_t tree_header;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    {
        int option_status = parse_options(argc, argv, &options);
        if (option_status == 1)
            return 0;
        if (option_status != 0) {
        usage(stderr);
            return 2;
        }
    }
    if (options.show_capabilities)
    {
        pe_runtime_capabilities_t runtime;
        if (pe_runtime_probe(&runtime) != 0)
            return 1;
        printf("runtime cpus=%u openmp=%s simd=%s\n",
               runtime.logical_cpus, runtime.openmp_available ? "yes" : "no",
               pe_runtime_simd_name(runtime.simd));
        for (int i = 0; i < PE_COMPUTE_COUNT; ++i)
        {
            char line[256];
            pe_runtime_backend_status(&runtime.backends[i], line, sizeof(line));
            printf("%s\n", line);
        }
        return 0;
    }
    {
        pe_runtime_capabilities_t runtime;
        const pe_runtime_backend_info_t *backend;
        if (pe_runtime_probe(&runtime) != 0)
            return 1;
        if (options.backend == PE_COMPUTE_AUTO)
        {
            options.backend = pe_runtime_recommended_backend(&runtime);
            if (options.backend == PE_COMPUTE_AUTO)
            {
                fprintf(stderr, "no validated runtime solver backend is available\n");
                return 2;
            }
            printf("backend_auto_resolved=%s\n",
                   pe_compute_kind_name(options.backend));
        }
        backend = &runtime.backends[options.backend];
        if (!backend->runtime_available || !backend->validated)
        {
            fprintf(stderr, "backend refused: %s (%s)\n",
                    pe_compute_kind_name(options.backend), backend->reason);
            return 2;
        }
    }
    variant = parse_variant(options.game);
    memset(&tree_header, 0, sizeof(tree_header));
    if (options.tree)
    {
        pe_monker_status_t tree_status = pe_monker_tree_read_header(
            options.tree, &tree_header);
        if (tree_status == PE_MONKER_OK)
            tree_status = pe_monker_tree_load(options.tree, &tree);
        if (tree_status != PE_MONKER_OK || !tree)
        {
            fprintf(stderr, "could not load Monker tree %s: %s\n",
                    options.tree, pe_monker_status_string(tree_status));
            goto fail;
        }
        if (tree_header.street != 0 ||
            tree_header.player_count != (uint32_t)options.players)
        {
            fprintf(stderr,
                    "tree must be preflop and contain %d players (got street=%u players=%u)\n",
                    options.players, tree_header.street, tree_header.player_count);
            goto fail;
        }
    }
    StdDeck_CardMask_RESET(dead);
    for (int player = 0; player < options.players; ++player) {
        enum_game_t range_game = variant == PE_PREFLOP_HOLDEM ? game_holdem
            : variant == PE_PREFLOP_PLO4 ? game_omaha
            : variant == PE_PREFLOP_PLO5 ? game_omaha5 : game_omaha6;
        if (pe_solver_range_parse(range_game, options.range[player], dead,
                                  &ranges[player]) != PE_SOLVER_OK ||
            !ranges[player]) {
            fprintf(stderr, "invalid %s range%d: %s\n", options.game, player,
                    options.range[player]);
            goto fail;
        }
    }

    memset(&rules, 0, sizeof(rules));
    rules.variant = variant;
    rules.player_count = options.players;
    for (int player = 0; player < options.players; ++player)
        rules.stacks[player] = options.stack;
    rules.small_blind = options.small_blind;
    rules.big_blind = options.big_blind;
    rules.ante = options.ante;
    rules.min_raise = options.min_raise;
    rules.raise_cap = 0;
    rules.raise_count = options.raise_count;
    rules.allow_nonallin_call = options.allow_nonallin_call;
    rules.postflop_streets = options.postflop_streets;
    rules.tree = tree;
    rules.tree_showdown = tree != NULL ? 1 : 0;
    rules.showdown_samples = options.showdown_samples;
    rules.showdown_seed = options.seed;
    memcpy(rules.raise_sizes, options.raise_sizes, sizeof(rules.raise_sizes));
    game = pe_preflop_allin_game_create(&rules, ranges);
    if (!game) {
        fprintf(stderr, "could not create preflop game\n");
        goto fail;
    }

    config = pe_solver_config_default();
    config.algorithm.preset = options.algorithm;
    if (options.policy != PE_POLICY_COUNT ||
        fabs(options.exponential_lambda - 1.0) > 1e-15 ||
        options.have_dcfr_alpha || options.have_dcfr_beta || options.have_dcfr_gamma) {
        if (pe_preset_expand(options.algorithm, &config.algorithm) != 0)
            goto fail;
        config.algorithm.preset = PE_PRESET_CUSTOM;
        if (options.policy != PE_POLICY_COUNT) {
            config.algorithm.policy = options.policy;
            if (options.policy == PE_POLICY_EXPONENTIAL)
                config.algorithm.regret = PE_REGRET_LEGACY_EXP;
        }
    }
    config.algorithm.exponential_lambda = options.exponential_lambda;
    if (options.have_dcfr_alpha)
        config.algorithm.dcfr_alpha = options.dcfr_alpha;
    if (options.have_dcfr_beta)
        config.algorithm.dcfr_beta = options.dcfr_beta;
    if (options.have_dcfr_gamma)
        config.algorithm.dcfr_gamma = options.dcfr_gamma;
    config.execution.backend = options.backend;
    config.execution.stages.traversal = options.backend;
    config.execution.stages.update = options.backend;
    config.execution.stages.terminal_eval = options.backend;
    config.execution.precision = options.precision;
    config.execution.cpu_threads = options.cpu_threads;
    config.execution.deterministic = 1;
    config.execution.sample_batch_size = 1u;
    config.problem.expected_infosets = 4096u;
    config.problem.expected_actions = 8u;
    config.problem.expected_combos = 1u;
    config.max_iterations = options.iterations;
    config.execution.big_blind = options.big_blind;
    config.target_exploitability_mbb = options.target_mbb;
    config.exploitability_interval = options.exploitability_interval;
    config.br_samples = (uint32_t)options.br_samples;
    config.seed = options.seed;
    deps = pe_solver_deps_default();
    deps.external_game = pe_preflop_allin_external(game);
    deps.telemetry = pe_telemetry_stdout();
    solver = pe_solver_create(&config, &deps);
    status = solver ? pe_solver_run(solver) : PE_SOLVER_ERR_OUT_OF_MEMORY;
    if (solver)
        (void)pe_solver_progress(solver, &progress);
    if (solver)
        (void)pe_solver_metrics(solver, &metrics);
    if (status != PE_SOLVER_OK) {
        fprintf(stderr, "preflop solve failed: status=%d\n", (int)status);
        goto fail;
    }
    {
        pe_preflop_allin_game_set_storage(
            game, (pe_storage_t *)pe_solver_get_storage_instance(solver));
        size_t infosets = pe_preflop_allin_infodesc_count(game);
        printf("preflop_solver=lane-b algorithm=%s traversal=%s regret=%s policy=%s "
               "backend=%s precision=%s dcfr_alpha=%.6g dcfr_beta=%.6g "
               "dcfr_gamma=%.6g lambda=%.6g game=%s players=%d postflop=%d tree=%s\n",
               config.algorithm.preset == PE_PRESET_CUSTOM
                   ? "custom" : pe_preset_name(options.algorithm),
               pe_traversal_name(config.algorithm.traversal),
               pe_regret_name(config.algorithm.regret),
               pe_policy_name(config.algorithm.policy),
               pe_compute_kind_name(options.backend),
               pe_precision_name(options.precision),
               config.algorithm.dcfr_alpha, config.algorithm.dcfr_beta,
               config.algorithm.dcfr_gamma, config.algorithm.exponential_lambda,
               options.game, options.players, options.postflop_streets,
               tree ? options.tree : "none");
        printf("iterations=%" PRIu64 " complete=%d infosets=%zu\n",
               progress.iteration, progress.complete, infosets);
        printf("solver_phase=complete stop_reason=%s report=starting\n",
               options.target_mbb > 0.0 &&
               metrics.exploitability_mbb_per_game <= options.target_mbb
                   ? "target" : "max_iterations");
        fflush(stdout);
        printf("guarantee=%s exploitability_raw=%.6f exploitability_mbb=%.6f br_samples=%" PRIu64 "\n",
               guarantee_name(metrics.guarantee), metrics.exploitability_raw,
               metrics.exploitability_mbb_per_game, options.br_samples);
        print_strategy_report(&options, game, solver, tree);
        if (options.output)
            write_report(options.output, &options, &metrics, &progress, infosets);
    }
    pe_solver_destroy(solver);
    pe_preflop_allin_game_destroy(game);
    mpf_tree_free(tree);
    for (int player = 0; player < options.players; ++player)
        pe_range_free(ranges[player]);
    return 0;

fail:
    pe_solver_destroy(solver);
    pe_preflop_allin_game_destroy(game);
    mpf_tree_free(tree);
    for (int player = 0; player < options.players; ++player)
        pe_range_free(ranges[player]);
    return 1;
}
