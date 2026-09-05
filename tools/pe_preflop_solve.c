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
#include <poker_eval/solver/pe_persist.h>
#include <poker_eval/solver/pe_rng.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/engine/solvers/cfr/board_canonical.h>
#include <poker_eval/engine/solvers/cfr/board_texture.h>

#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/solver/domain/finite_double.h"

#define DEFAULT_ITERATIONS 10000u
#define DEFAULT_SHOWDOWN_SAMPLES 128
/* Tree nodes the per-node row quota tracks; anything beyond shares the last
 * bucket, which only costs those nodes a fair share between them. */
#define PE_REPORT_MAX_NODES 512u
#define DEFAULT_STACK 100.0
#define DEFAULT_REPORT_ROWS 2000u
#define DEFAULT_SMALL_BLIND 0.5
#define DEFAULT_BIG_BLIND 1.0
#define DEFAULT_MIN_RAISE 1.0

typedef struct {
    const char *game;
    const char *range[PE_PREFLOP_ALLIN_MAX_PLAYERS];
    int players;
    uint64_t iterations;
    int verbose;
    uint64_t max_ram_bytes;
    uint64_t desc_limit_bytes;
    int desc_limit_set;
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
    const char *checkpoint_path;
    const char *resume_path;
    uint64_t checkpoint_interval;
    /* Postflop root (Lane B street trees).  street names preflop (default,
     * classic blind-posted root) or flop/turn/river (root at that street
     * with the fixed --board, --pot and first actor). */
    const char *street;
    const char *board;
    /* Hand rows printed in the report.  0 means every sampled infoset.  This
     * used to be a hardcoded 180 shared across every node, so a tree with a
     * couple of dozen decisions showed a handful of hands each. */
    size_t report_rows;
    const char *board_abstraction;
    /* Board to interrogate the SOLVER for, rather than sampling the report.
     * The report prints at most --report-rows sampled infosets; a query walks
     * every infoset the solve holds and emits the ones whose board matches,
     * uncapped, so a chosen board comes back with its whole hand table. */
    const char *query_board;
    /* Stay alive after the report and answer board queries on stdin, instead
     * of exiting and forcing a fresh process (and a fresh solve) per board.
     * The solve's infoset descriptions only exist in the process that played
     * them -- the checkpoint stores strategies, not descriptions -- so a
     * query is only complete while that process is still up. */
    int interactive;
    /* Resolved --board-abstraction, so the report and the query read the
     * solve with the same rule the solve was keyed with. */
    int board_texture_level;
    double pot;
    int have_pot;
    int to_act;
    int have_to_act;
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



static int report_rank_index(char rank)
{
    const char *ranks = "23456789TJQKA";
    const char *found = strchr(ranks, rank);
    return found ? (int)(found - ranks) : -1;
}

static int tree_path_is_json(const char *path)
{
    size_t length;
    if (!path)
        return 0;
    length = strnlen(path, 4096u);
    return length >= 5u && strcmp(path + length - 5u, ".json") == 0;
}

static int infer_json_tree_header(const mpf_tree_def_t *tree,
                                  pe_monker_tree_header_t *header)
{
    int players = 2;
    int street;
    int first_to_act;
    if (!tree || !header || !tree->nodes || tree->root_index < 0 ||
        tree->root_index >= tree->node_count)
        return 0;
    for (int node = 0; node < tree->node_count; ++node)
    {
        const mpf_tree_node_t *entry = &tree->nodes[node];
        if (entry->acting_player >= 0 && entry->acting_player + 1 > players)
            players = entry->acting_player + 1;
        if (entry->has_snapshot && entry->snapshot.has_num_players &&
            entry->snapshot.num_players >= 2 && entry->snapshot.num_players <= 8)
            players = entry->snapshot.num_players;
    }
    street = tree->nodes[tree->root_index].street;
    first_to_act = tree->nodes[tree->root_index].acting_player;
    if (street < MPF_STREET_PREFLOP || street > MPF_STREET_RIVER ||
        players < 2 || players > 8)
        return 0;
    memset(header, 0, sizeof(*header));
    header->player_count = (uint32_t)players;
    header->street = (uint32_t)street;
    header->first_to_act = first_to_act >= 0 ? first_to_act : 0;
    return 1;
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
            if (probability > 0.0 && pe_finite_double(probability))
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
            if (!(probability > 0.0) || !pe_finite_double(probability))
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

/* Do two boards name the same spot, under the rule this run keyed its
 * infosets with?  Exact suit isomorphism normally; the texture id when a
 * board abstraction is on, so a query reads the solve the same way the solve
 * wrote it. */
/* Does this infoset carry a strategy, or is it still the uniform one regret
 * matching starts from?  A sampled solve materialises an infoset the first
 * time it is reached, so "exists" and "has data" are not the same thing. */
static int strategy_has_data(const pe_strategy_view_t *strategy)
{
    double uniform;
    if (!strategy || strategy->action_count == 0u)
        return 0;
    uniform = 1.0 / (double)strategy->action_count;
    for (uint16_t a = 0u; a < strategy->action_count; ++a)
        if (fabs(strategy->values[a * strategy->combo_count] - uniform) > 1e-6)
            return 1;
    return 0;
}

static int query_board_matches(mask_t a, mask_t b, int level)
{
    int na = (int)mask_popcount(a);
    int nb = (int)mask_popcount(b);
    char ka[32];
    char kb[32];
    if (na != nb || na == 0)
        return 0;
    if (level > 0)
        return pe_board_texture_id(a, (pe_texture_filter_level_t)level) ==
               pe_board_texture_id(b, (pe_texture_filter_level_t)level);
    return pe_board_canonical_key(a, na, ka, sizeof(ka)) == 0 &&
           pe_board_canonical_key(b, nb, kb, sizeof(kb)) == 0 &&
           strcmp(ka, kb) == 0;
}

static void print_strategy_report(const options_t *options,
                                  pe_preflop_allin_game_t *game,
                                  pe_solver_t *solver,
                                  const mpf_tree_def_t *tree)
{
    size_t desc_count = pe_preflop_allin_infodesc_count(game);
    size_t solver_count = pe_solver_strategy_count(solver);
    const pe_external_game_t *external = pe_preflop_allin_external(game);
    size_t report_rows = options->report_rows;
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
    fflush(stdout);
    printf("OBSERVED DECISIONS\n");
    /* Deduplicate on (tree node, actor), keeping first-occurrence order.
     * NOTE: this used to rescan all previous infosets per row (O(n^2)
     * view_at calls); on long runs (n = 77594) that is ~3e9 calls and the
     * report never finishes — the Studio waits forever on Stop.  A small
     * seen-list (at most 64 entries are ever emitted) makes it O(64n)
     * with byte-identical output. */
    {
        struct {
            int node;
            int actor;
        } seen[64];
        size_t seen_count = 0u;
        for (size_t i = 0u; i < desc_count && emitted < 64u; ++i)
        {
            int duplicate = 0;
            pe_preflop_infodesc_view_t view;
            if (pe_preflop_allin_infodesc_view_at(game, i, &view) != 0)
                continue;
            for (size_t k = 0u; k < seen_count; ++k)
            {
                if (seen[k].node == view.tree_node_index &&
                    seen[k].actor == (int)view.actor)
                {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) continue;
            if (seen_count < 64u)
            {
                seen[seen_count].node = view.tree_node_index;
                seen[seen_count].actor = (int)view.actor;
                ++seen_count;
            }
            printf("step node=%d actor=P%d hand=%s pot=%.2f to_call=%.2f actions=",
                   view.tree_node_index, view.actor + 1, view.hand,
                   view.pot, view.to_call);
            for (uint16_t a = 0u; a < view.action_count; ++a)
                printf("%s%s", a ? "|" : "", view.actions[a]);
            putchar('\n');
            ++emitted;
        }
    }
    fflush(stdout);
    fflush(stdout);
    printf("HAND TABLE\nhand\tnode\tactor\tfrequencies\tEV by action\n");
    /* A query walks every infoset and keeps only the matching boards, so the
     * row cap does not apply: the point is to return a board's COMPLETE hand
     * table, which the sampled report structurally cannot. */
    mask_t query_mask = MASK_EMPTY;
    int querying = 0;
    if (options->query_board && *options->query_board)
    {
        query_mask = string_to_mask(options->query_board);
        querying = mask_popcount(query_mask) > 0;
        printf("BOARD QUERY board=%s abstraction=%s\n",
               options->query_board,
               options->board_abstraction ? options->board_abstraction : "none");
        fflush(stdout);
    }
    /* Share the row budget between the tree's decision nodes instead of
     * handing it out first-come-first-served.
     *
     * A multi-street solve at an exact board abstraction has millions of
     * infosets, nearly all of them postflop -- one per board it ever sampled.
     * Walking them in id order spent the whole budget on a handful of
     * postflop nodes: on nlhe_hu_full at 100k iterations node 19 took 525 of
     * the 4000 rows while the PREFLOP node, which has only 169 infosets in
     * total, got 117 of them.  The Studio's preflop grid was therefore always
     * missing a third of its hands, however long the solve ran.
     *
     * Each node gets an equal share, capped at what it actually has; whatever
     * that leaves over is redistributed to the nodes that wanted more.  A
     * small node is thus always emitted whole. */
    int node_quota[PE_REPORT_MAX_NODES];
    size_t node_emitted[PE_REPORT_MAX_NODES];
    if (!querying && report_rows > 0u)
    {
        size_t node_have[PE_REPORT_MAX_NODES];
        size_t nodes_present = 0u;
        size_t budget = report_rows;
        for (size_t n = 0u; n < PE_REPORT_MAX_NODES; ++n)
        {
            node_have[n] = 0u;
            node_quota[n] = 0;
            node_emitted[n] = 0u;
        }
        for (size_t id = 0u; id < solver_count; ++id)
        {
            uint64_t key = 0u;
            size_t desc_index;
            pe_preflop_betting_state_t state;
            int node;
            if (pe_solver_strategy_key_at(solver, (uint32_t)id, &key) != PE_SOLVER_OK)
                continue;
            if (pe_preflop_allin_infodesc_find(game, key, &desc_index) != 0 ||
                pe_preflop_allin_infodesc_state_at(game, desc_index, &state) != 0)
                continue;
            node = state.tree_node_index;
            if (node < 0 || node >= (int)PE_REPORT_MAX_NODES)
                node = (int)PE_REPORT_MAX_NODES - 1;
            if (node_have[node] == 0u)
                ++nodes_present;
            ++node_have[node];
        }
        /* Repeated equal shares: each round gives every still-hungry node the
         * same slice, so nodes smaller than their share are satisfied whole
         * and release the remainder to the others. */
        while (budget > 0u && nodes_present > 0u)
        {
            size_t hungry = 0u;
            size_t share;
            for (size_t n = 0u; n < PE_REPORT_MAX_NODES; ++n)
                if (node_have[n] > (size_t)node_quota[n])
                    ++hungry;
            if (hungry == 0u)
                break;
            share = budget / hungry;
            if (share == 0u)
                share = 1u;
            for (size_t n = 0u; n < PE_REPORT_MAX_NODES && budget > 0u; ++n)
            {
                size_t want = node_have[n] - (size_t)node_quota[n];
                size_t give;
                if (node_have[n] <= (size_t)node_quota[n])
                    continue;
                give = want < share ? want : share;
                if (give > budget)
                    give = budget;
                node_quota[n] += (int)give;
                budget -= give;
            }
        }
    }
    /* Two sweeps: rows that carry a strategy first, the untouched ones after.
     *
     * A postflop node in a multi-street solve has hundreds of thousands of
     * infosets and a quota of a few hundred, and most of those infosets were
     * visited once or not at all -- their strategy is still the uniform one
     * regret matching starts from.  Taking the first N by id filled the grid
     * with 50/50 cells that say nothing: at node 7, 165 of 338 rows.  Rows
     * with data are what a reader wants; the rest only fill leftover quota. */
    for (int sweep = 0; sweep < 2 && (querying || report_rows == 0u ||
                                      emitted < report_rows); ++sweep)
    for (size_t id = 0u; id < solver_count &&
                        (querying || report_rows == 0u || emitted < report_rows); ++id)
    {
        uint64_t key = 0u;
        pe_strategy_query_t query;
        pe_strategy_view_t strategy;
        pe_preflop_infodesc_view_t view;
        size_t desc_index;
        pe_preflop_betting_state_t state;
        int row_node;
        if (pe_solver_strategy_key_at(solver, (uint32_t)id, &key) != PE_SOLVER_OK)
            continue;
        if (pe_preflop_allin_infodesc_find(game, key, &desc_index) != 0 ||
            pe_preflop_allin_infodesc_state_at(game, desc_index, &state) != 0)
            continue;
        row_node = state.tree_node_index;
        if (row_node < 0 || row_node >= (int)PE_REPORT_MAX_NODES)
            row_node = (int)PE_REPORT_MAX_NODES - 1;
        if (!querying && report_rows > 0u &&
            node_emitted[row_node] >= (size_t)node_quota[row_node])
            continue;
        /* Filter on the board BEFORE building the view: the view formats the
         * context line and every action label, and a query discards nearly
         * every row it is handed. */
        if (querying &&
            !query_board_matches(state.board, query_mask,
                                 options->board_texture_level))
            continue;
        if (pe_preflop_allin_infodesc_view_at(game, desc_index, &view) != 0)
            continue;
        query.infoset = (uint32_t)id;
        if (pe_solver_strategy(solver, &query, &strategy) != PE_SOLVER_OK)
            continue;
        /* An infoset nobody reached still holds the uniform strategy.  Sweep 0
         * takes everything else, sweep 1 takes these. */
        if (!querying && report_rows > 0u &&
            strategy_has_data(&strategy) != (sweep == 0))
            continue;
        ++node_emitted[row_node];
        printf("%s\t%d\tP%d\t", view.hand, view.tree_node_index,
               view.actor + 1);
        /* The board of THIS sampled deal is appended below as a sixth
         * column.  Without it a per-board filter has nothing to match on:
         * every row of a preflop-rooted run looks board-less even though
         * each was played out on its own runout. */
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
        {
            /* Compact, so the column matches the spelling --board takes and
             * the Studio's board filter can compare them directly. */
            char board_text[32];
            char compact[32];
            size_t out = 0u;
            board_text[0] = '\0';
            if (mask_popcount(state.board) > 0)
                (void)mask_to_string(state.board, board_text, sizeof(board_text));
            for (size_t i = 0u; board_text[i] && out + 1u < sizeof(compact); ++i)
                if (board_text[i] != ' ')
                    compact[out++] = board_text[i];
            compact[out] = '\0';
            printf("\t%s", out ? compact : "-");
        }
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
        if (strcmp(options->game, "holdem") == 0 &&
            strnlen(view.hand, sizeof(view.hand)) >= 4u)
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
    if (querying)
        printf("board_query_rows=%zu\n", emitted);
    if (!querying && report_rows != 0u && emitted >= report_rows)
        printf("... report capped at %zu visible rows (--report-rows); the solve "
               "storage still contains all %zu infosets.\n",
               report_rows, solver_count);
    printf("report_phase=complete rows=%zu\n", emitted);
    fflush(stdout);
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
        "  --interactive               after the report, keep running and answer\n"
        "                              \"query <cards>\" lines on stdin with that board\'s\n"
        "                              hand table; \"quit\" or EOF ends the process.\n"
        "  --query-board CARDS         emit the hand table of every infoset whose board\n"
        "                              matches CARDS, walking the whole solve instead of\n"
        "                              the sampled report. Ignores --report-rows and uses\n"
        "                              the run\'s own board matching (suit isomorphism, or\n"
        "                              the texture id when --board-abstraction is set).\n"
        "  --report-rows N             hand rows printed in the report (default 2000,\n"
        "                              0 = every sampled infoset)\n"
        "  --street NAME               root street: preflop (default), flop, turn or river.\n"
        "                              With a tree, the tree decisions are followed on every\n"
        "                              street the tree declares; other streets roll out.\n"
        "  --board CARDS               fixed board for a flop/turn/river root (e.g. AsKdQc)\n"
        "  --pot BB                    pot at a flop/turn/river root (required there)\n"
        "  --to-act SEAT               seat to act first at a postflop root\n"
        "                              (default: tree header first_to_act, else 0)\n"
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
        "  --max-ram MB                 stop cleanly when storage plus the game\n"
        "                               adapter exceed MB (default: 70%% of RAM,\n"
        "                               0 disables).  A run with no iteration cap\n"
        "                               grows until something stops it.\n"
        , DEFAULT_ITERATIONS);
    fputs(
        "  --desc-limit MB              cap the human-readable description table\n"
        "                               (default: a quarter of --max-ram).  It is\n"
        "                               only used to print report rows, and at 448\n"
        "                               bytes an infoset it was three quarters of a\n"
        "                               long solve's memory.  0 = unbounded.\n"
        "  --verbose                    emit the per-iteration debug counters\n"
"  --seed N                     deterministic RNG seed\n"
        "  --output FILE                write a JSON run report\n"
"  --checkpoint FILE            save a v2 checkpoint (at completion and every --checkpoint-interval\\n"
        "  --resume FILE               load a v2 checkpoint and continue the solve\\n"
        "  --checkpoint-interval N     save a checkpoint every N iterations (0=off\\n"
        "  --help                       show this help\n", stream);
    /* Split out: the single usage literal was over the 4095-char limit C99
     * guarantees, which -Woverlength-strings rejects. */
    fputs(
        "  --board-abstraction LEVEL   merge boards a level cannot tell apart:\n"
        "                              none (default, exact), small, medium, large,\n"
        "                              detailed.\n"
        "                              One sampled board then answers for its whole\n"
        "                              class, at the cost of averaging every board in it.\n"
        "                              small/medium/large ignore board RANKS (2, 3 and 7\n"
        "                              classes for all 22100 flops); detailed keeps them\n"
        "                              (366 flop classes) and is the one to use when the\n"
        "                              board has to be played.\n", stream);
}
/* A sampled run without an iteration cap grows its footprint for as long as
 * it runs, so it needs a budget it did not ask for: without one the kernel
 * ends the run, and everything the solve had is lost.  70% of physical RAM
 * leaves room for the rest of the desktop; --max-ram overrides it and
 * --max-ram 0 turns it off. */
static uint64_t default_ram_budget_bytes(void)
{
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || page_size <= 0)
        return UINT64_C(4) * 1024u * 1024u * 1024u;
    return (uint64_t)pages * (uint64_t)page_size / 10u * 7u;
}

static pe_solver_t *g_solver = NULL;
static volatile sig_atomic_t g_stop_requested = 0;
static pthread_t g_stop_watcher;

/* A stop requested by SIGINT/SIGTERM only sets this flag (async-signal-safe) ;
 * a dedicated watcher thread calls pe_solver_stop() fromits own stack so the
 * solve thread never re-locks the lifecycle mutex from insidea signal handler(:, */
static void i_on_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static void *i_stop_watcher(void *opaque)
{
    (void)opaque;
    /* Poll a stop request; call pe_solver_stop from a thread distinct from
     * the solving thread.  Short sleep keeps the latency around one iteration. */
    while (g_stop_requested == 0)
        usleep(50000);
    if (g_solver)
        pe_solver_stop(g_solver);
    return NULL;
}
static void i_hash_byte(uint64_t *hash, unsigned char byte)
{
    *hash ^= byte;
    *hash *= UINT64_C(0x100000001b3);
}

static void i_hash_str(uint64_t *hash, const char *text)
{
    if (!text)
        return;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        i_hash_byte(hash, *p);
}

/* Board abstraction levels, in the vocabulary board_texture.h already uses.
 * "none" keeps boards exact (the suit isomorphism still applies); the coarser
 * levels merge boards a level cannot tell apart, so a sampled board answers
 * for its whole class.  That is an approximation, hence opt-in. */
static int parse_board_abstraction(const char *text)
{
    if (!text || !*text || strcmp(text, "none") == 0)
        return PE_TEXTURE_FILTER_NONE;
    if (strcmp(text, "small") == 0)   return PE_TEXTURE_FILTER_SMALL;
    if (strcmp(text, "medium") == 0)  return PE_TEXTURE_FILTER_MEDIUM;
    if (strcmp(text, "large") == 0)   return PE_TEXTURE_FILTER_LARGE;
    if (strcmp(text, "detailed") == 0) return PE_TEXTURE_FILTER_DETAILED;
    if (strcmp(text, "perfect") == 0) return PE_TEXTURE_FILTER_PERFECT;
    return -1;
}

/* Root street names for Lane B street trees: preflop keeps the classic
 * blind-posted root; flop/turn/river root the game at that street with
 * the fixed --board, --pot and first actor.  Returns 0..3 or -1. */
static int parse_street_name(const char *text)
{
    if (!text || !*text || strcmp(text, "preflop") == 0)
        return 0;
    if (strcmp(text, "flop") == 0)
        return 1;
    if (strcmp(text, "turn") == 0)
        return 2;
    if (strcmp(text, "river") == 0)
        return 3;
    return -1;
}

static int street_board_cards(int street)
{
    return street == 1 ? 3 : street == 2 ? 4 : street == 3 ? 5 : 0;
}

/* A range spec that means "any hand".  Recognised before parsing so the
 * solver can use its complete-range draw instead of materialising the combo
 * list: full PLO5/PLO6 ranges are 2.6M / 20.4M hands, which is neither
 * storable nor walkable per deal, and the 5- and 6-card parser deliberately
 * refuses percentages it has no ranked table for. */
static int range_is_complete(const char *text)
{
    if (!text || !*text)
        return 1;   /* the drivers default an empty range to 100% */
    while (*text == ' ' || *text == '\t')
        ++text;
    /* "100%" is what this repo's tools and the Studio already emit for a full
     * range; "random" is ProPokerTools' spelling for the same thing.  Nothing
     * else is treated as complete -- an unrecognised spelling must reach the
     * parser and be judged there, not silently become "any hand". */
    return strcmp(text, "100%") == 0 || strcmp(text, "random") == 0;
}

/* Deterministic fingerprint of the solve spot; the checkpoint adapter stores it
 * so it can refuse to resume a checkpoint saved on a different spot. */
static uint64_t spot_hash(const options_t *options, const mpf_tree_def_t *tree)
{
    uint64_t h = UINT64_C(0x50455f5052464c42);
    i_hash_str(&h, options->game);
    i_hash_str(&h, options->tree);
    i_hash_str(&h, options->street);
    i_hash_str(&h, options->board);
    {
        double v = options->target_mbb;
        for (unsigned i = 0; i < sizeof(v); ++i)
            i_hash_byte(&h, ((const unsigned char *)&v)[i]);
    }
    {
        double v = options->have_pot ? options->pot : 0.0;
        for (unsigned i = 0; i < sizeof(v); ++i)
            i_hash_byte(&h, ((const unsigned char *)&v)[i]);
    }
    /* The tree's CONTENT, not just its path.  Editing a tree in place --
     * changing a bet size, rewiring an action -- leaves the path identical,
     * so without this a checkpoint full of regrets for the old betting was
     * accepted for the new one and silently resumed onto a different spot. */
    if (tree && tree->nodes)
    {
        i_hash_byte(&h, (unsigned char)(tree->node_count & 0xFF));
        i_hash_byte(&h, (unsigned char)((tree->node_count >> 8) & 0xFF));
        for (int n = 0; n < tree->node_count; ++n)
        {
            const mpf_tree_node_t *node = &tree->nodes[n];
            i_hash_byte(&h, (unsigned char)node->type);
            i_hash_byte(&h, (unsigned char)node->street);
            i_hash_byte(&h, (unsigned char)(node->acting_player + 1));
            i_hash_byte(&h, (unsigned char)node->use_pot_sizing);
            i_hash_byte(&h, (unsigned char)node->action_count);
            for (int a = 0; a < node->action_count; ++a)
            {
                i_hash_byte(&h, (unsigned char)node->actions[a].type);
                i_hash_byte(&h, (unsigned char)(node->actions[a].size_index + 1));
                i_hash_byte(&h, (unsigned char)(node->actions[a].next_index & 0xFF));
                i_hash_byte(&h, (unsigned char)((node->actions[a].next_index >> 8) & 0xFF));
            }
            for (int b = 0; b < node->bet_size_count; ++b)
            {
                double size = node->bet_sizes[b];
                for (unsigned i = 0; i < sizeof(size); ++i)
                    i_hash_byte(&h, ((const unsigned char *)&size)[i]);
            }
        }
    }
    return h;
}


/* Like parse_u64 but 0 is a meaningful value (--report-rows 0 = no cap). */
static int parse_u64_allow_zero(const char *text, uint64_t *out)
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
    if (errno || end == text || *end != '\0' || !pe_finite_double(value) || value < 0.0)
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
    if (!text || !options || strnlen(text, sizeof(buffer)) >= sizeof(buffer))
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
    options->verbose = 0;
    options->max_ram_bytes = default_ram_budget_bytes();
    options->desc_limit_bytes = 0u;
    options->desc_limit_set = 0;
    options->showdown_samples = DEFAULT_SHOWDOWN_SAMPLES;
    options->stack = DEFAULT_STACK;
    options->report_rows = DEFAULT_REPORT_ROWS;
    options->small_blind = DEFAULT_SMALL_BLIND;
    options->big_blind = DEFAULT_BIG_BLIND;
    options->ante = 0.0;
    options->min_raise = DEFAULT_MIN_RAISE;
    options->br_samples = 256u;
    options->exploitability_interval = 256u;
options->checkpoint_interval =0u;
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
             strcmp(arg, "--max-ram") == 0 ||
             strcmp(arg, "--desc-limit") == 0 ||
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
             strcmp(arg, "--exploitability-interval") == 0 ||
             strcmp(arg, "--checkpoint") == 0 ||
             strcmp(arg, "--resume") == 0 ||
             strcmp(arg, "--street") == 0 ||
             strcmp(arg, "--board") == 0 ||
             strcmp(arg, "--pot") == 0 ||
             strcmp(arg, "--to-act") == 0 ||
             strcmp(arg, "--report-rows") == 0 ||
             strcmp(arg, "--board-abstraction") == 0 ||
             strcmp(arg, "--query-board") == 0 ||
             strcmp(arg, "--checkpoint-interval") == 0) &&
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
        } else if (strcmp(arg, "--verbose") == 0) {
            options->verbose = 1;
            continue;
        } else if (strcmp(arg, "--max-ram") == 0) {
            uint64_t megabytes;
            if (parse_u64(value, &megabytes) != 0)
                return -1;
            options->max_ram_bytes = megabytes * 1024u * 1024u;
        } else if (strcmp(arg, "--desc-limit") == 0) {
            uint64_t megabytes;
            if (parse_u64(value, &megabytes) != 0)
                return -1;
            options->desc_limit_bytes = megabytes * 1024u * 1024u;
            options->desc_limit_set = 1;
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
} else if (strcmp(arg, "--checkpoint") == 0) {
            options->checkpoint_path = value;
        } else if (strcmp(arg, "--resume") == 0) {
            options->resume_path = value;
        } else if (strcmp(arg, "--checkpoint-interval") == 0) {
            if (parse_u64(value,&options->checkpoint_interval) != 0)
                return -1;
        } else if (strcmp(arg, "--seed") == 0) {
            if (parse_u64(value, &options->seed) != 0) return -1;
        } else if (strcmp(arg, "--interactive") == 0) {
            options->interactive = 1;
            continue;
        } else if (strcmp(arg, "--query-board") == 0) {
            options->query_board = value;
        } else if (strcmp(arg, "--board-abstraction") == 0) {
            options->board_abstraction = value;
        } else if (strcmp(arg, "--report-rows") == 0) {
            uint64_t rows;
            if (parse_u64_allow_zero(value, &rows) != 0) return -1;
            options->report_rows = (size_t)rows;
        } else if (strcmp(arg, "--street") == 0) {
            options->street = value;
        } else if (strcmp(arg, "--board") == 0) {
            options->board = value;
        } else if (strcmp(arg, "--pot") == 0) {
            if (parse_positive_double(value, &options->pot) != 0) return -1;
            options->have_pot = 1;
        } else if (strcmp(arg, "--to-act") == 0) {
            uint64_t seat;
            if (parse_u64(value, &seat) != 0) return -1;
            options->to_act = (int)seat;
            options->have_to_act = 1;
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
                         size_t infosets, simd_capability_t detected_simd)
{
    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        return;
    }
    fprintf(file,
        "{\"schema\":\"pe-preflop-solve/v1\","
        "\"game\":\"%s\",\"players\":%d,"
        "\"algorithm\":\"%s\",\"backend\":\"%s\","
        "\"backend_validated\":true,\"precision\":\"%s\","
        "\"simd_detected\":\"%s\",\"simd_cfr_integrated\":false,"
        "\"iterations\":%" PRIu64 ",\"showdown_samples\":%d,"
        "\"stack\":%.17g,\"small_blind\":%.17g,\"big_blind\":%.17g,\"ante\":%.17g,"
        "\"allow_nonallin_call\":%s,\"postflop_streets\":%s,\"br_samples\":%" PRIu64 ",\"infosets\":%zu,"
        "\"progress\":{\"iteration\":%" PRIu64 ",\"complete\":%s},"
        "\"metrics\":{\"guarantee\":\"%s\",\"exploitability_raw\":%.17g,"
        "\"exploitability_mbb_per_game\":%.17g,\"big_blind\":%.17g}}\n",
        options->game, options->players, pe_preset_name(options->algorithm),
        pe_compute_kind_name(options->backend),
        pe_precision_name(options->precision), pe_runtime_simd_name(detected_simd),
        options->iterations,
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
    pe_telemetry_ops_t telemetry_sink;
    pe_solver_t *solver = NULL;
    pe_solver_status_t status;
    pe_progress_t progress = {0};
    pe_metrics_t metrics = {0};
    StdDeck_CardMask dead;
    pe_preflop_variant_t variant;
    mpf_tree_def_t *tree = NULL;
    pe_monker_tree_header_t tree_header;
    simd_capability_t detected_simd = SIMD_NONE;

    /* MSVC's UCRT rejects a zero-sized line buffer.  The CLI flushes its
     * progress output explicitly, so unbuffered output is portable here. */
    setvbuf(stdout, NULL, _IONBF, 0);
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
        printf("simd_solver_path=not-integrated (SIMD is used by equity/terminal adapters; "
               "CFR regret traversal remains scalar)\n");
        printf("lane_b_algorithms=external-mccfr,external-dcfr,outcome-mccfr,external-ecfr\n");
        printf("lane_a_algorithms=cfr,cfr+,dcfr,mccfr (full-tree adapter pending)\n");
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
        detected_simd = runtime.simd;
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
    int root_street = parse_street_name(options.street);
    mask_t board_mask = 0u;
    if (root_street < 0)
    {
        fprintf(stderr, "unknown --street '%s' (want preflop, flop, turn or river)\n",
                options.street ? options.street : "(null)");
        goto fail;
    }
    if (root_street == 0 && options.board)
    {
        fprintf(stderr, "--board needs a flop, turn or river --street\n");
        goto fail;
    }
    if (root_street != 0)
    {
        int need;
        if (!options.board)
        {
            fprintf(stderr, "--street %s needs --board CARDS\n",
                    options.street);
            goto fail;
        }
        board_mask = string_to_mask(options.board);
        need = street_board_cards(root_street);
        if (mask_popcount(board_mask) != (uint32_t)need)
        {
            fprintf(stderr, "--board must hold exactly %d cards for %s\n",
                    need, options.street);
            goto fail;
        }
        if (!options.have_pot)
        {
            fprintf(stderr, "--street %s needs --pot BB\n", options.street);
            goto fail;
        }
    }
    if (options.tree)
    {
        if (tree_path_is_json(options.tree))
        {
            mpf_tree_error_t json_error = {0};
            tree = mpf_tree_load_json_file(options.tree, &json_error);
            if (!tree || !infer_json_tree_header(tree, &tree_header))
            {
                fprintf(stderr, "could not load tree JSON %s: %s\n",
                        options.tree,
                        json_error.message[0] ? json_error.message
                                              : "invalid JSON tree header");
                goto fail;
            }
        }
        else
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
        }
        if (tree_header.street != (uint32_t)root_street ||
            tree_header.player_count != (uint32_t)options.players)
        {
            fprintf(stderr,
                    "tree street must match --street %s and contain %d players (got street=%u players=%u)\n",
                    options.street ? options.street : "preflop",
                    options.players, tree_header.street, tree_header.player_count);
            goto fail;
        }
        /* A tree may span several streets: when the round-closing action of
         * one street wires into a player node on the next, the board is dealt
         * and the tree carries on there.  Later-street nodes are therefore
         * fine.  Nodes on a street EARLIER than the root are the unreachable
         * ones -- play never walks backwards. */
        {
            uint32_t unreachable_nodes = 0u;
            char census[160];
            size_t census_used = 0u;
            uint32_t street_nodes[5] = {0u, 0u, 0u, 0u, 0u};
            static const char *names[5] = {"PRE", "FLOP", "TURN", "RIVER", "SHOWDOWN"};
            if (tree && tree->nodes && tree->node_count > 0)
            {
                for (int ni = 0; ni < tree->node_count; ++ni)
                {
                    if (tree->nodes[ni].type == MPF_TREE_NODE_PLAYER &&
                        (int)tree->nodes[ni].street < root_street)
                        ++unreachable_nodes;
                    if (tree->nodes[ni].type == MPF_TREE_NODE_PLAYER &&
                        (int)tree->nodes[ni].street >= 0 &&
                        (int)tree->nodes[ni].street < 5)
                        ++street_nodes[(int)tree->nodes[ni].street];
                }
            }
            census[0] = '\0';
            for (int street = 0; street < 5; ++street)
            {
                if (street_nodes[street] == 0u)
                    continue;
                census_used += (size_t)snprintf(census + census_used,
                                                sizeof(census) - census_used,
                                                "%s%s=%u", census_used ? " " : "",
                                                names[street], street_nodes[street]);
                if (census_used + 1u >= sizeof(census))
                    break;
            }
            printf("tree_streets=%s\n",
                   census[0] ? census : "none");
            {
                int streets_with_nodes = 0;
                for (int street = 0; street < 5; ++street)
                    streets_with_nodes += street_nodes[street] > 0u;
                /* An exact board key on a multi-street tree has no bound:
                 * every runout the solve has not seen yet becomes a new
                 * infoset, for as long as the run lasts.  Measured on
                 * nlhe_hu_full: "none" grows ~25 infosets per iteration and
                 * never settles, while "large" reaches 75 801 infosets by
                 * 500k iterations and 77 210 by 2M -- 1.9% more for four
                 * times the work, i.e. the whole space, at constant memory.
                 * Say so before the run rather than when it runs out. */
                if (streets_with_nodes > 1 &&
                    parse_board_abstraction(options.board_abstraction) == 0)
                    printf("warning=unbounded_state_space streets=%d"
                           " abstraction=none  A multi-street tree with an"
                           " exact board key adds infosets for as long as it"
                           " runs and can only end on the memory budget."
                           "  --board-abstraction large bounds it (~77k"
                           " infosets on this tree, reached by 500k"
                           " iterations); detailed keeps ranks and bounds it"
                           " far higher.\n", streets_with_nodes);
            }
            fflush(stdout);
            if (unreachable_nodes > 0u)
            {
                fprintf(stderr,
                        "warning: tree holds %u decision node(s) on a street "
                        "before the %s root; play never walks backwards, so "
                        "those are unreachable.\n",
                        unreachable_nodes,
                        options.street ? options.street : "preflop");
            }
        }
    }
    StdDeck_CardMask_RESET(dead);
    /* All-or-nothing: the complete-range draw has no closed form for a deal
     * that mixes "any hand" with an explicit list, so one restricted range
     * puts every player back on the enumerated path. */
    int complete_ranges = 1;
    for (int player = 0; player < options.players; ++player)
        if (!range_is_complete(options.range[player]))
            complete_ranges = 0;
    for (int player = 0; !complete_ranges && player < options.players; ++player) {
        enum_game_t range_game = variant == PE_PREFLOP_HOLDEM ? game_holdem
            : variant == PE_PREFLOP_PLO4 ? game_omaha
            : variant == PE_PREFLOP_PLO5 ? game_omaha5 : game_omaha6;
        if (pe_solver_range_parse(range_game, options.range[player], dead,
                                  &ranges[player]) != PE_SOLVER_OK ||
            !ranges[player]) {
            fprintf(stderr, "invalid %s range%d: %s\n", options.game, player,
                    options.range[player]);
            if (variant == PE_PREFLOP_PLO5 || variant == PE_PREFLOP_PLO6)
            {
                int n = variant == PE_PREFLOP_PLO5 ? 5 : 6;
                fprintf(stderr,
                        "note: %s ranges accept\n"
                        "  - a complete hand: %s\n"
                        "  - a ProPokerTools rank pattern, 'x' for any rank: %s\n"
                        "  - the full range: 100%%\n"
                        "A pattern is refused when it expands past %u combos "
                        "(AAxxxx alone is 1.4M); narrow it, e.g. AAKxxx "
                        "instead of AAxxxx.\n"
                        "Suit suffixes (ds/ss/ts/qs/r) are 4-card notation and "
                        "are not accepted for %d-card hands: their suit-count "
                        "shapes have no agreed %d-card meaning.\n",
                        options.game,
                        n == 5 ? "AsKsQd3c9h" : "AsKsQd3c9h8d",
                        n == 5 ? "AAxxx, AKQxx, AAKKx" : "AKQJxx, AAKKxx",
                        (unsigned)500000, n, n);
            }
            goto fail;
        }
    }

    memset(&rules, 0, sizeof(rules));
    rules.variant = variant;
    rules.player_count = options.players;
    for (int player = 0; player < options.players; ++player)
        rules.stacks[player] = options.stack;
    /* Postflop roots use remaining stacks: prefer the tree header's when
     * present, else the --stack value. */
    if (root_street != 0 && tree)
    {
        for (int player = 0; player < options.players; ++player)
            if (tree_header.stacks[player] > 0.0)
                rules.stacks[player] = tree_header.stacks[player];
    }
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
    rules.complete_ranges = complete_ranges;
    {
        int level = parse_board_abstraction(options.board_abstraction);
        if (level < 0)
        {
            fprintf(stderr,
                    "unknown --board-abstraction '%s' (want none, small, "
                    "medium, large, detailed or perfect)\n",
                    options.board_abstraction);
            goto fail;
        }
        rules.board_texture_level = level;
        options.board_texture_level = level;
    }
    rules.root_street = root_street;
    rules.root_board = (uint64_t)board_mask;
    rules.root_pot = options.have_pot ? options.pot : 0.0;
    if (options.have_to_act)
    {
        if (options.to_act >= options.players)
        {
            fprintf(stderr, "--to-act seat %d out of range for %d players\n",
                    options.to_act, options.players);
            goto fail;
        }
        rules.root_to_act = options.to_act;
    }
    else if (tree && tree_header.first_to_act >= 0)
        rules.root_to_act = tree_header.first_to_act;
    else
        rules.root_to_act = 0;
    for (size_t i = 0u; i < sizeof(rules.raise_sizes) / sizeof(rules.raise_sizes[0]); ++i)
        rules.raise_sizes[i] = options.raise_sizes[i];
    game = pe_preflop_allin_game_create(&rules, ranges);
    if (game)
    {
        /* Give the description table a quarter of the budget by default.  It
         * serves the report only, and a report prints a few thousand rows;
         * the solve's own regrets and average strategy should have the rest.
         * Without the bound the table was 75% of a long solve's memory and
         * the run hit the budget paying to describe rows nobody reads. */
        uint64_t limit = options.desc_limit_set
            ? options.desc_limit_bytes
            : options.max_ram_bytes / 4u;
        pe_preflop_allin_game_set_desc_limit(game, (size_t)limit);
    }
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
    config.execution.max_ram_bytes = options.max_ram_bytes;
    config.target_exploitability_mbb = options.target_mbb;
    config.exploitability_interval = options.exploitability_interval;
    config.br_samples = (uint32_t)options.br_samples;
    config.seed = options.seed;
    deps = pe_solver_deps_default();
    deps.external_game = pe_preflop_allin_external(game);
    /* The stdout sink accepts every level, and the sampled loop emits a DEBUG
     * counter line per iteration: a million-iteration run wrote a million
     * lines nobody reads, through a pipe when the Studio is the reader.  Cap
     * the sink at INFO unless --verbose asks for the rest. */
    telemetry_sink = *pe_telemetry_stdout();
    if (!options.verbose)
        telemetry_sink.max_level = PE_LOG_INFO;
    deps.telemetry = &telemetry_sink;
    deps.persist = (const pe_persist_ops_t *)pe_persist_checkpoint_ops();
    solver = pe_solver_create(&config, &deps);
        status = solver ? PE_SOLVER_OK : PE_SOLVER_ERR_OUT_OF_MEMORY;
    g_solver = solver;
    if (options.resume_path && solver) {
        pe_persist_source_t src;
        memset(&src, 0, sizeof(src));
        src.path = options.resume_path;
        src.game_hash = spot_hash(&options, tree);
        src.tree_hash = src.game_hash;
        if (pe_solver_load(solver, &src) != PE_SOLVER_OK) {
            fprintf(stderr, "could not resume checkpoint %s\n", options.resume_path);
            status = PE_SOLVER_ERR_EXECUTION;
        } else {
            pe_progress_t p;
            pe_solver_progress(solver, &p);
            printf("resumed_checkpoint=1 path=%s continuation_iteration=%" PRIu64 "\n", options.resume_path, p.iteration);
        }
    }
    int interrupted = 0;
    if (status == PE_SOLVER_OK) {
        signal(SIGINT, i_on_signal);
        signal(SIGTERM, i_on_signal);
        g_stop_requested = 0;
        if (pthread_create(&g_stop_watcher,NULL,i_stop_watcher,NULL) !=0){
            g_stop_watcher = (pthread_t)0;
        }
        if (g_stop_requested && g_solver)
            pe_solver_stop(g_solver);
        status = pe_solver_run(solver);
        /* Capture the interrupt BEFORE the flag is reused below to stop the
         * watcher thread.  An interrupted solve must not go on to serve
         * queries: the user asked for the run to end. */
        interrupted = g_stop_requested != 0;
        g_stop_requested = 1;
        if (g_stop_watcher)
            pthread_join(g_stop_watcher,NULL);
        if (options.checkpoint_path && solver) {
            pe_persist_target_t t;
            memset(&t, 0, sizeof(t));
            t.path = options.checkpoint_path;
            t.game_hash = spot_hash(&options, tree);
            t.tree_hash = t.game_hash;
            if (pe_solver_save(solver, &t) == PE_SOLVER_OK) {
                pe_progress_t p;
                pe_solver_progress(solver, &p);
                printf("checkpoint_saved=1 path=%s iteration=%" PRIu64 "\n", options.checkpoint_path, p.iteration);
            } else {
                fprintf(stderr, "checkpoint save failed\n");
            }
        }
        g_solver = NULL;
    }
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
               "backend=%s backend_validated=1 cpu_threads=%d precision=%s simd_detected=%s "
               "simd_cfr=not-integrated dcfr_alpha=%.6g dcfr_beta=%.6g "
               "dcfr_gamma=%.6g lambda=%.6g game=%s players=%d postflop=%d tree=%s street=%s board=%s board_abstraction=%s\n",
               config.algorithm.preset == PE_PRESET_CUSTOM
                   ? "custom" : pe_preset_name(options.algorithm),
               pe_traversal_name(config.algorithm.traversal),
               pe_regret_name(config.algorithm.regret),
               pe_policy_name(config.algorithm.policy),
               pe_compute_kind_name(options.backend),
               options.cpu_threads,
               pe_precision_name(options.precision),
               pe_runtime_simd_name(detected_simd),
               config.algorithm.dcfr_alpha, config.algorithm.dcfr_beta,
               config.algorithm.dcfr_gamma, config.algorithm.exponential_lambda,
               options.game, options.players, options.postflop_streets,
               tree ? options.tree : "none",
               options.street ? options.street : "preflop",
               options.board ? options.board : "none",
               options.board_abstraction ? options.board_abstraction : "none");
        printf("iterations=%" PRIu64 " complete=%d infosets=%zu\n",
               progress.iteration, progress.complete, infosets);
        double desc_bytes = (double)(uint64_t)
            pe_preflop_allin_infodesc_bytes(game);
        /* The solver names the cause; "stopped" used to cover a caller stop,
         * a signal and a budget alike, which is no use to anyone watching a
         * run end on its own.  interrupted distinguishes a signal we received
         * from a stop the solver decided on. */
        printf("solver_phase=complete stop_reason=%s report=starting\n",
               progress.stop_cause == PE_STOP_REQUESTED && interrupted
                   ? "interrupted"
                   : pe_stop_cause_name(progress.stop_cause));
        printf("stop_detail cause=%s interrupted=%d iteration=%" PRIu64
               " held_mb=%.1f budget_mb=%.1f descriptions_mb=%.1f"
               " descriptions_capped=%d\n",
               pe_stop_cause_name(progress.stop_cause), interrupted,
               progress.iteration,
               (double)progress.memory_bytes / (1024.0 * 1024.0),
               (double)options.max_ram_bytes / (1024.0 * 1024.0),
               desc_bytes / (1024.0 * 1024.0),
               pe_preflop_allin_infodesc_limited(game));
        fflush(stdout);
        printf("guarantee=%s exploitability_raw=%.6f exploitability_mbb=%.6f br_samples=%" PRIu64 "\n",
               guarantee_name(metrics.guarantee), metrics.exploitability_raw,
               metrics.exploitability_mbb_per_game, options.br_samples);
        print_strategy_report(&options, game, solver, tree);
        /* Serve after an interrupt too.  Stopping a run is the normal way to
         * say "that is enough, let me look at it" -- and with an iteration
         * cap disabled it is the ONLY way a run ever ends, so refusing to
         * serve then made board queries unreachable.  This was safe to
         * refuse only while an orphaned solver could outlive its shell; that
         * is fixed at the launch site, and "quit" ends the process cleanly. */
        if (options.interactive)
        {
            /* The game and solver stay in memory, so every query sees the
             * complete set of infosets the solve visited -- descriptions
             * included -- which a resumed process cannot reconstruct. */
            char line[256];
            printf("interactive=1 ready interrupted=%d\n", interrupted);
            fflush(stdout);
            while (fgets(line, sizeof(line), stdin))
            {
                size_t len = strlen(line);
                while (len > 0u && (line[len - 1u] == '\n' || line[len - 1u] == '\r'))
                    line[--len] = '\0';
                if (strcmp(line, "quit") == 0)
                    break;
                if (strncmp(line, "query ", 6u) == 0 && line[6])
                {
                    options_t query_options = options;
                    query_options.query_board = line + 6;
                    print_strategy_report(&query_options, game, solver, tree);
                }
                printf("query_done\n");
                fflush(stdout);
            }
        }
        if (options.output)
            write_report(options.output, &options, &metrics, &progress, infosets,
                         detected_simd);
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
