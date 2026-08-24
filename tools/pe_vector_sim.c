/*
 * pe_vector_sim - exact river terminal for the new correlated-range lane.
 *
 * This command is intentionally small and strict. It is the executable seam
 * used while the full tree/.mkr orchestration is moved from the legacy CFR
 * runner to the vector domain.
 */

#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/range.h>
#include <poker_eval/solver/pe_betting_state.h>
#include <poker_eval/solver/pe_holdem_deals.h>
#include <poker_eval/solver/pe_holdem_river.h>
#include <poker_eval/solver/pe_monker.h>
#include <poker_eval/solver/pe_monker_strategy.h>
#include <poker_eval/solver/pe_monker_omaha_tree.h>
#include <poker_eval/solver/pe_monker_tree_vector.h>
#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_omaha_river.h>
#include <poker_eval/solver/pe_traversal.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_VECTOR_SIM_MAX_PLAYERS 8u

typedef struct
{
    size_t terminal_calls;
    double reach_mass;
    size_t visited_nodes;
    size_t terminal_nodes;
} monker_walk_stats_t;

typedef struct
{
    const char *game_name;
    enum_game_t game;
    uint8_t hole_cards;
} game_spec_t;

typedef struct
{
    const char *board_text;
    const char *ranges[PE_VECTOR_SIM_MAX_PLAYERS];
    const char *tree_path;
    const char *mkr_path;
    const char *strategy_name;
    const pe_range_t *source_ranges[PE_VECTOR_SIM_MAX_PLAYERS];
    uint8_t player_count;
    double invested;
    double pot;
    int pot_explicit;
} cli_options_t;

typedef struct
{
    mpf_tree_def_t *tree;
    pe_monker_tree_header_t header;
    pe_monker_range_set_t ranges;
    pe_monker_mkr_t archive;
    pe_monker_mkr_metadata_t metadata;
    pe_monker_mkr_strategy_t strategy;
    pe_monker_classes_t *classes;
    pe_monker_strategy_t *strategy_view;
    pe_monker_tree_vector_t tree_vector;
    pe_monker_strategy_game_t strategy_vector;
    monker_walk_stats_t walk_stats;
    int tree_loaded;
    int ranges_loaded;
    int archive_loaded;
    int metadata_loaded;
    int strategy_loaded;
    int classes_loaded;
    int strategy_view_loaded;
    int tree_vector_loaded;
    int strategy_vector_loaded;
    int tree_walk_loaded;
    int tree_ev_loaded;
    double tree_ev[PE_VECTOR_SIM_MAX_PLAYERS];
    double tree_path_weight;
    size_t tree_deal_count;
    double tree_weight_sum;
} monker_cli_t;

static const game_spec_t GAME_SPECS[] = {
    {"holdem", game_holdem, 2u},
    {"plo4", game_omaha, 4u},
    {"plo5", game_omaha5, 5u},
    {"plo6", game_omaha6, 6u}};

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s --game holdem|plo4|plo5|plo6 --board <cards> "
            "--range0 <expr> --range1 <expr> [options]\n"
            "\n"
            "Options:\n"
            "  --players <n>       Number of players (2..8, default 2)\n"
            "  --invested <x>      Contribution per player (default 1)\n"
            "  --pot <x>           Total pot (default players*invested)\n"
            "  --range<N> <expr>   Range for player N, N=0..7\n"
            "                      Omit all ranges to use ranges in --tree\n"
            "  --tree <path>       Monker .tree (loads topology and embedded ranges)\n"
            "  --mkr <path>        Monker .mkr (loads stored strategy metadata)\n"
            "  --strategy <name>   Strategy entry in .mkr (default storedstrategy0)\n"
            "  --help              Show this message\n",
            program);
}

static int parse_double(const char *text, double *out)
{
    char *end;
    double value;
    if (!text || !out)
        return -1;
    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(value) ||
        value < 0.0)
        return -1;
    *out = value;
    return 0;
}

static int parse_uint8(const char *text, uint8_t *out)
{
    char *end;
    unsigned long value;
    if (!text || !out)
        return -1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > 8u)
        return -1;
    *out = (uint8_t)value;
    return 0;
}

static int find_game(const char *name, game_spec_t *out)
{
    size_t index;
    if (!name || !out)
        return -1;
    for (index = 0u; index < sizeof(GAME_SPECS) / sizeof(GAME_SPECS[0]);
         ++index)
    {
        if (strcmp(name, GAME_SPECS[index].game_name) == 0)
        {
            *out = GAME_SPECS[index];
            return 0;
        }
    }
    return -1;
}

static int parse_args(int argc, char **argv, cli_options_t *out,
                      game_spec_t *game)
{
    int index;
    const char *game_name = NULL;
    memset(out, 0, sizeof(*out));
    out->player_count = 2u;
    out->invested = 1.0;
    for (index = 1; index < argc; ++index)
    {
        const char *arg = argv[index];
        const char *value = NULL;
        if (strcmp(arg, "--help") == 0)
        {
            usage(argv[0]);
            return 1;
        }
        if (strcmp(arg, "--game") == 0 || strcmp(arg, "--board") == 0 ||
            strcmp(arg, "--players") == 0 ||
            strcmp(arg, "--invested") == 0 || strcmp(arg, "--pot") == 0 ||
            strcmp(arg, "--tree") == 0 || strcmp(arg, "--mkr") == 0 ||
            strcmp(arg, "--strategy") == 0)
        {
            if (++index >= argc)
                return -1;
            value = argv[index];
            if (strcmp(arg, "--game") == 0)
                game_name = value;
            else if (strcmp(arg, "--board") == 0)
                out->board_text = value;
            else if (strcmp(arg, "--players") == 0 &&
                     parse_uint8(value, &out->player_count) != 0)
                return -1;
            else if (strcmp(arg, "--invested") == 0 &&
                     parse_double(value, &out->invested) != 0)
                return -1;
            else if (strcmp(arg, "--pot") == 0)
            {
                if (parse_double(value, &out->pot) != 0)
                    return -1;
                out->pot_explicit = 1;
            }
            else if (strcmp(arg, "--tree") == 0)
                out->tree_path = value;
            else if (strcmp(arg, "--mkr") == 0)
                out->mkr_path = value;
            else if (strcmp(arg, "--strategy") == 0)
                out->strategy_name = value;
            continue;
        }
        if (strncmp(arg, "--range", 7u) == 0 && arg[7] >= '0' &&
            arg[7] <= '7' && arg[8] == '\0')
        {
            if (++index >= argc)
                return -1;
            out->ranges[(unsigned)(arg[7] - '0')] = argv[index];
            continue;
        }
        return -1;
    }
    if (!game_name || find_game(game_name, game) != 0 || !out->board_text ||
        out->player_count < 2u || out->player_count > PE_VECTOR_SIM_MAX_PLAYERS)
        return -1;
    if (!out->pot_explicit)
        out->pot = out->invested * (double)out->player_count;
    {
        int range_count = 0;
        for (index = 0; index < (int)out->player_count; ++index)
            if (out->ranges[index])
                ++range_count;
        if (range_count != 0 && range_count != (int)out->player_count)
            return -1;
        if (range_count == 0 && !out->tree_path)
            return -1;
    }
    return 0;
}

static mask_t std_to_modern(StdDeck_CardMask source)
{
    mask_t result = MASK_EMPTY;
    int card;
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (StdDeck_CardMask_CARD_IS_SET(source, card))
            result = mask_set(result, card);
    return result;
}

static StdDeck_CardMask modern_to_std(mask_t source)
{
    StdDeck_CardMask result;
    int card;
    StdDeck_CardMask_RESET(result);
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(source, card))
            StdDeck_CardMask_SET(result, card);
    return result;
}

static int parse_exact_omaha_combo(const char *text, uint8_t hole_cards,
                                   pe_omaha_combo_t **out_combo)
{
    char compact[64];
    char spaced[64];
    const char *weight_text;
    size_t length;
    size_t index;
    double weight = 1.0;
    mask_t cards;
    pe_omaha_combo_t *combo;
    if (!text || !out_combo || hole_cards < 5u || hole_cards > 6u)
        return -1;
    *out_combo = NULL;
    length = strlen(text);
    if (length >= sizeof(compact))
        return -1;
    memcpy(compact, text, length + 1u);
    weight_text = strchr(compact, ':');
    if (weight_text)
    {
        char *end;
        errno = 0;
        weight = strtod(weight_text + 1, &end);
        if (errno != 0 || end == weight_text + 1 || *end != '\0' ||
            !isfinite(weight) || weight < 0.0)
            return -1;
        compact[(size_t)(weight_text - compact)] = '\0';
    }
    length = strlen(compact);
    if (length != (size_t)hole_cards * 2u)
        return -1;
    for (index = 0u; index < (size_t)hole_cards; ++index)
    {
        spaced[index * 3u] = compact[index * 2u];
        spaced[index * 3u + 1u] = compact[index * 2u + 1u];
        if (index + 1u < (size_t)hole_cards)
            spaced[index * 3u + 2u] = ' ';
    }
    spaced[hole_cards * 3u - 1u] = '\0';
    cards = string_to_mask(spaced);
    if (mask_popcount(cards) != hole_cards)
        return -1;
    combo = (pe_omaha_combo_t *)calloc(1u, sizeof(*combo));
    if (!combo)
        return -1;
    combo->cards = cards;
    combo->weight = weight;
    *out_combo = combo;
    return 0;
}

static void monker_range_to_internal(pe_range_t *range)
{
    static const int suit_map[4] = {
        MODERN_SUIT_SPADES, MODERN_SUIT_HEARTS, MODERN_SUIT_CLUBS,
        MODERN_SUIT_DIAMONDS};
    size_t combo;

    if (!range)
        return;
    for (combo = 0u; combo < range->count; ++combo)
    {
        StdDeck_CardMask source = range->combos[combo].hand;
        StdDeck_CardMask converted;
        int card;
        StdDeck_CardMask_RESET(converted);
        for (card = 0; card < MODERN_DECK_SIZE; ++card)
        {
            if (StdDeck_CardMask_CARD_IS_SET(source, card))
            {
                int wire_suit = card / 13;
                int rank = card % 13;
                StdDeck_CardMask_SET(
                    converted, MODERN_MAKE_CARD(rank, suit_map[wire_suit]));
            }
        }
        range->combos[combo].hand = converted;
    }
}

static void monker_cli_free(monker_cli_t *state)
{
    if (!state)
        return;
    if (state->strategy_vector_loaded)
        memset(&state->strategy_vector, 0, sizeof(state->strategy_vector));
    if (state->tree_vector_loaded)
        pe_monker_tree_vector_destroy(&state->tree_vector);
    if (state->strategy_view_loaded)
        pe_monker_strategy_close(state->strategy_view);
    if (state->classes_loaded)
        pe_monker_classes_destroy(state->classes);
    if (state->strategy_loaded)
        pe_monker_mkr_strategy_free(&state->strategy);
    if (state->metadata_loaded)
        pe_monker_mkr_metadata_free(&state->metadata);
    if (state->archive_loaded)
        pe_monker_mkr_free(&state->archive);
    if (state->ranges_loaded)
        pe_monker_range_set_free(&state->ranges);
    if (state->tree_loaded)
        mpf_tree_free(state->tree);
    memset(state, 0, sizeof(*state));
}

static int monker_cli_decode_combo(const void *state, uint16_t combo,
                                   int *out_node, int out_cards[4],
                                   void *user)
{
    const pe_monker_tree_state_t *tree_state =
        (const pe_monker_tree_state_t *)state;
    const pe_monker_classes_t *classes =
        (const pe_monker_classes_t *)user;
    if (!tree_state || !out_node || !out_cards ||
        pe_monker_class_representative(classes, combo, out_cards) !=
            PE_MONKER_OK)
        return -1;
    *out_node = tree_state->node_index;
    return 0;
}

static int monker_cli_terminal_values(
    int node_index, const pe_reach_vec_t *reach, pe_value_vec_t *out_values,
    uint8_t player_count, void *user)
{
    monker_walk_stats_t *stats = (monker_walk_stats_t *)user;
    size_t combo;
    uint8_t player;
    (void)node_index;
    if (!stats || !reach || !out_values || player_count == 0u)
        return -1;
    stats->terminal_calls++;
    for (combo = 0u; combo < out_values[0].n; ++combo)
    {
        stats->reach_mass += reach[0].v[combo] /
                             (double)out_values[0].n;
        for (player = 0u; player < player_count; ++player)
        {
            out_values[player].v[combo] = 0.0;
        }
    }
    return 0;
}

static int monker_cli_walk(monker_cli_t *state, uint8_t player_count)
{
    pe_traversal_ctx_t traversal = {0};
    pe_update_batch_t batch = {0};
    const pe_traversal_ops_t *ops = pe_traversal_full_vector_ops();
    int status = -1;

    if (!state || !state->strategy_vector_loaded || !ops)
        return -1;
    if (pe_traversal_ctx_init(&traversal, &state->strategy_vector.game) != 0)
        goto done;
    if (ops->begin_iteration(&traversal, 0u) != 0 ||
        ops->run_iteration(&traversal, &batch) != 0 ||
        ops->end_iteration(&traversal, 0u) != 0)
        goto done;
    state->walk_stats.visited_nodes = traversal.visited_nodes;
    state->walk_stats.terminal_nodes = traversal.terminal_nodes;
    state->tree_walk_loaded = 1;
    status = 0;
done:
    pe_update_batch_destroy(&batch);
    pe_traversal_ctx_destroy(&traversal);
    return status;
}

static int monker_cli_load(const cli_options_t *options,
                           const game_spec_t *game, monker_cli_t *out)
{
    pe_monker_status_t tree_status;
    pe_monker_mkr_status_t mkr_status;
    pe_monker_combo_layout_t layout;
    int use_tree_ranges;
    uint8_t player;

    if (!options || !game || !out)
        return -1;
    memset(out, 0, sizeof(*out));
    use_tree_ranges = options->ranges[0] == NULL;
    if (!options->tree_path && !options->mkr_path)
        return 0;

    if (options->tree_path)
    {
        tree_status = pe_monker_tree_read_header(options->tree_path,
                                                 &out->header);
        if (tree_status != PE_MONKER_OK)
        {
            fprintf(stderr, "Monker .tree header error: %s\n",
                    pe_monker_status_string(tree_status));
            goto fail;
        }
        if (out->header.player_count != options->player_count)
        {
            fprintf(stderr,
                    "Monker .tree has %u players, CLI requested %u\n",
                    out->header.player_count, options->player_count);
            goto fail;
        }
        tree_status = pe_monker_tree_load(options->tree_path, &out->tree);
        if (tree_status != PE_MONKER_OK || !out->tree)
        {
            fprintf(stderr, "Monker .tree load error: %s\n",
                    pe_monker_status_string(tree_status));
            goto fail;
        }
        out->tree_loaded = 1;

        if (use_tree_ranges)
        {
            tree_status = pe_monker_tree_read_ranges(options->tree_path,
                                                      &out->ranges);
            if (tree_status != PE_MONKER_OK)
            {
                fprintf(stderr, "Monker .tree ranges error: %s\n",
                        pe_monker_status_string(tree_status));
                goto fail;
            }
            out->ranges_loaded = 1;
            if (out->ranges.player_count != options->player_count)
            {
                fprintf(stderr,
                        "Monker .tree ranges have %u players, expected %u\n",
                        out->ranges.player_count, options->player_count);
                goto fail;
            }
            if (pe_monker_combo_layout_from_count(out->ranges.combo_count,
                                                  &layout) != PE_MONKER_OK ||
                layout.game != game->game)
            {
                fprintf(stderr,
                        "Monker .tree range layout (%u combos) does not "
                        "match --game %s\n",
                        out->ranges.combo_count, game->game_name);
                goto fail;
            }
            for (player = 0u; player < out->ranges.player_count; ++player)
                monker_range_to_internal(out->ranges.players[player]);
        }
    }

    if (options->mkr_path)
    {
        const char *strategy_name = options->strategy_name
                                        ? options->strategy_name
                                        : "storedstrategy0";
        mkr_status = pe_monker_mkr_read(options->mkr_path, &out->archive);
        if (mkr_status != PE_MONKER_MKR_OK)
        {
            fprintf(stderr, "Monker .mkr load error: %s\n",
                    pe_monker_mkr_status_string(mkr_status));
            goto fail;
        }
        out->archive_loaded = 1;
        if (pe_monker_mkr_read_metadata(&out->archive, &out->metadata) ==
            PE_MONKER_MKR_OK)
            out->metadata_loaded = 1;
        mkr_status = pe_monker_mkr_read_strategy(
            &out->archive, strategy_name, &out->strategy);
        if (mkr_status != PE_MONKER_MKR_OK)
        {
            fprintf(stderr, "Monker strategy '%s' error: %s\n", strategy_name,
                    pe_monker_mkr_status_string(mkr_status));
            goto fail;
        }
        out->strategy_loaded = 1;
        if (out->tree_loaded)
        {
            /* The verified Monker class codec currently covers PLO4. Refuse
             * to call a different game's class numbering "applied" until its
             * real archive ordering has been measured. */
            if (game->game != game_omaha)
            {
                fprintf(stderr,
                        "Monker strategy application currently requires "
                        "the verified PLO4 class codec (game=%s)\n",
                        game->game_name);
                goto fail;
            }
            if (pe_monker_classes_create(&out->classes) != PE_MONKER_OK)
            {
                fprintf(stderr, "could not build Monker PLO4 class codec\n");
                goto fail;
            }
            out->classes_loaded = 1;
            if (pe_monker_strategy_open(out->tree, &out->strategy,
                                        out->classes,
                                        &out->strategy_view) != PE_MONKER_OK)
            {
                fprintf(stderr,
                        "Monker strategy does not bind to the supplied tree\n");
                goto fail;
            }
            out->strategy_view_loaded = 1;
            if (pe_monker_tree_vector_init(
                    &out->tree_vector, out->tree, options->player_count,
                    (uint16_t)pe_monker_strategy_class_count(
                        out->strategy_view),
                    monker_cli_terminal_values, &out->walk_stats) != 0)
            {
                fprintf(stderr,
                    "Monker tree cannot be represented by the vector "
                    "topology adapter\n");
                goto fail;
            }
            out->tree_vector_loaded = 1;
            if (pe_monker_strategy_vector_game_init(
                    &out->strategy_vector, &out->tree_vector.game,
                    out->strategy_view, monker_cli_decode_combo,
                    out->classes) != PE_MONKER_OK)
            {
                fprintf(stderr,
                        "Monker strategy could not be attached to vector "
                        "tree topology\n");
                goto fail;
            }
            out->strategy_vector_loaded = 1;
            if (monker_cli_walk(out, options->player_count) != 0)
            {
                fprintf(stderr,
                        "Monker strategy/tree vector traversal failed\n");
                goto fail;
            }
        }
    }
    return 0;

fail:
    monker_cli_free(out);
    return -1;
}

static void destroy_state(pe_betting_state_t *state)
{
    (void)state;
}

static int run_holdem(const EvalContext *context, mask_t board,
                      const cli_options_t *options, double *values,
                      size_t *deal_count, double *weight_sum)
{
    pe_holdem_range_t ranges[PE_VECTOR_SIM_MAX_PLAYERS];
    pe_holdem_combo_t *owned[PE_VECTOR_SIM_MAX_PLAYERS] = {0};
    pe_betting_state_t state;
    uint8_t player;
    int status = -1;
    memset(ranges, 0, sizeof(ranges));
    for (player = 0u; player < options->player_count; ++player)
    {
        pe_range_t *parsed = NULL;
        const pe_range_t *input;
        size_t combo;
        size_t kept = 0u;
        StdDeck_CardMask dead = modern_to_std(board);
        input = options->source_ranges[player];
        if (!input)
        {
            if (pe_range_parse(game_holdem, options->ranges[player], dead,
                               NULL, &parsed) != PE_STATUS_OK || !parsed)
                goto cleanup;
            input = parsed;
        }
        owned[player] = (pe_holdem_combo_t *)calloc(
            input->count, sizeof(*owned[player]));
        if (!owned[player])
        {
            pe_range_free(parsed);
            goto cleanup;
        }
        ranges[player].combos = owned[player];
        for (combo = 0u; combo < input->count; ++combo)
        {
            mask_t cards = std_to_modern(input->combos[combo].hand);
            if ((cards & board) != MASK_EMPTY || mask_popcount(cards) != 2)
                continue;
            owned[player][kept].cards = cards;
            owned[player][kept].weight = input->combos[combo].weight;
            ++kept;
        }
        ranges[player].count = kept;
        pe_range_free(parsed);
        if (kept == 0u)
            goto cleanup;
    }
    memset(&state, 0, sizeof(state));
    state.player_count = options->player_count;
    state.pot = options->pot;
    state.winner = -1;
    for (player = 0u; player < options->player_count; ++player)
    {
        state.active[player] = 1;
        state.invested[player] = options->invested;
    }
    status = pe_holdem_river_range_values(
        context, board, ranges, &state, values, options->player_count,
        deal_count, weight_sum);
cleanup:
    for (player = 0u; player < options->player_count; ++player)
        free(owned[player]);
    destroy_state(&state);
    return status;
}

static int run_omaha(const EvalContext *context, mask_t board,
                     const cli_options_t *options, const game_spec_t *game,
                     double *values, size_t *deal_count, double *weight_sum)
{
    pe_omaha_range_t ranges[PE_VECTOR_SIM_MAX_PLAYERS];
    pe_omaha_combo_t *owned[PE_VECTOR_SIM_MAX_PLAYERS] = {0};
    pe_betting_state_t state;
    uint8_t player;
    int status = -1;
    memset(ranges, 0, sizeof(ranges));
    for (player = 0u; player < options->player_count; ++player)
    {
        pe_range_t *parsed = NULL;
        const pe_range_t *input;
        size_t combo;
        size_t kept = 0u;
        StdDeck_CardMask dead = modern_to_std(board);
        input = options->source_ranges[player];
        if (!input && game->hole_cards > 4u)
        {
            if (parse_exact_omaha_combo(options->ranges[player],
                                        game->hole_cards, &owned[player]) != 0)
                goto cleanup;
            ranges[player].combos = owned[player];
            ranges[player].count = 1u;
            continue;
        }
        if (!input)
        {
            if (pe_range_parse(game->game, options->ranges[player], dead, NULL,
                               &parsed) != PE_STATUS_OK || !parsed)
                goto cleanup;
            input = parsed;
        }
        owned[player] = (pe_omaha_combo_t *)calloc(
            input->count, sizeof(*owned[player]));
        if (!owned[player])
        {
            pe_range_free(parsed);
            goto cleanup;
        }
        ranges[player].combos = owned[player];
        for (combo = 0u; combo < input->count; ++combo)
        {
            mask_t cards = std_to_modern(input->combos[combo].hand);
            if ((cards & board) != MASK_EMPTY ||
                mask_popcount(cards) != game->hole_cards)
                continue;
            owned[player][kept].cards = cards;
            owned[player][kept].weight = input->combos[combo].weight;
            ++kept;
        }
        ranges[player].count = kept;
        pe_range_free(parsed);
        if (kept == 0u)
            goto cleanup;
    }
    memset(&state, 0, sizeof(state));
    state.player_count = options->player_count;
    state.pot = options->pot;
    state.winner = -1;
    for (player = 0u; player < options->player_count; ++player)
    {
        state.active[player] = 1;
        state.invested[player] = options->invested;
    }
    status = pe_omaha_river_range_values(
        context, board, ranges, &state, game->hole_cards, values,
        options->player_count, deal_count, weight_sum);
cleanup:
    for (player = 0u; player < options->player_count; ++player)
        free(owned[player]);
    destroy_state(&state);
    return status;
}

static int run_omaha_tree(const EvalContext *context, mask_t board,
                          const cli_options_t *options, const game_spec_t *game,
                          const monker_cli_t *monker, double *values,
                          size_t *deal_count, double *weight_sum,
                          double *path_weight)
{
    pe_omaha_range_t ranges[PE_VECTOR_SIM_MAX_PLAYERS];
    pe_omaha_combo_t *owned[PE_VECTOR_SIM_MAX_PLAYERS] = {0};
    pe_betting_state_t state;
    uint8_t player;
    int status = -1;

    memset(ranges, 0, sizeof(ranges));
    for (player = 0u; player < options->player_count; ++player)
    {
        pe_range_t *parsed = NULL;
        const pe_range_t *input = options->source_ranges[player];
        size_t combo;
        size_t kept = 0u;
        StdDeck_CardMask dead = modern_to_std(board);
        if (!input && game->hole_cards > 4u)
        {
            if (parse_exact_omaha_combo(options->ranges[player],
                                        game->hole_cards, &owned[player]) != 0)
                goto cleanup;
            ranges[player].combos = owned[player];
            ranges[player].count = 1u;
            continue;
        }
        if (!input)
        {
            if (pe_range_parse(game->game, options->ranges[player], dead, NULL,
                               &parsed) != PE_STATUS_OK || !parsed)
                goto cleanup;
            input = parsed;
        }
        owned[player] = (pe_omaha_combo_t *)calloc(
            input->count, sizeof(*owned[player]));
        if (!owned[player])
        {
            pe_range_free(parsed);
            goto cleanup;
        }
        ranges[player].combos = owned[player];
        for (combo = 0u; combo < input->count; ++combo)
        {
            mask_t cards = std_to_modern(input->combos[combo].hand);
            if ((cards & board) != MASK_EMPTY ||
                mask_popcount(cards) != game->hole_cards)
                continue;
            owned[player][kept].cards = cards;
            owned[player][kept].weight = input->combos[combo].weight;
            ++kept;
        }
        ranges[player].count = kept;
        pe_range_free(parsed);
        if (kept == 0u)
            goto cleanup;
    }
    memset(&state, 0, sizeof(state));
    state.player_count = options->player_count;
    state.pot = options->pot;
    state.winner = -1;
    for (player = 0u; player < options->player_count; ++player)
    {
        state.active[player] = 1;
        state.invested[player] = options->invested;
        state.round_contrib[player] = options->invested;
        state.stack[player] = monker->header.stacks[player] >
                                      options->invested
                                  ? monker->header.stacks[player] -
                                        options->invested
                                  : 0.0;
    }
    state.to_call = options->invested;
    state.current_bet = options->invested;
    state.min_raise = options->invested > 0.0 ? options->invested : 1.0;
    {
        pe_monker_omaha_tree_spec_t spec;
        memset(&spec, 0, sizeof(spec));
        spec.context = context;
        spec.board = board;
        spec.ranges = ranges;
        spec.state = &state;
        spec.player_count = options->player_count;
        spec.hole_cards = game->hole_cards;
        spec.tree = monker->tree;
        spec.strategy = monker->strategy_view;
        spec.classes = monker->classes;
        status = pe_monker_omaha_tree_values(&spec, values, deal_count,
                                             weight_sum, path_weight);
    }
cleanup:
    for (player = 0u; player < options->player_count; ++player)
        free(owned[player]);
    return status;
}

int main(int argc, char **argv)
{
    cli_options_t options;
    game_spec_t game;
    monker_cli_t monker;
    EvalConfig config;
    EvalContext *context;
    mask_t board;
    double values[PE_VECTOR_SIM_MAX_PLAYERS] = {0};
    size_t deal_count = 0u;
    double weight_sum = 0.0;
    int status;
    uint8_t player;

    status = parse_args(argc, argv, &options, &game);
    if (status != 0)
    {
        if (status < 0)
            usage(argv[0]);
        return status > 0 ? 0 : 2;
    }
    board = string_to_mask(options.board_text);
    if (mask_popcount(board) != 5)
    {
        fprintf(stderr, "--board must contain exactly five valid cards\n");
        return 2;
    }
    if (monker_cli_load(&options, &game, &monker) != 0)
        return 2;
    if (monker.ranges_loaded)
    {
        for (player = 0u; player < options.player_count; ++player)
            options.source_ranges[player] = monker.ranges.players[player];
    }
    config = eval_config_holdem();
    context = eval_context_create(&config);
    if (!context)
    {
        fprintf(stderr, "could not create evaluation context\n");
        monker_cli_free(&monker);
        return 1;
    }
    if (game.game == game_holdem)
        status = run_holdem(context, board, &options, values, &deal_count,
                            &weight_sum);
    else
        status = run_omaha(context, board, &options, &game, values,
                           &deal_count, &weight_sum);
    if (status != 0)
    {
        fprintf(stderr, "vector terminal evaluation failed (status=%d)\n",
                status);
        eval_context_destroy(context);
        monker_cli_free(&monker);
        return 1;
    }
    if (monker.strategy_view_loaded && game.game == game_omaha)
    {
        status = run_omaha_tree(context, board, &options, &game, &monker,
                                monker.tree_ev, &monker.tree_deal_count,
                                &monker.tree_weight_sum,
                                &monker.tree_path_weight);
        if (status != 0 ||
            fabs(monker.tree_path_weight - 1.0) > 1e-9)
        {
            fprintf(stderr,
                    "Monker tree did not produce a complete weighted "
                    "terminal distribution (mass=%.17g)\n",
                    monker.tree_path_weight);
            eval_context_destroy(context);
            monker_cli_free(&monker);
            return 1;
        }
        for (player = 0u; player < options.player_count; ++player)
            values[player] = monker.tree_ev[player];
        deal_count = monker.tree_deal_count;
        weight_sum = monker.tree_weight_sum;
        monker.tree_ev_loaded = 1;
    }
    printf("game=%s players=%u board=", game.game_name, options.player_count);
    {
        char board_text[128];
        printf("%s", mask_to_string(board, board_text, sizeof(board_text)));
    }
    printf(" deals=%zu weight=%.17g", deal_count, weight_sum);
    if (monker.tree_loaded)
        printf(" tree_nodes=%d tree_players=%u", monker.tree->node_count,
               monker.header.player_count);
    if (monker.archive_loaded)
    {
        printf(" mkr_entries=%zu strategy_slots=%u", monker.archive.count,
               monker.strategy_loaded ? monker.strategy.slot_count : 0u);
        if (monker.metadata_loaded)
            printf(" iterations=%lld", (long long)monker.metadata.iterations);
        if (monker.strategy_view_loaded)
            printf(" strategy_binding=vector classes=%u",
                   pe_monker_strategy_class_count(monker.strategy_view));
        if (monker.tree_walk_loaded)
            printf(" strategy_tree_traversal=vector visited=%zu terminals=%zu "
                   "reach_mass=%.17g",
                   monker.walk_stats.visited_nodes,
                   monker.walk_stats.terminal_nodes,
                   monker.walk_stats.reach_mass);
        if (monker.tree_ev_loaded)
            printf(" ev_weighting=tree_path terminal_mass=%.17g "
                   "bet_amounts=tree_applied",
                   monker.tree_path_weight);
        else if (monker.tree_walk_loaded)
            printf(" ev_weighting=not_applied");
    }
    for (player = 0u; player < options.player_count; ++player)
        printf(" ev%u=%.17g", player, values[player]);
    putchar('\n');
    eval_context_destroy(context);
    monker_cli_free(&monker);
    return 0;
}
