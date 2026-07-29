#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/equity/RangeEquity.h>
#include <poker_eval/deck/deck_std.h>

#if defined(_WIN32)
#define strtok_r strtok_s
#endif

#define MAX_PLAYERS 10

typedef struct
{
    char **items;
    int count;
    int capacity;
} string_list_t;

typedef struct
{
    double *values;
    int count;
} double_list_t;

typedef struct
{
    int total_combos;
    int playable_combos;
    double total_weight;
    double playable_weight;
} combo_stats_t;

static void string_list_init(string_list_t *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void string_list_append(string_list_t *list, const char *value)
{
    if (!value)
        return;
    if (list->count == list->capacity)
    {
        int new_cap = list->capacity ? list->capacity * 2 : 8;
        char **new_items = (char **)realloc(list->items, new_cap * sizeof(char *));
        if (!new_items)
        {
            fprintf(stderr, "Allocation failure while growing string list\n");
            exit(EXIT_FAILURE);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count] = strdup(value);
    if (!list->items[list->count])
    {
        fprintf(stderr, "Allocation failure while duplicating range string\n");
        exit(EXIT_FAILURE);
    }
    list->count += 1;
}

static void string_list_free(string_list_t *list)
{
    if (!list)
        return;
    for (int i = 0; i < list->count; ++i)
        free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void parse_double_list(const char *arg, double_list_t *out)
{
    if (!arg || !out)
        return;
    size_t len = strlen(arg);
    char *buf = (char *)malloc(len + 1);
    if (!buf)
    {
        fprintf(stderr, "Allocation failure while parsing numeric list\n");
        exit(EXIT_FAILURE);
    }
    memcpy(buf, arg, len + 1);
    char *saveptr = NULL;
    char *token = strtok_r(buf, ",", &saveptr);
    while (token)
    {
        char *endp = NULL;
        double v = strtod(token, &endp);
        if (endp == token)
        {
            fprintf(stderr, "Failed to parse numeric value '%s' in list '%s'\n", token, arg);
            free(buf);
            exit(EXIT_FAILURE);
        }
        out->values = (double *)realloc(out->values, (size_t)(out->count + 1) * sizeof(double));
        if (!out->values)
        {
            fprintf(stderr, "Allocation failure while growing numeric list\n");
            free(buf);
            exit(EXIT_FAILURE);
        }
        out->values[out->count++] = v;
        token = strtok_r(NULL, ",", &saveptr);
    }
    free(buf);
}

static int parse_card_mask(const char *input, StdDeck_CardMask *out)
{
    StdDeck_CardMask_RESET(*out);
    if (!input || !*input)
        return 1;

    size_t len = strlen(input);
    for (size_t i = 0; i < len;)
    {
        if (isspace((unsigned char)input[i]) || input[i] == ',')
        {
            ++i;
            continue;
        }
        if (i + 1 >= len)
        {
            fprintf(stderr, "Incomplete card spec near '%s'\n", input + i);
            return 0;
        }
        char rank_char = (char)toupper((unsigned char)input[i]);
        char suit_char = (char)tolower((unsigned char)input[i + 1]);
        int rank = -1;
        int suit = -1;
        for (int r = StdDeck_Rank_FIRST; r <= StdDeck_Rank_LAST; ++r)
        {
            if (StdDeck_rankChars[r] == rank_char)
            {
                rank = r;
                break;
            }
        }
        for (int s = StdDeck_Suit_FIRST; s <= StdDeck_Suit_LAST; ++s)
        {
            if (StdDeck_suitChars[s] == suit_char)
            {
                suit = s;
                break;
            }
        }
        if (rank < 0 || suit < 0)
        {
            fprintf(stderr, "Invalid card '%c%c' in \"%s\"\n", input[i], input[i + 1], input);
            return 0;
        }
        int card = StdDeck_MAKE_CARD(rank, suit);
        if (StdDeck_CardMask_CARD_IS_SET(*out, card))
        {
            fprintf(stderr, "Duplicate card '%c%c' in \"%s\"\n", input[i], input[i + 1], input);
            return 0;
        }
        StdDeck_CardMask_SET(*out, card);
        i += 2;
    }
    return 1;
}

static void compute_combo_stats(const arp_range_t *range,
                                StdDeck_CardMask board,
                                StdDeck_CardMask dead,
                                combo_stats_t *stats)
{
    if (!stats)
        return;
    stats->total_combos = range ? (int)range->count : 0;
    stats->total_weight = (range && range->has_weights) ? range->total_weight : (double)stats->total_combos;
    stats->playable_combos = 0;
    stats->playable_weight = 0.0;
    if (!range)
        return;

    StdDeck_CardMask blockers;
    StdDeck_CardMask_OR(blockers, board, dead);
    for (size_t i = 0; i < range->count; ++i)
    {
        StdDeck_CardMask hand = range->hands[i];
        if (StdDeck_CardMask_ANY_SET(hand, blockers))
            continue;
        stats->playable_combos++;
        double weight = (range->has_weights && range->weights) ? range->weights[i] : 1.0;
        stats->playable_weight += weight;
    }
}

static const char *game_to_string(enum_game_t game)
{
    enum_gameparams_t *params = enumGameParams(game);
    if (params && params->name)
        return params->name;
    return "unknown";
}

static enum_game_t parse_game(const char *name)
{
    if (!name || !*name)
        return game_holdem;

    char lower[32];
    size_t len = strlen(name);
    if (len >= sizeof(lower))
        len = sizeof(lower) - 1;
    for (size_t i = 0; i < len; ++i)
        lower[i] = (char)tolower((unsigned char)name[i]);
    lower[len] = '\0';

    if (strcmp(lower, "holdem") == 0 || strcmp(lower, "he") == 0)
        return game_holdem;
    if (strcmp(lower, "omaha") == 0 || strcmp(lower, "plo") == 0)
        return game_omaha;
    if (strcmp(lower, "omaha8") == 0 || strcmp(lower, "plohilo") == 0 || strcmp(lower, "plo8") == 0)
        return game_omaha8;
    if (strcmp(lower, "shortdeck") == 0 || strcmp(lower, "sixplus") == 0)
        return game_sdholdem;

    fprintf(stderr, "Unknown game '%s', defaulting to Hold'em\n", name);
    return game_holdem;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --range <expr> [--range <expr> ...] [options]\n"
            "Options:\n"
            "  --game <holdem|omaha|omaha8|shortdeck>   (default holdem)\n"
            "  --board <cards>                           board cards (e.g. AhKhQc)\n"
            "  --dead <cards>                            dead cards (space or comma separated)\n"
            "  --invested <v1,v2,...>                    invested amounts per player (defaults to 1.0)\n"
            "  --method <exhaustive|mc>                  evaluation method (default exhaustive)\n"
            "  --iterations <n>                          MC iterations (default 10000 when --method mc)\n"
            "  --cards-to-deal <n>                       number of future board cards to run out (auto for holdem)\n"
            "  --orderflag <n>                           advanced ordering flag (default 0)\n"
            "  --eedc-threads <n>                        override PE_EEDC_THREADS env (1..1024)\n"
            "  --help                                    show this help\n",
            prog);
}

int main(int argc, char **argv)
{
    if (argc <= 1)
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    string_list_t ranges;
    string_list_init(&ranges);

    double_list_t invested_list = {NULL, 0};

    enum_game_t game = game_holdem;
    const char *board_arg = "";
    const char *dead_arg = "";
    int cards_to_deal = -1;
    int cards_to_deal_fixed = 0;
    int orderflag = 0;
    int use_montecarlo = 0;
    int iterations = 0;
    int eedc_threads = 0;
    int eedc_threads_set = 0;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--range") == 0 && i + 1 < argc)
        {
            string_list_append(&ranges, argv[++i]);
        }
        else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc)
        {
            game = parse_game(argv[++i]);
        }
        else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc)
        {
            board_arg = argv[++i];
        }
        else if (strcmp(argv[i], "--dead") == 0 && i + 1 < argc)
        {
            dead_arg = argv[++i];
        }
        else if (strcmp(argv[i], "--invested") == 0 && i + 1 < argc)
        {
            parse_double_list(argv[++i], &invested_list);
        }
        else if (strcmp(argv[i], "--method") == 0 && i + 1 < argc)
        {
            const char *method = argv[++i];
            if (strcmp(method, "mc") == 0 || strcmp(method, "montecarlo") == 0)
                use_montecarlo = 1;
            else if (strcmp(method, "exhaustive") == 0)
                use_montecarlo = 0;
            else
            {
                fprintf(stderr, "Unknown method '%s'\n", method);
                usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
        {
            iterations = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--cards-to-deal") == 0 && i + 1 < argc)
        {
            cards_to_deal = atoi(argv[++i]);
            cards_to_deal_fixed = 1;
        }
        else if (strcmp(argv[i], "--orderflag") == 0 && i + 1 < argc)
        {
            orderflag = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--eedc-threads") == 0 && i + 1 < argc)
        {
            const char *value = argv[++i];
            char *endptr = NULL;
            errno = 0;
            long parsed = strtol(value, &endptr, 10);
            if (errno != 0 || endptr == value || *endptr != '\0' || parsed <= 0 || parsed > 1024)
            {
                fprintf(stderr, "Invalid --eedc-threads value '%s' (expected integer 1..1024)\n", value);
                return EXIT_FAILURE;
            }
            eedc_threads = (int)parsed;
            eedc_threads_set = 1;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (ranges.count < 2)
    {
        fprintf(stderr, "At least two --range expressions are required\n");
        return EXIT_FAILURE;
    }
    if (ranges.count > MAX_PLAYERS)
    {
        fprintf(stderr, "Too many players (%d). Maximum supported is %d\n", ranges.count, MAX_PLAYERS);
        return EXIT_FAILURE;
    }

    if (invested_list.count != 0 && invested_list.count != ranges.count)
    {
        fprintf(stderr, "Number of invested values (%d) must match number of ranges (%d)\n",
                invested_list.count, ranges.count);
        return EXIT_FAILURE;
    }

    if (use_montecarlo && iterations <= 0)
    {
        iterations = 10000;
    }

    if (eedc_threads_set)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", eedc_threads);
        if (setenv("PE_EEDC_THREADS", buf, 1) != 0)
        {
            perror("setenv(PE_EEDC_THREADS)");
            return EXIT_FAILURE;
        }
    }

    StdDeck_CardMask board_mask;
    StdDeck_CardMask dead_mask;
    if (!parse_card_mask(board_arg, &board_mask))
        return EXIT_FAILURE;
    if (!parse_card_mask(dead_arg, &dead_mask))
        return EXIT_FAILURE;

    enum_gameparams_t *params = enumGameParams(game);
    int board_card_cap = params ? params->maxboard : 5;
    if (board_card_cap <= 0)
        board_card_cap = 5;

    int board_cards = StdDeck_numCards(board_mask);
    if (board_cards > board_card_cap)
    {
        fprintf(stderr, "Too many board cards (%d) for game (max %d)\n",
                board_cards, board_card_cap);
        return EXIT_FAILURE;
    }

    int board_cards_for_eval = board_cards;
    if (cards_to_deal_fixed)
    {
        if (cards_to_deal < 0 || cards_to_deal > board_card_cap)
        {
            fprintf(stderr, "Invalid --cards-to-deal value %d (expected 0..%d)\n",
                    cards_to_deal, board_card_cap);
            return EXIT_FAILURE;
        }
        board_cards_for_eval = board_card_cap - cards_to_deal;
    }
    else
    {
        cards_to_deal = board_card_cap - board_cards;
    }
    if (board_cards_for_eval < 0)
        board_cards_for_eval = 0;

    if (board_cards != board_cards_for_eval)
    {
        fprintf(stderr,
                "Board card count mismatch: provided %d cards, but configuration implies %d known cards\n",
                board_cards, board_cards_for_eval);
        fprintf(stderr,
                "Hint: adjust --board or --cards-to-deal so that known board cards match.\n");
        return EXIT_FAILURE;
    }

    PlayerRange *player_ranges = (PlayerRange *)calloc((size_t)ranges.count, sizeof(PlayerRange));
    arp_range_t *parsed_ranges = (arp_range_t *)calloc((size_t)ranges.count, sizeof(arp_range_t));
    if (!player_ranges || !parsed_ranges)
    {
        fprintf(stderr, "Allocation failure while preparing player ranges\n");
        free(player_ranges);
        free(parsed_ranges);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < ranges.count; ++i)
    {
        if (!ARP_ParseRange(ranges.items[i], dead_mask, game, &parsed_ranges[i]))
        {
            fprintf(stderr, "Failed to parse range expression '%s'\n", ranges.items[i]);
            for (int j = 0; j < i; ++j)
                ARP_FreeRange(&parsed_ranges[j]);
            free(player_ranges);
            free(parsed_ranges);
            return EXIT_FAILURE;
        }
        if (!ARP_ToPlayerRange(&parsed_ranges[i], &player_ranges[i]))
        {
            fprintf(stderr, "Failed to convert range '%s' to PlayerRange\n", ranges.items[i]);
            for (int j = 0; j <= i; ++j)
                ARP_FreeRange(&parsed_ranges[j]);
            free(player_ranges);
            free(parsed_ranges);
            return EXIT_FAILURE;
        }
    }

    double *stack_sizes = (double *)calloc((size_t)ranges.count, sizeof(double));
    double *invested = (double *)calloc((size_t)ranges.count, sizeof(double));
    if (!stack_sizes || !invested)
    {
        fprintf(stderr, "Allocation failure while preparing stack/invested arrays\n");
        for (int i = 0; i < ranges.count; ++i)
            ARP_FreeRange(&parsed_ranges[i]);
        free(player_ranges);
        free(parsed_ranges);
        free(stack_sizes);
        free(invested);
        return EXIT_FAILURE;
    }
    for (int i = 0; i < ranges.count; ++i)
    {
        double base = (invested_list.count == ranges.count) ? invested_list.values[i] : 1.0;
        invested[i] = base;
    }

    combo_stats_t combo_stats[MAX_PLAYERS];
    memset(combo_stats, 0, sizeof(combo_stats));
    for (int i = 0; i < ranges.count; ++i)
    {
        compute_combo_stats(&parsed_ranges[i], board_mask, dead_mask, &combo_stats[i]);
    }

    MultiwayPotState state;
    state.ranges = player_ranges;
    state.stack_sizes = stack_sizes;
    state.invested = invested;
    state.num_players = ranges.count;

    MultiwayEquityOptions options;
    options.use_montecarlo = use_montecarlo;
    options.iterations = iterations;
    options.orderflag = orderflag;

    MultiwayEquityResult result;
    memset(&result, 0, sizeof(result));

    int matchups = CalculateMultiwayEquity(
        game,
        &state,
        board_mask,
        dead_mask,
        board_cards_for_eval,
        &options,
        &result);

    if (matchups < 0)
    {
        fprintf(stderr, "Multiway equity calculation failed (code %d)\n", matchups);
        for (int i = 0; i < ranges.count; ++i)
            ARP_FreeRange(&parsed_ranges[i]);
        free(player_ranges);
        free(parsed_ranges);
        free(stack_sizes);
        free(invested);
        free(invested_list.values);
        string_list_free(&ranges);
        return EXIT_FAILURE;
    }
    if (matchups == 0)
    {
        fprintf(stderr, "No valid hand matchups found for the provided ranges/board\n");
        fprintf(stderr, "Debug info:\n");
        char board_buf_dbg[64];
        char dead_buf_dbg[64];
        StdDeck_maskToString(board_mask, board_buf_dbg);
        StdDeck_maskToString(dead_mask, dead_buf_dbg);
        fprintf(stderr, "  Board mask: %s\n", board_buf_dbg[0] ? board_buf_dbg : "(none)");
        fprintf(stderr, "  Dead mask:  %s\n", dead_buf_dbg[0] ? dead_buf_dbg : "(none)");
        for (int i = 0; i < ranges.count; ++i)
        {
            int valid = 0;
            double wsum = 0.0;
            for (size_t h = 0; h < parsed_ranges[i].count; ++h)
            {
                StdDeck_CardMask hand = parsed_ranges[i].hands[h];
                if (StdDeck_CardMask_ANY_SET(hand, board_mask))
                    continue;
                if (StdDeck_CardMask_ANY_SET(hand, dead_mask))
                    continue;
                valid++;
                if (parsed_ranges[i].has_weights && parsed_ranges[i].weights)
                    wsum += parsed_ranges[i].weights[h];
            }
            fprintf(stderr, "  Range %d (%s): valid combos=%d weight_sum=%.2f\n",
                    i + 1, ranges.items[i], valid, wsum);
        }
        for (int i = 0; i < ranges.count; ++i)
            ARP_FreeRange(&parsed_ranges[i]);
        free(player_ranges);
        free(parsed_ranges);
        free(stack_sizes);
        free(invested);
        free(invested_list.values);
        string_list_free(&ranges);
        return EXIT_FAILURE;
    }

    char board_buf[64];
    char dead_buf[64];
    StdDeck_maskToString(board_mask, board_buf);
    StdDeck_maskToString(dead_mask, dead_buf);

    printf("Multiway Equity (%s) — %d players\n", game_to_string(game), ranges.count);
    printf("Board: %s\n", board_buf[0] ? board_buf : "(none)");
    printf("Dead: %s\n", dead_buf[0] ? dead_buf : "(none)");
    printf("Method: %s\n", use_montecarlo ? "Monte Carlo" : "Exhaustive");
    if (use_montecarlo)
        printf("Iterations: %d\n", iterations);
    const char *eedc_env = getenv("PE_EEDC_THREADS");
    printf("EEDC threads: %s\n", (eedc_env && *eedc_env) ? eedc_env : "(auto)");
    printf("Cards to deal: %d\n", cards_to_deal);
    printf("Order flag: %d\n", orderflag);
    printf("Matchups evaluated: %d\n", matchups);
    printf("Total weighted samples: %.0f\n", result.total_weighted_samples);
    printf("\n%-8s %-12s %-10s %-10s %-10s %-10s\n",
           "Player", "Invested", "Equity", "Win%", "Tie%", "EV");
    for (int i = 0; i < ranges.count; ++i)
    {
        double equity = result.equity[i];
        double winp = result.win_prob[i] * 100.0;
        double tiep = result.tie_prob[i] * 100.0;
        double ev = result.ev[i];
        printf("#%-7d %-12.2f %-10.4f %-10.2f %-10.2f %-10.4f  %s\n",
               i + 1,
               invested[i],
               equity,
               winp,
               tiep,
               ev,
               ranges.items[i]);
    }

    printf("\nRange stats (after board/dead filtering):\n");
    printf("%-8s %-12s %-14s %-14s %-14s\n",
           "Player", "Combos", "Playable", "Weight", "PlayableWt");
    for (int i = 0; i < ranges.count; ++i)
    {
        printf("#%-7d %-12d %-14d %-14.2f %-14.2f\n",
               i + 1,
               combo_stats[i].total_combos,
               combo_stats[i].playable_combos,
               combo_stats[i].total_weight,
               combo_stats[i].playable_weight);
    }

    for (int i = 0; i < ranges.count; ++i)
        ARP_FreeRange(&parsed_ranges[i]);
    free(player_ranges);
    free(parsed_ranges);
    free(stack_sizes);
    free(invested);
    free(invested_list.values);
    string_list_free(&ranges);

    return EXIT_SUCCESS;
}
