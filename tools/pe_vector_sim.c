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
#include <poker_eval/solver/pe_omaha_deals.h>
#include <poker_eval/solver/pe_omaha_river.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_VECTOR_SIM_MAX_PLAYERS 8u

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
    uint8_t player_count;
    double invested;
    double pot;
    int pot_explicit;
} cli_options_t;

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
            "                      PLO5/PLO6 currently require an exact hand\n"
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
            strcmp(arg, "--invested") == 0 || strcmp(arg, "--pot") == 0)
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
    for (index = 0; index < (int)out->player_count; ++index)
        if (!out->ranges[index])
            return -1;
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
        size_t combo;
        StdDeck_CardMask dead = modern_to_std(board);
        if (pe_range_parse(game_holdem, options->ranges[player], dead, NULL,
                           &parsed) != PE_STATUS_OK || !parsed)
            goto cleanup;
        owned[player] = (pe_holdem_combo_t *)calloc(
            parsed->count, sizeof(*owned[player]));
        if (!owned[player])
        {
            pe_range_free(parsed);
            goto cleanup;
        }
        ranges[player].combos = owned[player];
        ranges[player].count = parsed->count;
        for (combo = 0u; combo < parsed->count; ++combo)
        {
            owned[player][combo].cards =
                std_to_modern(parsed->combos[combo].hand);
            owned[player][combo].weight = parsed->combos[combo].weight;
        }
        pe_range_free(parsed);
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
        size_t combo;
        StdDeck_CardMask dead = modern_to_std(board);
        if (game->hole_cards > 4u)
        {
            if (parse_exact_omaha_combo(options->ranges[player],
                                        game->hole_cards, &owned[player]) != 0)
                goto cleanup;
            ranges[player].combos = owned[player];
            ranges[player].count = 1u;
            continue;
        }
        if (pe_range_parse(game->game, options->ranges[player], dead, NULL,
                           &parsed) != PE_STATUS_OK || !parsed)
            goto cleanup;
        owned[player] = (pe_omaha_combo_t *)calloc(
            parsed->count, sizeof(*owned[player]));
        if (!owned[player])
        {
            pe_range_free(parsed);
            goto cleanup;
        }
        ranges[player].combos = owned[player];
        ranges[player].count = parsed->count;
        for (combo = 0u; combo < parsed->count; ++combo)
        {
            owned[player][combo].cards =
                std_to_modern(parsed->combos[combo].hand);
            owned[player][combo].weight = parsed->combos[combo].weight;
        }
        pe_range_free(parsed);
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

int main(int argc, char **argv)
{
    cli_options_t options;
    game_spec_t game;
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
    config = eval_config_holdem();
    context = eval_context_create(&config);
    if (!context)
    {
        fprintf(stderr, "could not create evaluation context\n");
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
        return 1;
    }
    printf("game=%s players=%u board=", game.game_name, options.player_count);
    {
        char board_text[128];
        printf("%s", mask_to_string(board, board_text, sizeof(board_text)));
    }
    printf(" deals=%zu weight=%.17g", deal_count, weight_sum);
    for (player = 0u; player < options.player_count; ++player)
        printf(" ev%u=%.17g", player, values[player]);
    putchar('\n');
    eval_context_destroy(context);
    return 0;
}
