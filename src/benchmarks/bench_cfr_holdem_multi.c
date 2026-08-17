#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/core/modern_cardmask.h>
#include <poker_eval/range/AdvancedRangeParser.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/core/string_compat.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <poker_eval/core/pthread_compat.h>
#include <termios.h>
#include <poker_eval/core/unistd_compat.h>
#include <math.h>

#define HERO_HAND_STR_LEN 32

/* The 36 cards of the Short Deck (6+ only) game, as modern mask indices:
 * rank 4..12 (6..A) per suit, matching create_deck_mask(EVAL_DECK_SHORT)
 * in eval_context.c. Random deals must be drawn from this pool; indices
 * outside it (e.g. 16, 26..29, 39..42) are rejected by the evaluator. */
static const int shortdeck_cards[36] = {
    4, 5, 6, 7, 8, 9, 10, 11, 12,       /* clubs */
    17, 18, 19, 20, 21, 22, 23, 24, 25, /* diamonds */
    30, 31, 32, 33, 34, 35, 36, 37, 38, /* hearts */
    43, 44, 45, 46, 47, 48, 49, 50, 51  /* spades */
};

static bool is_shortdeck_card(int card)
{
    return card >= 0 && card < 52 && MODERN_GET_RANK(card) >= 4;
}

typedef struct
{
    char hand[HERO_HAND_STR_LEN];
    double total;
    double fold;
    double call;
    double raises[MPF_MAX_BET_SIZES];
} hero_row_t;

typedef struct
{
    hero_row_t *rows;
    size_t count;
    size_t capacity;
    int hero_index;
    int bet_size_count;
} hero_dump_t;

static volatile sig_atomic_t g_stop_flag = 0;
static volatile sig_atomic_t g_force_exit = 0;
static struct termios g_orig_termios;
static int g_termios_saved = 0;
static pthread_t g_ctrlw_thread;
static int g_ctrlw_thread_running = 0;

static void restore_terminal(void);

static void sigint_handler(int signo)
{
    if (!g_stop_flag)
    {
        g_stop_flag = 1;
        fprintf(stderr, "\n[signal] stop requested (Ctrl+C)\n" );
    }
    else
    {
        g_force_exit = 1;
        fprintf(stderr, "\n[signal] forcing exit\n");
    }
}

static void *ctrlw_thread_func(void *arg)
{
    (void)arg;
    int fd = STDIN_FILENO;
    while (!g_stop_flag)
    {
        unsigned char ch;
        ssize_t n = read(fd, &ch, 1);
        if (n > 0)
        {
            if (ch == 0x17) /* Ctrl+W */
            {
                if (!g_stop_flag)
                {
                    g_stop_flag = 1;
                    fprintf(stderr, "\n[signal] stop requested (Ctrl+W)\n");
                }
                break;
            }
        }
        else
        {
            usleep(10000);
        }
    }
    return NULL;
}

static int setup_ctrlw_listener(void)
{
    if (!isatty(STDIN_FILENO))
        return 0;
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1)
        return -1;
    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1)
        return -1;
    g_termios_saved = 1;
    if (pthread_create(&g_ctrlw_thread, NULL, ctrlw_thread_func, NULL) == 0)
    {
        g_ctrlw_thread_running = 1;
        return 0;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
    g_termios_saved = 0;
    return -1;
}

static void hero_dump_init(hero_dump_t *dump, int hero_index, int bet_size_count)
{
    dump->rows = NULL;
    dump->count = 0;
    dump->capacity = 0;
    dump->hero_index = hero_index;
    dump->bet_size_count = bet_size_count;
}

static void hero_dump_free(hero_dump_t *dump)
{
    free(dump->rows);
    dump->rows = NULL;
    dump->count = dump->capacity = 0;
}

static void restore_terminal(void)
{
    if (g_ctrlw_thread_running)
    {
        void *res = NULL;
        pthread_join(g_ctrlw_thread, &res);
        g_ctrlw_thread_running = 0;
    }
    if (g_termios_saved)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
        g_termios_saved = 0;
    }
}

typedef struct
{
    int enabled;
    int hero_index;
    int num_players;
    mpf_state_t *root_state;
    double mean;
    double m2;
    int count;
} monitor_state_t;

static void compute_avg_ev(cfr_game_t *game, cfr_storage_t *storage, uint64_t state_key, int num_players, double *out);
static int mask_to_array_local(mask_t mask, int *out, int limit);

static int hole_cards_required(mpf_rule_t rules)
{
    switch (rules)
    {
    case MPF_RULE_PLO4:
        return 4;
    case MPF_RULE_PLO5:
        return 5;
    case MPF_RULE_PLO6:
        return 6;
    case MPF_RULE_SHORTDECK:
    case MPF_RULE_HOLDEM:
    default:
        return 2;
    }
}

static void monitor_callback(int iteration, cfr_game_t *game, cfr_storage_t *storage, void *user)
{
    monitor_state_t *ms = (monitor_state_t *)user;
    if (!ms || !ms->enabled || ms->hero_index < 0)
        return;
    double ev[MPF_MAX_PLAYERS];
    compute_avg_ev(game, storage, (uint64_t)(uintptr_t)ms->root_state, ms->num_players, ev);
    double hero_ev = ev[ms->hero_index];
    ms->count += 1;
    double delta = hero_ev - ms->mean;
    ms->mean += delta / ms->count;
    ms->m2 += delta * (hero_ev - ms->mean);
    double variance = (ms->count > 1) ? (ms->m2 / (ms->count - 1)) : 0.0;
    double stdev = (variance > 0.0) ? sqrt(variance) : 0.0;
    fprintf(stderr, "[monitor] iter %d hero_ev=%.6f mean=%.6f stdev=%.6f\n",
            iteration, hero_ev, ms->mean, stdev);
}


static hero_row_t *hero_dump_get_row(hero_dump_t *dump, const char *hand)
{
    for (size_t i = 0; i < dump->count; ++i)
        if (strcmp(dump->rows[i].hand, hand) == 0)
            return &dump->rows[i];
    if (dump->count == dump->capacity)
    {
        size_t new_cap = dump->capacity ? dump->capacity * 2 : 32;
        hero_row_t *tmp = (hero_row_t *)realloc(dump->rows, new_cap * sizeof(hero_row_t));
        if (!tmp)
            return NULL;
        dump->rows = tmp;
        dump->capacity = new_cap;
    }
    hero_row_t *row = &dump->rows[dump->count++];
    memset(row, 0, sizeof(*row));
    strncpy(row->hand, hand, HERO_HAND_STR_LEN - 1);
    row->hand[HERO_HAND_STR_LEN - 1] = '\0';
    return row;
}

static int card_from_token(const char *tok)
{
    char tmp[3] = {0};
    tmp[0] = (char)toupper((unsigned char)tok[0]);
    tmp[1] = (char)tolower((unsigned char)tok[1]);
    int sd = -1;
    if (StdDeck_stringToCard(tmp, &sd) != 2)
        return -1;
    /* StdDeck suits are h=0, d=1, c=2, s=3 (suitChars "hdcs"); modern masks
     * are c=0, d=1, h=2, s=3. The adapter consumes modern indices, so the
     * parsed card must be converted or its suit is silently rotated. */
    static const int modern_suit[4] = {MODERN_SUIT_HEARTS, MODERN_SUIT_DIAMONDS,
                                       MODERN_SUIT_CLUBS, MODERN_SUIT_SPADES};
    return MODERN_MAKE_CARD(StdDeck_RANK(sd), modern_suit[StdDeck_SUIT(sd)]);
}

static int is_rank_char(int c)
{
    c = toupper(c);
    return (c == 'A' || c == 'K' || c == 'Q' || c == 'J' || c == 'T' ||
            (c >= '2' && c <= '9'));
}

static int parse_cards_list(const char *str, int max_cards, int *out_cards)
{
    int count = 0;
    size_t len = strlen(str);
    for (size_t i = 0; i < len && count < max_cards; ++i)
    {
        if (!is_rank_char(str[i]))
            continue;
        size_t j = i + 1;
        while (j < len && !isalpha((unsigned char)str[j]))
            ++j;
        if (j >= len)
            break;
        char tok[3];
        tok[0] = str[i];
        tok[1] = str[j];
        tok[2] = '\0';
        int card = card_from_token(tok);
        if (card >= 0)
        {
            out_cards[count++] = card;
            i = j;
        }
    }
    return count;
}

static int parse_double_list(const char *str, double *out, int max)
{
    if (!str || max <= 0)
        return 0;
    char *dup = strdup(str);
    if (!dup)
        return 0;
    int count = 0;
    char *tok = strtok(dup, ",");
    while (tok && count < max)
    {
        out[count++] = atof(tok);
        tok = strtok(NULL, ",");
    }
    free(dup);
    return count;
}

static int parse_int_list(const char *str, int *out, int max)
{
    if (!str || max <= 0)
        return 0;
    char *dup = strdup(str);
    if (!dup)
        return 0;
    int count = 0;
    char *tok = strtok(dup, ",");
    while (tok && count < max)
    {
        out[count++] = atoi(tok);
        tok = strtok(NULL, ",");
    }
    free(dup);
    return count;
}

/* FEAT-14 (#150): folded-range card bunching. Per-seat parsed ranges: for
 * each seat, the per-card probability that the card was in the seat's
 * (folded) initial hand, derived from an ARP range string. */
typedef struct
{
    int provided[MPF_MAX_PLAYERS];
    double prob[MPF_MAX_PLAYERS][52];
} fold_range_set_t;

static enum_game_t bench_arp_game(mpf_rule_t rules)
{
    switch (rules)
    {
    case MPF_RULE_PLO4:
        return game_omaha;
    case MPF_RULE_PLO5:
        return game_omaha5;
    case MPF_RULE_PLO6:
        return game_omaha6;
    default:
        return game_holdem;
    }
}

/* Parse a "P1:AA,KK,QQ|P2:TT+" specification into per-seat per-card marginal
 * probabilities (weighted over the range's hands). Returns 0 on success,
 * -1 on parse failure (message in err). */
static int parse_fold_ranges(const char *spec, int players, mpf_rule_t rules,
                             fold_range_set_t *out, char *err, size_t err_size)
{
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    if (!spec || !*spec)
        return 0;
    char *dup = strdup(spec);
    if (!dup)
    {
        if (err && err_size)
            snprintf(err, err_size, "out of memory");
        return -1;
    }
    int rc = 0;
    char *tok = strtok(dup, "|");
    while (tok)
    {
        char *colon = strchr(tok, ':');
        if (!colon)
        {
            if (err && err_size)
                snprintf(err, err_size, "fold range '%s' must be SEAT:RANGE", tok);
            rc = -1;
            break;
        }
        *colon = '\0';
        const char *seat_label = tok;
        const char *range_str = colon + 1;
        int seat = -1;
        if (seat_label[0] == 'P' && isdigit((unsigned char)seat_label[1]))
            seat = atoi(seat_label + 1) - 1;
        else if (isdigit((unsigned char)seat_label[0]))
            seat = atoi(seat_label) - 1;
        if (seat < 0 || seat >= players)
        {
            if (err && err_size)
                snprintf(err, err_size, "invalid seat '%s' (players=%d)", seat_label, players);
            rc = -1;
            break;
        }
        StdDeck_CardMask dead;
        StdDeck_CardMask_RESET(dead);
        arp_range_t range;
        memset(&range, 0, sizeof(range));
        if (!ARP_ParseRange(range_str, dead, bench_arp_game(rules), &range) ||
            range.count == 0)
        {
            if (err && err_size)
                snprintf(err, err_size, "cannot parse range '%s' for %s", range_str, seat_label);
            rc = -1;
            break;
        }
        double total = 0.0;
        for (size_t i = 0; i < range.count; ++i)
        {
            double w = range.has_weights && range.weights ? range.weights[i] : 1.0;
            if (w > 0.0)
                total += w;
        }
        if (total <= 0.0)
        {
            if (err && err_size)
                snprintf(err, err_size, "range '%s' for %s has zero total weight", range_str, seat_label);
            rc = -1;
            break;
        }
        for (size_t i = 0; i < range.count; ++i)
        {
            double w = range.has_weights && range.weights ? range.weights[i] : 1.0;
            if (w <= 0.0)
                continue;
            for (int c = 0; c < 52; ++c)
            {
                if (StdDeck_CardMask_CARD_IS_SET(range.hands[i], c))
                    out->prob[seat][c] += w / total;
            }
        }
        out->provided[seat] = 1;
        tok = strtok(NULL, "|");
    }
    free(dup);
    return rc;
}

static void deal_random_cards(bool used[52], int *out_cards, int need, unsigned *seed, bool shortdeck)
{
    uint64_t state = (uint64_t)(*seed);
    int range = shortdeck ? 36 : 52;
    for (int i = 0; i < need; ++i)
    {
        for (;;)
        {
            state = state * 6364136223846793005ull + 1ull;
            int draw = (int)(((state >> 32) % (uint64_t)range));
            int card = shortdeck ? shortdeck_cards[draw] : draw;
            if (!used[card])
            {
                used[card] = true;
                out_cards[i] = card;
                break;
            }
        }
    }
    *seed = (unsigned)state;
}

static int mask_to_array_local(mask_t mask, int *out, int limit)
{
    int count = 0;
    for (int c = 0; c < 52 && count < limit; ++c)
        if (mask_is_set(mask, c))
            out[count++] = c;
    return count;
}

static void mark_used_from_mask(bool used[52], mask_t m)
{
    int cards[6];
    int count = mask_to_array_local(m, cards, 6);
    for (int i = 0; i < count; ++i)
        if (cards[i] >= 0 && cards[i] < 52)
            used[cards[i]] = true;
}

static void format_hand_string(mask_t mask, int hole_cards, char *out, size_t out_size)
{
    int cards[6];
    int n = mask_to_array_local(mask, cards, 6);
    if (hole_cards < n)
        n = hole_cards;
    for (int i = 0; i < n - 1; ++i)
        for (int j = i + 1; j < n; ++j)
            if (cards[j] < cards[i])
            {
                int tmp = cards[i];
                cards[i] = cards[j];
                cards[j] = tmp;
            }
    size_t pos = 0;
    for (int i = 0; i < n; ++i)
    {
        char buf[3] = {0};
        StdDeck_cardToString(cards[i], buf);
        if (pos + 2 >= out_size)
            break;
        out[pos++] = buf[0];
        out[pos++] = buf[1];
    }
    out[pos] = '\0';
}

static void hero_dump_iter(uint64_t key, int n_actions, const double *regret, const double *avg, void *user)
{
    hero_dump_t *dump = (hero_dump_t *)user;
    mpf_state_t *st = (mpf_state_t *)(uintptr_t)key;
    if (!st || dump->hero_index < 0 || dump->hero_index >= st->num_players)
        return;
    if (!st->active[dump->hero_index])
        return;
    if (st->to_act != dump->hero_index)
        return;
    if (n_actions <= 0)
        return;

    double sum = 0.0;
    for (int i = 0; i < n_actions; ++i)
        sum += avg[i];
    if (sum <= 1e-12)
        return;

    double need = st->to_call - st->round_contrib[dump->hero_index];
    if (need < 1e-9)
        need = 0.0;

    int idx = 0;
    int fold_idx = -1;
    int call_idx = -1;
    int raise_start = -1;

    if (need > 0.0)
    {
        if (idx < n_actions)
            fold_idx = idx++;
    }
    if (idx < n_actions)
        call_idx = idx++;
    if (idx < n_actions)
        raise_start = idx;

    char hand_buf[HERO_HAND_STR_LEN];
    format_hand_string(st->hole[dump->hero_index], st->total_hole_cards, hand_buf, sizeof(hand_buf));
    hero_row_t *row = hero_dump_get_row(dump, hand_buf);
    if (!row)
        return;

    row->total += sum;
    if (fold_idx >= 0 && fold_idx < n_actions)
        row->fold += avg[fold_idx];
    if (call_idx >= 0 && call_idx < n_actions)
        row->call += avg[call_idx];
    if (raise_start >= 0)
    {
        int raise_count = n_actions - raise_start;
        if (raise_count > dump->bet_size_count)
            raise_count = dump->bet_size_count;
        for (int r = 0; r < raise_count; ++r)
            row->raises[r] += avg[raise_start + r];
    }
}

static int hero_row_cmp(const void *a, const void *b)
{
    const hero_row_t *ra = (const hero_row_t *)a;
    const hero_row_t *rb = (const hero_row_t *)b;
    return strcmp(ra->hand, rb->hand);
}

static int hero_dump_write(const hero_dump_t *dump, const char *path)
{
    FILE *fp = NULL;
    int close_fp = 0;
    if (!path || strcmp(path, "-") == 0)
    {
        fp = stdout;
    }
    else
    {
        fp = fopen(path, "w");
        if (!fp)
        {
            fprintf(stderr, "Failed to open %s for writing: %s\n", path, strerror(errno));
            return -1;
        }
        close_fp = 1;
    }

    if (dump->count > 1)
        qsort(dump->rows, dump->count, sizeof(hero_row_t), hero_row_cmp);

    fprintf(fp, "hand,total_mass,fold_mass,call_mass");
    for (int r = 0; r < dump->bet_size_count; ++r)
        fprintf(fp, ",raise%d_mass", r + 1);
    fprintf(fp, ",fold_pct,call_pct");
    for (int r = 0; r < dump->bet_size_count; ++r)
        fprintf(fp, ",raise%d_pct", r + 1);
    fprintf(fp, "\n");

    for (size_t i = 0; i < dump->count; ++i)
    {
        const hero_row_t *row = &dump->rows[i];
        double total = row->total;
        double fold_pct = (total > 0.0) ? (row->fold / total) : 0.0;
        double call_pct = (total > 0.0) ? (row->call / total) : 0.0;
        fprintf(fp, "%s,%.6f,%.6f,%.6f", row->hand, total, row->fold, row->call);
        for (int r = 0; r < dump->bet_size_count; ++r)
            fprintf(fp, ",%.6f", row->raises[r]);
        fprintf(fp, ",%.6f,%.6f", fold_pct, call_pct);
        for (int r = 0; r < dump->bet_size_count; ++r)
        {
            double pct = (total > 0.0) ? (row->raises[r] / total) : 0.0;
            fprintf(fp, ",%.6f", pct);
        }
        fprintf(fp, "\n");
    }

    if (close_fp)
        fclose(fp);
    return 0;
}

static void mask_add_card(mask_t *m, int card)
{
    *m = mask_set(*m, card);
}

static mpf_rule_t parse_game(const char *s)
{
    if (!s)
        return MPF_RULE_HOLDEM;
    if (strcasecmp(s, "holdem") == 0 || strcasecmp(s, "nlhe") == 0)
        return MPF_RULE_HOLDEM;
    if (strcasecmp(s, "plo4") == 0 || strcasecmp(s, "plo") == 0)
        return MPF_RULE_PLO4;
    if (strcasecmp(s, "plo5") == 0)
        return MPF_RULE_PLO5;
    if (strcasecmp(s, "plo6") == 0)
        return MPF_RULE_PLO6;
    if (strcasecmp(s, "shortdeck") == 0 || strcasecmp(s, "sixplus") == 0)
        return MPF_RULE_SHORTDECK;
    return MPF_RULE_HOLDEM;
}

static const char *rule_to_string(mpf_rule_t rule)
{
    switch (rule)
    {
    case MPF_RULE_HOLDEM:
        return "holdem";
    case MPF_RULE_PLO4:
        return "plo4";
    case MPF_RULE_PLO5:
        return "plo5";
    case MPF_RULE_PLO6:
        return "plo6";
    case MPF_RULE_SHORTDECK:
        return "shortdeck";
    default:
        return "unknown";
    }
}

static int parse_hero_label(const char *label, int players, int button_index)
{
    if (!label || players <= 0)
        return -1;
    char buf[16];
    size_t len = strlen(label);
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    for (size_t i = 0; i < len; ++i)
        buf[i] = (char)toupper((unsigned char)label[i]);
    buf[len] = '\0';

    if (buf[0] == 'P' && isdigit((unsigned char)buf[1]))
    {
        int seat = atoi(buf + 1);
        if (seat >= 1 && seat <= players)
            return (seat - 1) % players;
    }

    if (strcmp(buf, "BTN") == 0)
        return button_index;
    if (strcmp(buf, "SB") == 0)
        return (button_index + 1) % players;
    if (strcmp(buf, "BB") == 0)
        return (button_index + 2) % players;
    if (strcmp(buf, "CO") == 0)
        return (button_index + players - 1) % players;
    if (strcmp(buf, "HJ") == 0)
        return (button_index + players - 2) % players;
    if (strcmp(buf, "MP") == 0)
        return (button_index + players - 3) % players;

    if (strncmp(buf, "UTG", 3) == 0)
    {
        int offset = 3; /* UTG = first to act */
        if (buf[3] == '+' && isdigit((unsigned char)buf[4]))
            offset += atoi(buf + 4);
        else if (isdigit((unsigned char)buf[3]))
            offset += atoi(buf + 3);
        return (button_index + offset) % players;
    }

    return -1;
}

static void compute_avg_ev(cfr_game_t *game, cfr_storage_t *storage, uint64_t state_key, int num_players, double *out)
{
    if (game->is_terminal(game, state_key, NULL))
    {
        for (int p = 0; p < num_players; ++p)
            out[p] = game->get_utility(game, state_key, p, NULL);
        return;
    }
    int actions[16];
    int n_actions = game->get_actions(game, state_key, actions, 16, NULL);
    if (n_actions <= 0)
    {
        for (int p = 0; p < num_players; ++p)
            out[p] = 0.0;
        return;
    }
    double strategy[16];
    cfr_storage_get_avg_strategy(storage, state_key, n_actions, strategy);

    double accum[MPF_MAX_PLAYERS] = {0};
    double child[MPF_MAX_PLAYERS];

    for (int i = 0; i < n_actions; ++i)
    {
        uint64_t next_state = game->apply_action(game, state_key, actions[i], NULL);
        compute_avg_ev(game, storage, next_state, num_players, child);
        double weight = strategy[i];
        for (int p = 0; p < num_players; ++p)
            accum[p] += weight * child[p];
        free((void *)(uintptr_t)next_state);
    }
    for (int p = 0; p < num_players; ++p)
        out[p] = accum[p];
}

static mpf_street_t parse_street(const char *s)
{
    if (!s)
        return MPF_STREET_PREFLOP;
    if (strcasecmp(s, "preflop") == 0 || strcasecmp(s, "pf") == 0)
        return MPF_STREET_PREFLOP;
    if (strcasecmp(s, "flop") == 0)
        return MPF_STREET_FLOP;
    if (strcasecmp(s, "turn") == 0)
        return MPF_STREET_TURN;
    if (strcasecmp(s, "river") == 0)
        return MPF_STREET_RIVER;
    return MPF_STREET_PREFLOP;
}

int main(int argc, char **argv)
{
    int exit_code = 0;
    int players = 3;
    int iters = 1000;
    int deals = 1;
    unsigned seed = 2025u;
    int progress = 0;
    mpf_rule_t game_rules = MPF_RULE_HOLDEM;
    char *game_str = NULL;
    double sb = 0.5;
    double bb = 1.0;
    double ante = 0.0;
    int button = 0;
    int raise_cap = 3;
    mpf_street_t start_street = MPF_STREET_PREFLOP;
    char *board_str = NULL;
    char *hands_str = NULL;
    char *stacks_str = NULL;
    char *bet_sizes_str = NULL;
    char *hero_label = NULL;
    char *hero_output = NULL;
    int hero_index = -1;
    const char *checkpoint_path = NULL;
    int checkpoint_interval = 0;
    int checkpoint_final = 0;
    const char *resume_path = NULL;
    int monitor_enabled = 0;
    int monitor_period = 0;
    EvalContext *ctx = NULL;
    double pre_invested[MPF_MAX_PLAYERS] = {0};
    double pre_round[MPF_MAX_PLAYERS] = {0};
    int pre_active[MPF_MAX_PLAYERS];
    for (int i = 0; i < MPF_MAX_PLAYERS; ++i)
        pre_active[i] = 1;
    int pre_invested_count = 0;
    int pre_round_count = 0;
    int pre_active_count = 0;
    int pre_invested_flag = 0;
    int pre_round_flag = 0;
    int pre_active_flag = 0;
    double pre_pot = 0.0;
    int pre_pot_flag = 0;
    double pre_to_call = 0.0;
    int pre_to_call_flag = 0;
    double pre_current_bet = 0.0;
    int pre_current_flag = 0;
    int pre_raises = 0;
    int pre_raises_flag = 0;
    char *pre_to_act_label = NULL;
    int pre_to_act_index = -1;
    int bunching_enabled = 0;
    int chance_enabled = 0;
    char *fold_range_str = NULL;
    fold_range_set_t fold_ranges = {{0}};

    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--players") && i + 1 < argc)
            players = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--iters") && i + 1 < argc)
            iters = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--deals") && i + 1 < argc)
            deals = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--progress") && i + 1 < argc)
            progress = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sb") && i + 1 < argc)
            sb = atof(argv[++i]);
        else if (!strcmp(argv[i], "--bb") && i + 1 < argc)
            bb = atof(argv[++i]);
        else if (!strcmp(argv[i], "--ante") && i + 1 < argc)
            ante = atof(argv[++i]);
        else if (!strcmp(argv[i], "--button") && i + 1 < argc)
            button = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--raise-cap") && i + 1 < argc)
            raise_cap = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--game") && i + 1 < argc)
            game_str = argv[++i];
        else if (!strcmp(argv[i], "--street") && i + 1 < argc)
            start_street = parse_street(argv[++i]);
        else if (!strcmp(argv[i], "--board") && i + 1 < argc)
            board_str = argv[++i];
        else if (!strcmp(argv[i], "--hands") && i + 1 < argc)
            hands_str = argv[++i];
        else if (!strcmp(argv[i], "--stacks") && i + 1 < argc)
            stacks_str = argv[++i];
        else if (!strcmp(argv[i], "--bet-sizes") && i + 1 < argc)
            bet_sizes_str = argv[++i];
        else if (!strcmp(argv[i], "--preflop-invested") && i + 1 < argc)
        {
            pre_invested_count = parse_double_list(argv[++i], pre_invested, MPF_MAX_PLAYERS);
            if (pre_invested_count > 0)
                pre_invested_flag = 1;
        }
        else if (!strcmp(argv[i], "--preflop-round") && i + 1 < argc)
        {
            pre_round_count = parse_double_list(argv[++i], pre_round, MPF_MAX_PLAYERS);
            if (pre_round_count > 0)
                pre_round_flag = 1;
        }
        else if (!strcmp(argv[i], "--preflop-active") && i + 1 < argc)
        {
            pre_active_count = parse_int_list(argv[++i], pre_active, MPF_MAX_PLAYERS);
            if (pre_active_count > 0)
                pre_active_flag = 1;
        }
        else if (!strcmp(argv[i], "--preflop-pot") && i + 1 < argc)
        {
            pre_pot = atof(argv[++i]);
            pre_pot_flag = 1;
        }
        else if (!strcmp(argv[i], "--preflop-to-call") && i + 1 < argc)
        {
            pre_to_call = atof(argv[++i]);
            pre_to_call_flag = 1;
        }
        else if (!strcmp(argv[i], "--preflop-current-bet") && i + 1 < argc)
        {
            pre_current_bet = atof(argv[++i]);
            pre_current_flag = 1;
        }
        else if (!strcmp(argv[i], "--preflop-raises") && i + 1 < argc)
        {
            pre_raises = atoi(argv[++i]);
            pre_raises_flag = 1;
        }
        else if (!strcmp(argv[i], "--preflop-to-act") && i + 1 < argc)
            pre_to_act_label = argv[++i];
        else if (!strcmp(argv[i], "--bunching"))
            bunching_enabled = 1;
        else if (!strcmp(argv[i], "--chance"))
            chance_enabled = 1;
        else if (!strcmp(argv[i], "--fold-range") && i + 1 < argc)
            fold_range_str = argv[++i];
        else if (!strcmp(argv[i], "--hero") && i + 1 < argc)
            hero_label = argv[++i];
        else if (!strcmp(argv[i], "--hero-output") && i + 1 < argc)
            hero_output = argv[++i];
        else if (!strcmp(argv[i], "--monitor-hero"))
            monitor_enabled = 1;
        else if (!strcmp(argv[i], "--monitor-period") && i + 1 < argc)
            monitor_period = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--checkpoint") && i + 1 < argc)
            checkpoint_path = argv[++i];
        else if (!strcmp(argv[i], "--checkpoint-interval") && i + 1 < argc)
            checkpoint_interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--checkpoint-final"))
            checkpoint_final = 1;
        else if (!strcmp(argv[i], "--resume") && i + 1 < argc)
            resume_path = argv[++i];
        else if (!strcmp(argv[i], "--help"))
        {
            fprintf(stderr, "Usage: %s [options]\n", argv[0]);
            fprintf(stderr, "  --game holdem|plo4|plo5|plo6|shortdeck\n");
            fprintf(stderr, "  --players N          (max %d)\n", MPF_MAX_PLAYERS);
            fprintf(stderr, "  --iters N\n");
            fprintf(stderr, "  --deals N            (tirages successifs, défaut 1)\n");
            fprintf(stderr, "  --street preflop|flop|turn|river\n");
            fprintf(stderr, "  --board AhKs7d2c5s   (optional, missing cards tirés aléatoirement)\n");
            fprintf(stderr, "  --hands P1=AhKd,P2=QcJd\n");
            fprintf(stderr, "  --stacks 100,100,...\n");
            fprintf(stderr, "  --sb 0.5 --bb 1.0 --ante 0.0 --button 0\n");
            fprintf(stderr, "  --bet-sizes 1.5,3.0  (montants absolus). En PLO, valeurs=fractions du pot\n");
            fprintf(stderr, "  --raise-cap 3\n");
            fprintf(stderr, "  --preflop-invested x,y,...    (montants déjà investis)\n");
            fprintf(stderr, "  --preflop-round x,y,...       (contributions sur la street en cours)\n");
            fprintf(stderr, "  --preflop-active 1,1,0,...    (0 = joueur déjà fold)\n");
            fprintf(stderr, "  --preflop-pot valeur          (pot total au début de la street)\n");
            fprintf(stderr, "  --preflop-to-call valeur\n");
            fprintf(stderr, "  --preflop-current-bet valeur\n");
            fprintf(stderr, "  --preflop-raises N            (relances déjà effectuées)\n");
            fprintf(stderr, "  --preflop-to-act CO|P1...     (joueur à parler en premier)\n");
            fprintf(stderr, "  --hero BTN|CO|P1...\n");
            fprintf(stderr, "  --hero-output fichier.csv (ou '-' pour stdout)\n");
            fprintf(stderr, "  --monitor-hero                (affiche EV moyenne/volatilité du héros)\n");
            fprintf(stderr, "  --monitor-period N            (par défaut = progress ou 20)\n");
            fprintf(stderr, "  --checkpoint state.bin [--checkpoint-interval N --checkpoint-final]\n");
            fprintf(stderr, "  --resume state.bin\n");
            fprintf(stderr, "  --bunching                    (FEAT-14: card-bunching chance deals;\n");
            fprintf(stderr, "                                 auto-enables --chance)\n");
            fprintf(stderr, "  --chance                      (deal turn/river via chance nodes)\n");
            fprintf(stderr, "  --fold-range \"P1:AA,KK,QQ|P2:TT+\" (folded players' preflop ranges;\n");
            fprintf(stderr, "                                 requires --preflop-active to mark the fold)\n");
            fprintf(stderr, "  --seed N --progress K\n");
            return 0;
        }
    }

    if (players < 2 || players > MPF_MAX_PLAYERS)
    {
        fprintf(stderr, "Players must be between 2 and %d\n", MPF_MAX_PLAYERS);
        exit_code = 1;
        goto cleanup;
    }

    if (game_str)
        game_rules = parse_game(game_str);

    EvalConfig cfg_eval = eval_config_holdem();
    if (game_rules == MPF_RULE_SHORTDECK)
        cfg_eval = eval_config_shortdeck();
    ctx = eval_context_create(&cfg_eval);
    if (!ctx)
    {
        fprintf(stderr, "Failed to create eval context\n");
        exit_code = 1;
        goto cleanup;
    }

    button = (button % players + players) % players;

    if (hero_label)
    {
        hero_index = parse_hero_label(hero_label, players, button);
        if (hero_index < 0 || hero_index >= players)
        {
            fprintf(stderr, "Invalid hero seat: %s\n", hero_label);
            exit_code = 1;
            goto cleanup;
        }
    }

    if (monitor_enabled && hero_index < 0)
    {
        fprintf(stderr, "--monitor-hero requires --hero <seat>\n");
        exit_code = 1;
        goto cleanup;
    }

    if (pre_to_act_label)
    {
        pre_to_act_index = parse_hero_label(pre_to_act_label, players, button);
        if (pre_to_act_index < 0 || pre_to_act_index >= players)
        {
            fprintf(stderr, "Invalid preflop to-act seat: %s\n", pre_to_act_label);
            exit_code = 1;
            goto cleanup;
        }
    }

    mpf_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ctx = ctx;
    cfg.rules = game_rules;
    cfg.num_players = players;
    cfg.sb = sb;
    cfg.bb = bb;
    cfg.ante = ante;
    cfg.button_index = button;
    cfg.start_street = start_street;
    cfg.raise_cap = raise_cap;
    cfg.enable_pot_sizing = 0;
    cfg.enable_chance_nodes = chance_enabled ? 1 : 0;

    mpf_preflop_cfg_t *pre_cfg = &cfg.preflop;
    memset(pre_cfg, 0, sizeof(*pre_cfg));
    if (pre_invested_flag || pre_round_flag || pre_active_flag || pre_pot_flag || pre_to_call_flag || pre_current_flag || pre_raises_flag || (pre_to_act_index >= 0))
    {
        pre_cfg->defined = 1;
        if (pre_invested_flag)
        {
            pre_cfg->has_invested = 1;
            for (int i = 0; i < players; ++i)
                pre_cfg->invested[i] = (i < pre_invested_count) ? pre_invested[i] : 0.0;
        }
        if (pre_round_flag)
        {
            pre_cfg->has_round = 1;
            for (int i = 0; i < players; ++i)
                pre_cfg->round_contrib[i] = (i < pre_round_count) ? pre_round[i] : 0.0;
        }
        if (pre_active_flag)
        {
            pre_cfg->has_active = 1;
            for (int i = 0; i < players; ++i)
                pre_cfg->active[i] = (i < pre_active_count) ? (pre_active[i] != 0) : 1;
        }
        else
        {
            for (int i = 0; i < players; ++i)
                pre_cfg->active[i] = 1;
        }
        if (pre_pot_flag)
        {
            pre_cfg->has_pot = 1;
            pre_cfg->pot = pre_pot;
        }
        if (pre_to_call_flag)
        {
            pre_cfg->has_to_call = 1;
            pre_cfg->to_call = pre_to_call;
        }
        if (pre_current_flag)
        {
            pre_cfg->has_current_bet = 1;
            pre_cfg->current_bet = pre_current_bet;
        }
        if (pre_raises_flag)
        {
            pre_cfg->has_raises = 1;
            pre_cfg->raises_made = pre_raises;
        }
        if (pre_to_act_index >= 0)
        {
            pre_cfg->has_to_act = 1;
            pre_cfg->to_act = pre_to_act_index;
        }
    }
    else
    {
        for (int i = 0; i < players; ++i)
            pre_cfg->active[i] = 1;
    }

    for (int i = 0; i < players; ++i)
        cfg.stacks[i] = 100.0;
    if (stacks_str)
    {
        char *dup = strdup(stacks_str);
        char *tok = strtok(dup, ",");
        int idx = 0;
        while (tok && idx < players)
        {
            cfg.stacks[idx++] = atof(tok);
            tok = strtok(NULL, ",");
        }
        free(dup);
    }

    if (bet_sizes_str)
    {
        char *dup = strdup(bet_sizes_str);
        char *tok = strtok(dup, ",");
        while (tok && cfg.bet_size_count_common < MPF_MAX_BET_SIZES)
        {
            cfg.bet_sizes_common[cfg.bet_size_count_common++] = atof(tok);
            tok = strtok(NULL, ",");
        }
        free(dup);
    }
    else
    {
        if (game_rules == MPF_RULE_PLO4 || game_rules == MPF_RULE_PLO5 || game_rules == MPF_RULE_PLO6)
        {
            cfg.enable_pot_sizing = 1;
            cfg.bet_sizes_common[cfg.bet_size_count_common++] = 0.5; /* demi-pot */
            cfg.bet_sizes_common[cfg.bet_size_count_common++] = 1.0; /* pot */
        }
        else
        {
            cfg.bet_sizes_common[cfg.bet_size_count_common++] = 1.5;
            cfg.bet_sizes_common[cfg.bet_size_count_common++] = 3.0;
        }
    }


    int hole_cards_per_player = hole_cards_required(game_rules);

    mask_t base_hole_masks[MPF_MAX_PLAYERS];
    int base_hole_specified[MPF_MAX_PLAYERS];
    for (int i = 0; i < players; ++i)
    {
        base_hole_masks[i] = MASK_EMPTY;
        base_hole_specified[i] = 0;
    }

    bool base_used[52] = {0};
    if (hands_str)
    {
        char *dup = strdup(hands_str);
        char *tok = strtok(dup, ",");
        while (tok)
        {
            char *eq = strchr(tok, '=');
            if (eq)
            {
                *eq = '\0';
                int seat = parse_hero_label(tok, players, button);
                if (seat < 0 && toupper((unsigned char)tok[0]) == 'P' && isdigit((unsigned char)tok[1]))
                    seat = atoi(tok + 1) - 1;
                if (seat >= 0 && seat < players)
                {
                    const char *cards = eq + 1;
                    size_t len = strlen(cards);
                    if (len >= (size_t)(hole_cards_per_player * 2))
                    {
                        mask_t m = MASK_EMPTY;
                        int ok = 1;
                        for (int k = 0; k < hole_cards_per_player; ++k)
                        {
                            int idx = k * 2;
                            if (idx + 1 >= (int)len)
                            {
                                ok = 0;
                                break;
                            }
                            int card = card_from_token(cards + idx);
                            if (card < 0 || base_used[card] || mask_is_set(m, card) ||
                                (game_rules == MPF_RULE_SHORTDECK && !is_shortdeck_card(card)))
                            {
                                ok = 0;
                                break;
                            }
                            mask_add_card(&m, card);
                            base_used[card] = true;
                        }
                        if (ok)
                        {
                            base_hole_masks[seat] = m;
                            base_hole_specified[seat] = 1;
                        }
                    }
                }
            }
            tok = strtok(NULL, ",");
        }
        free(dup);
    }

    int base_board_cards[5] = {-1, -1, -1, -1, -1};
    int base_board_count = 0;
    if (board_str)
    {
        base_board_count = parse_cards_list(board_str, 5, base_board_cards);
        for (int i = 0; i < base_board_count; ++i)
        {
            int card = base_board_cards[i];
            if (card < 0 || card >= 52 || base_used[card] ||
                (game_rules == MPF_RULE_SHORTDECK && !is_shortdeck_card(card)))
            {
                fprintf(stderr, "Invalid or duplicate board card in --board\n");
                exit_code = 1;
                goto cleanup;
            }
            base_used[card] = true;
        }
    }

    /* FEAT-14 (#150): folded-range card bunching validation. */
    memset(&fold_ranges, 0, sizeof(fold_ranges));
    if (bunching_enabled || fold_range_str)
    {
        char err[256];
        err[0] = '\0';
        if (!fold_range_str)
        {
            fprintf(stderr, "--bunching requires --fold-range \"P1:AA,...|P2:...\"\n");
            exit_code = 1;
            goto cleanup;
        }
        if (!pre_active_flag)
        {
            fprintf(stderr, "--bunching requires --preflop-active to mark the folded seats\n");
            exit_code = 1;
            goto cleanup;
        }
        if (parse_fold_ranges(fold_range_str, players, game_rules, &fold_ranges, err, sizeof(err)) != 0)
        {
            fprintf(stderr, "--fold-range: %s\n", err[0] ? err : "parse failed");
            exit_code = 1;
            goto cleanup;
        }
        int provided_count = 0;
        for (int i = 0; i < players; ++i)
        {
            if (!fold_ranges.provided[i])
                continue;
            if (pre_active[i] != 0)
            {
                fprintf(stderr, "--fold-range seat P%d must be folded (set 0 in --preflop-active)\n", i + 1);
                exit_code = 1;
                goto cleanup;
            }
            if (base_hole_specified[i])
            {
                fprintf(stderr, "--fold-range seat P%d must NOT have a specified hole in --hands "
                                "(its cards are modeled by the range)\n", i + 1);
                exit_code = 1;
                goto cleanup;
            }
            provided_count++;
        }
        if (provided_count < 2)
        {
            fprintf(stderr, "--bunching models preflop folds: provide ranges for at least 2 folded seats\n");
            exit_code = 1;
            goto cleanup;
        }
        fprintf(stderr, "[bunching] enabled, range-modeled folded seats:");
        for (int i = 0; i < players; ++i)
            if (fold_ranges.provided[i])
                fprintf(stderr, " P%d", i + 1);
        fprintf(stderr, "\n");
    }

    mpf_config_t base_cfg = cfg;
    for (int i = 0; i < players; ++i)
    {
        base_cfg.hole[i] = MASK_EMPTY;
        base_cfg.hole_specified[i] = 0;
    }
    base_cfg.enable_card_bunching = bunching_enabled ? 1 : 0;
    if (bunching_enabled)
        base_cfg.enable_chance_nodes = 1; /* bunching weights only apply to chance deals */
    for (int i = 0; i < players; ++i)
    {
        base_cfg.folded_range_provided[i] = fold_ranges.provided[i] ? 1 : 0;
        for (int c = 0; c < 52; ++c)
            base_cfg.folded_range_prob[i][c] = fold_ranges.prob[i][c];
    }

    double ev_sum[MPF_MAX_PLAYERS];
    for (int i = 0; i < MPF_MAX_PLAYERS; ++i)
        ev_sum[i] = 0.0;
    int deals_completed = 0;
    double total_time = 0.0;

    hero_dump_t hero_total;
    int hero_dump_ready = 0;
    if (hero_index >= 0)
    {
        hero_dump_init(&hero_total, hero_index, base_cfg.bet_size_count_common);
        hero_dump_ready = 1;
    }

    #ifdef _WIN32
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    #else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    #endif

    if (setup_ctrlw_listener() == -1)
        fprintf(stderr, "[ctrl] warning: unable to install Ctrl+W listener\n");
    else if (g_ctrlw_thread_running)
        fprintf(stderr, "[ctrl] Ctrl+W pour demander un arrêt propre (Ctrl+C en secours)\n");

    for (int deal = 0; deal < deals && !g_force_exit; ++deal)
    {
        if (g_stop_flag && deal > 0)
            break;

        mpf_config_t run_cfg = base_cfg;
        bool used[52] = {0};
        unsigned local_seed = seed + (unsigned)(deal * 97u);

        for (int i = 0; i < players; ++i)
        {
            run_cfg.hole[i] = MASK_EMPTY;
            run_cfg.hole_specified[i] = 0;
            if (base_hole_specified[i])
            {
                run_cfg.hole[i] = base_hole_masks[i];
                run_cfg.hole_specified[i] = 1;
                mark_used_from_mask(used, base_hole_masks[i]);
            }
        }

        /* FEAT-14 (#150): range-modeled folded seats keep an unknown hole
           (hole_specified stays 0); their cards are modeled by the fold range
           and must not be burned from the stub before the chance deals. */
        for (int i = 0; i < players; ++i)
        {
            if (fold_ranges.provided[i])
            {
                run_cfg.hole[i] = MASK_EMPTY;
                run_cfg.hole_specified[i] = 0;
            }
        }

        for (int i = 0; i < players; ++i)
        {
            if (fold_ranges.provided[i])
                continue; /* folded seats modeled by their range: no hole dealt */
            int current_cards[6];
            int have = mask_to_array_local(run_cfg.hole[i], current_cards, 6);
            int need = hole_cards_per_player - have;
            if (need > 0)
            {
                int new_cards[6];
                deal_random_cards(used, new_cards, need, &local_seed, game_rules == MPF_RULE_SHORTDECK);
                for (int k = 0; k < need; ++k)
                    mask_add_card(&run_cfg.hole[i], new_cards[k]);
                run_cfg.hole_specified[i] = 1;
            }
        }

        int board_cards_run[5];
        int board_count_run = 0;
        for (int i = 0; i < base_board_count && i < 5; ++i)
        {
            board_cards_run[board_count_run++] = base_board_cards[i];
            if (base_board_cards[i] >= 0)
                used[base_board_cards[i]] = true;
        }
        if (board_count_run < 5)
        {
            deal_random_cards(used, board_cards_run + board_count_run, 5 - board_count_run, &local_seed,
                              game_rules == MPF_RULE_SHORTDECK);
            board_count_run = 5;
        }
        run_cfg.board_card_count = 5;
        for (int i = 0; i < 5; ++i)
            run_cfg.board_cards[i] = board_cards_run[i];

        cfr_config_t ccfg = (cfr_config_t){0};
        ccfg.max_iterations = iters;
        ccfg.progress_interval = progress;
        if (checkpoint_path)
            ccfg.checkpoint_path = checkpoint_path;
        if (checkpoint_interval > 0)
            ccfg.checkpoint_interval = checkpoint_interval;
        if (checkpoint_final)
            ccfg.checkpoint_final = 1;
        if (resume_path)
            ccfg.resume_path = resume_path;
        monitor_state_t monitor_state = {0};
        if (monitor_enabled)
        {
            monitor_state.enabled = 1;
            monitor_state.hero_index = hero_index;
            monitor_state.num_players = players;
            monitor_state.root_state = NULL;
            monitor_state.mean = 0.0;
            monitor_state.m2 = 0.0;
            monitor_state.count = 0;
            ccfg.monitor_fn = monitor_callback;
            ccfg.monitor_user = &monitor_state;
            ccfg.monitor_period = (monitor_period > 0) ? monitor_period : ((progress > 0) ? progress : 20);
        }
        ccfg.stop_flag = &g_stop_flag;

        cfr_storage_t *storage_local = cfr_storage_create();
        if (!storage_local)
        {
            fprintf(stderr, "Storage alloc failed\n");
            exit_code = 1;
            break;
        }

        cfr_game_t game;
        mpf_state_t root_state;
        if (mpf_build_game(&run_cfg, &game, &root_state) != 0)
        {
            fprintf(stderr, "Failed to build game\n");
            cfr_storage_destroy(storage_local);
            exit_code = 1;
            break;
        }

        if (monitor_enabled)
            monitor_state.root_state = &root_state;

        g_stop_flag = 0;
        g_force_exit = 0;
        clock_t start = clock();
        cfr_solve(&game, storage_local, &ccfg, NULL);
        clock_t end = clock();

        if (g_stop_flag || g_force_exit)
        {
            cfr_storage_destroy(storage_local);
            if (resume_path)
                resume_path = NULL;
            break;
        }

        double deal_time = (double)(end - start) / CLOCKS_PER_SEC;
        total_time += deal_time;
        fprintf(stderr, "[deal %d/%d] %.3fs\n", deal + 1, deals, deal_time);

        double ev[MPF_MAX_PLAYERS] = {0};
        compute_avg_ev(&game, storage_local, (uint64_t)(uintptr_t)&root_state, players, ev);
        deals_completed++;
        for (int p = 0; p < players; ++p)
            ev_sum[p] += ev[p];

        printf("Deal %d/%d EV:\n", deal + 1, deals);
        for (int p = 0; p < players; ++p)
            printf("  Joueur %d  EV=%.4f\n", p + 1, ev[p]);

        if (monitor_enabled && monitor_state.count > 0)
        {
            double variance = (monitor_state.count > 1) ? (monitor_state.m2 / (monitor_state.count - 1)) : 0.0;
            double stdev = (variance > 0.0) ? sqrt(variance) : 0.0;
            fprintf(stderr, "[monitor] hero mean=%.6f stdev=%.6f samples=%d\n",
                    monitor_state.mean, stdev, monitor_state.count);
        }

        if (hero_dump_ready && !g_stop_flag && !g_force_exit)
            cfr_storage_iterate(storage_local, hero_dump_iter, &hero_total);

        cfr_storage_destroy(storage_local);

        if (resume_path)
            resume_path = NULL;

        if (g_stop_flag || g_force_exit)
            break;
    }

    if (hero_dump_ready && !g_stop_flag && !g_force_exit)
    {
        if (hero_dump_write(&hero_total, hero_output ? hero_output : "-") == 0 && hero_output && strcmp(hero_output, "-") != 0)
            fprintf(stderr, "Hero actions written to %s\n", hero_output);
        hero_dump_free(&hero_total);
    }
    else if (hero_dump_ready)
    {
        hero_dump_free(&hero_total);
    }

    if ((g_force_exit || g_stop_flag) && exit_code == 0 && deals_completed < deals)
        exit_code = 1;

    if (g_force_exit || g_stop_flag)
        goto cleanup;

    if (deals_completed > 0)
    {
        printf("\nRésumé (%d/%d deals) :\n", deals_completed, deals);
        for (int p = 0; p < players; ++p)
            printf("Joueur %d  stack=%.2f  EV=%.4f\n", p + 1,
                   cfg.stacks[p], ev_sum[p] / deals_completed);
        printf("Temps total %.3fs\n", total_time);
    }
    else
    {
        printf("Aucun deal terminé.\n");
    }

cleanup:
    if (ctx)
        eval_context_destroy(ctx);
    g_stop_flag = 1;
    restore_terminal();
    return exit_code;
}
