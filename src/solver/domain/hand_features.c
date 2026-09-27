/*
 * hand_features.c - PLO4/PLO5/PLO6 hand features and strategy buckets
 * (issue #238). See <poker_eval/solver/pe_hand_features.h>.
 *
 * Two evaluation paths, both under the Omaha two-plus-three rule:
 *
 *   - the made hand is the best HandVal over every two-hole, three-board
 *     five-card hand, from the legacy StdDeck evaluator;
 *   - draws are worked out on ranks and suits. A straight needs five
 *     consecutive ranks of which exactly two come from the hole and three from
 *     the board, which reduces to: the window's ranks missing from the board
 *     are at most two, all held, and the hole holds at least two window ranks.
 *     A flush needs two hole and three board cards of one suit.
 */

#include <poker_eval/solver/pe_hand_features.h>

#include <math.h>
#include <string.h>

#include <poker_eval/core/eval.h>
#include <poker_eval/core/handval.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/rules_std.h>

#define HF_RANKS 13
#define HF_SUITS 4
#define HF_DECK 52
#define HF_ACE 12

/* ------------------------------------------------------------------ *
 * Cards and ranks
 * ------------------------------------------------------------------ */

static int hf_rank(int card) { return card % HF_RANKS; }
static int hf_suit(int card) { return card / HF_RANKS; }

/* mask_t suits are clubs, diamonds, hearts, spades; StdDeck numbers them
   differently. Only consistency matters to the evaluator, but keep them the
   same suits anyway. */
static StdDeck_CardMask hf_std_card(int card)
{
    static const int std_suit[HF_SUITS] = {StdDeck_Suit_CLUBS,
                                           StdDeck_Suit_DIAMONDS,
                                           StdDeck_Suit_HEARTS,
                                           StdDeck_Suit_SPADES};
    return StdDeck_MASK(StdDeck_MAKE_CARD(hf_rank(card), std_suit[hf_suit(card)]));
}

static int hf_popcount16(unsigned v)
{
    int n = 0;
    while (v)
    {
        v &= v - 1u;
        ++n;
    }
    return n;
}

static int hf_cards(mask_t mask, int *out, int cap)
{
    int n = 0;
    for (int card = 0; card < HF_DECK; ++card)
    {
        if (mask_is_set(mask, card))
        {
            if (n >= cap)
                return -1;
            out[n++] = card;
        }
    }
    return n;
}

/* The five ranks of the straight topped by `top` (3 = five-high wheel). */
static unsigned hf_window(int top)
{
    if (top == 3)
        return (1u << HF_ACE) | 0xFu; /* A 2 3 4 5 */
    return 0x1Fu << (top - 4);
}

/* Highest straight the hole ranks make with the board ranks under
   two-plus-three, or -1. */
static int hf_hero_straight_top(unsigned hole_ranks, unsigned board_ranks)
{
    for (int top = HF_ACE; top >= 3; --top)
    {
        unsigned w = hf_window(top);
        unsigned missing = w & ~board_ranks;
        if (hf_popcount16(missing) > 2)
            continue;
        if (missing & ~hole_ranks)
            continue;
        if (hf_popcount16(w & hole_ranks) < 2)
            continue;
        return top;
    }
    return -1;
}

/* Highest straight any two cards make with these board ranks, or -1; its
   ranks missing from the board go to *need. */
static int hf_nut_straight_top(unsigned board_ranks, unsigned *need)
{
    for (int top = HF_ACE; top >= 3; --top)
    {
        unsigned w = hf_window(top);
        if (hf_popcount16(w & board_ranks) >= 3)
        {
            if (need)
                *need = w & ~board_ranks;
            return top;
        }
    }
    if (need)
        *need = 0;
    return -1;
}

/* Best HandVal over every two-hole, three-board five-card hand. */
static HandVal hf_best_omaha(const int *hole, int nh, const int *board, int nb)
{
    HandVal best = HandVal_NOTHING;
    int found = 0;
    for (int i = 0; i < nh; ++i)
        for (int j = i + 1; j < nh; ++j)
        {
            StdDeck_CardMask two;
            StdDeck_CardMask_OR(two, hf_std_card(hole[i]), hf_std_card(hole[j]));
            for (int a = 0; a < nb; ++a)
                for (int b = a + 1; b < nb; ++b)
                    for (int c = b + 1; c < nb; ++c)
                    {
                        StdDeck_CardMask five = two;
                        StdDeck_CardMask_OR(five, five, hf_std_card(board[a]));
                        StdDeck_CardMask_OR(five, five, hf_std_card(board[b]));
                        StdDeck_CardMask_OR(five, five, hf_std_card(board[c]));
                        HandVal v = StdDeck_StdRules_EVAL_N(five, 5);
                        if (!found || v > best)
                        {
                            best = v;
                            found = 1;
                        }
                    }
        }
    return best;
}

/* ------------------------------------------------------------------ *
 * Board
 * ------------------------------------------------------------------ */

pe_solver_status_t pe_hf_board_prepare(mask_t board, pe_hf_board_t *out)
{
    int cards[5];
    int n;

    if (!out)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->top_rank = out->second_rank = out->bottom_rank = -1;
    out->nut_straight_top = -1;

    if (board >> HF_DECK)
        return PE_SOLVER_ERR_INVALID_CONFIG;
    n = hf_cards(board, cards, 5);
    if (n < 0 || n == 1 || n == 2)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    out->board = board;
    out->card_count = (uint8_t)n;
    for (int i = 0; i < n; ++i)
    {
        out->rank_count[hf_rank(cards[i])]++;
        out->suit_count[hf_suit(cards[i])]++;
    }
    for (int r = HF_ACE; r >= 0; --r)
    {
        if (!out->rank_count[r])
            continue;
        out->rank_mask |= (uint16_t)(1u << r);
        if (out->distinct_ranks == 0)
            out->top_rank = (int8_t)r;
        else if (out->distinct_ranks == 1)
            out->second_rank = (int8_t)r;
        out->bottom_rank = (int8_t)r;
        out->distinct_ranks++;
        if (out->rank_count[r] >= 2)
            out->paired = 1;
    }
    for (int s = 0; s < HF_SUITS; ++s)
    {
        if (out->suit_count[s] > out->max_suit)
            out->max_suit = out->suit_count[s];
        /* The highest card of the suit not on the board. */
        out->nut_flush_card[s] = -1;
        for (int r = HF_ACE; r >= 0; --r)
        {
            int card = r + HF_RANKS * s;
            if (!mask_is_set(board, card))
            {
                out->nut_flush_card[s] = (int8_t)card;
                break;
            }
        }
    }
    out->monotone = n >= 3 && out->max_suit == n;
    out->flush_possible = out->max_suit >= 3;

    if (n < 3)
        return PE_SOLVER_OK;

    for (int top = HF_ACE; top >= 3; --top)
        if (hf_popcount16(hf_window(top) & out->rank_mask) >= 3)
            out->straight_windows++;
    {
        unsigned need = 0;
        out->nut_straight_top =
            (int8_t)hf_nut_straight_top(out->rank_mask, &need);
        out->nut_straight_need = (uint16_t)need;
    }

    /* The absolute nuts: every pair of cards off the board. */
    {
        HandVal best = HandVal_NOTHING;
        int found = 0;
        for (int c1 = 0; c1 < HF_DECK; ++c1)
        {
            if (mask_is_set(board, c1))
                continue;
            for (int c2 = c1 + 1; c2 < HF_DECK; ++c2)
            {
                int two[2] = {c1, c2};
                HandVal v;
                if (mask_is_set(board, c2))
                    continue;
                v = hf_best_omaha(two, 2, cards, n);
                if (!found || v > best)
                {
                    best = v;
                    found = 1;
                }
            }
        }
        out->nut_value = best;
    }
    return PE_SOLVER_OK;
}

/* ------------------------------------------------------------------ *
 * Hand
 * ------------------------------------------------------------------ */

static void hf_private_structure(const int *hole, int nh, pe_hand_features_t *f,
                                 uint8_t *rank_count, uint8_t *suit_count,
                                 unsigned *rank_mask)
{
    unsigned mask = 0;
    uint8_t shape[HF_SUITS];

    for (int i = 0; i < nh; ++i)
    {
        rank_count[hf_rank(hole[i])]++;
        suit_count[hf_suit(hole[i])]++;
        if (hf_rank(hole[i]) >= 8) /* ten and above */
            f->broadway_count++;
    }
    for (int r = 0; r < HF_RANKS; ++r)
    {
        if (!rank_count[r])
            continue;
        mask |= 1u << r;
        f->distinct_ranks++;
        f->highest_rank = (uint8_t)r;
        if (rank_count[r] == 2)
            f->paired_ranks++;
        else if (rank_count[r] >= 3)
            f->trips_ranks++;
    }
    *rank_mask = mask;

    /* Missing ranks inside the tightest span, the ace high or low. */
    if (f->distinct_ranks >= 2)
    {
        int lo = -1, hi = -1;
        for (int r = 0; r < HF_RANKS; ++r)
            if (mask & (1u << r))
            {
                if (lo < 0)
                    lo = r;
                hi = r;
            }
        int gaps = (hi - lo + 1) - f->distinct_ranks;
        if (mask & (1u << HF_ACE))
        {
            /* ace as the lowest rank: bit 0 is the ace, bit r+1 is rank r */
            unsigned low = ((mask & ~(1u << HF_ACE)) << 1) | 1u;
            int lo2 = -1, hi2 = -1;
            for (int r = 0; r < HF_RANKS; ++r)
                if (low & (1u << r))
                {
                    if (lo2 < 0)
                        lo2 = r;
                    hi2 = r;
                }
            int gaps2 = (hi2 - lo2 + 1) - f->distinct_ranks;
            if (gaps2 < gaps)
                gaps = gaps2;
        }
        f->rank_gaps = (uint8_t)gaps;
        f->connectivity = gaps == 0   ? PE_HF_CONNECT_RUNDOWN
                          : gaps == 1 ? PE_HF_CONNECT_HIGH
                          : gaps <= 3 ? PE_HF_CONNECT_MEDIUM
                                      : PE_HF_CONNECT_LOW;
    }
    else
    {
        f->connectivity = PE_HF_CONNECT_NONE;
    }

    /* Runs of consecutive ranks, the ace high... */
    f->rank_components = (uint8_t)hf_popcount16(mask & ~(mask << 1));
    /* ...and the longest run with the ace at both ends. */
    {
        unsigned m14 = (mask << 1) | ((mask >> HF_ACE) & 1u);
        int run = 0, best = 0;
        for (int b = 0; b < 14; ++b)
        {
            run = (m14 & (1u << b)) ? run + 1 : 0;
            if (run > best)
                best = run;
        }
        f->longest_run = (uint8_t)best;
    }

    memcpy(shape, suit_count, sizeof(shape));
    for (int i = 0; i < HF_SUITS; ++i)
        for (int j = i + 1; j < HF_SUITS; ++j)
            if (shape[j] > shape[i])
            {
                uint8_t t = shape[i];
                shape[i] = shape[j];
                shape[j] = t;
            }
    memcpy(f->suit_shape, shape, sizeof(shape));
    for (int s = 0; s < HF_SUITS; ++s)
        if (suit_count[s] >= 2)
            f->suited_groups++;
}

static uint8_t hf_detail(const pe_hf_board_t *b, HandVal hv)
{
    const int type = (int)HandVal_HANDTYPE(hv);
    const int top = (int)HandVal_TOP_CARD(hv);

    if (type == StdRules_HandType_ONEPAIR)
    {
        const int on_board = b->rank_count[top];
        if (on_board == 0) /* the pair is two hole cards */
            return top > b->top_rank ? PE_HF_DETAIL_OVERPAIR
                                     : PE_HF_DETAIL_UNDERPAIR;
        if (on_board >= 2)
            return PE_HF_DETAIL_BOARD_PAIR;
        if (top == b->top_rank)
            return PE_HF_DETAIL_TOP_PAIR;
        if (top == b->second_rank)
            return PE_HF_DETAIL_SECOND_PAIR;
        return PE_HF_DETAIL_LOW_PAIR;
    }
    if (type == StdRules_HandType_TWOPAIR)
    {
        const int second = (int)HandVal_SECOND_CARD(hv);
        if (top == b->top_rank && second == b->second_rank &&
            b->rank_count[top] == 1 && b->rank_count[second] == 1)
            return PE_HF_DETAIL_TOP_TWO_PAIR;
        return PE_HF_DETAIL_NONE;
    }
    if (type == StdRules_HandType_TRIPS)
    {
        const int on_board = b->rank_count[top];
        if (on_board == 1) /* a pocket pair matching the board: a set */
            return top == b->top_rank      ? PE_HF_DETAIL_TOP_SET
                   : top == b->bottom_rank ? PE_HF_DETAIL_BOTTOM_SET
                                           : PE_HF_DETAIL_MIDDLE_SET;
        if (on_board == 2)
            return PE_HF_DETAIL_TRIPS;
        return PE_HF_DETAIL_BOARD_TRIPS;
    }
    return PE_HF_DETAIL_NONE;
}

static void hf_evaluate(const pe_hf_board_t *b, const int *hole, int nh,
                        const uint8_t *hole_rank_count,
                        const uint8_t *hole_suit_count, unsigned hole_ranks,
                        pe_hand_features_t *f)
{
    int board_cards[5];
    const int nb = hf_cards(b->board, board_cards, 5);
    const HandVal hv = hf_best_omaha(hole, nh, board_cards, nb);
    const int made = (int)HandVal_HANDTYPE(hv);

    f->evaluated = 1;
    f->made = (uint8_t)made;
    f->made_value = hv;
    f->detail = hf_detail(b, hv);
    f->is_nuts = hv == b->nut_value;

    /* The made flush's suit: the only one with three board cards. */
    if (made == StdRules_HandType_FLUSH)
    {
        for (int s = 0; s < HF_SUITS; ++s)
            if (b->suit_count[s] >= 3 && hole_suit_count[s] >= 2)
            {
                for (int i = 0; i < nh; ++i)
                    if (hole[i] == b->nut_flush_card[s])
                        f->nut_flush = 1;
            }
    }

    /* Draws: only while cards are to come, and only for hands a straight
       or a flush would still improve. */
    if (b->card_count < 5 && made <= StdRules_HandType_STRAIGHT)
    {
        int draws = 0;
        for (int s = 0; s < HF_SUITS; ++s)
        {
            uint8_t kind = PE_HF_FLUSH_DRAW_NONE;
            if (hole_suit_count[s] < 2)
                continue;
            if (b->suit_count[s] == 2)
            {
                int nut = 0;
                for (int i = 0; i < nh; ++i)
                    if (hole[i] == b->nut_flush_card[s])
                        nut = 1;
                kind = nut ? PE_HF_FLUSH_DRAW_NUT : PE_HF_FLUSH_DRAW;
                draws++;
            }
            else if (b->card_count == 3 && b->suit_count[s] == 1)
            {
                kind = PE_HF_FLUSH_DRAW_BACKDOOR;
            }
            if (kind > f->flush_draw)
                f->flush_draw = kind;
        }
        f->double_flush_draw = draws >= 2;

        {
            const int current = hf_hero_straight_top(hole_ranks, b->rank_mask);
            int out_ranks = 0;
            for (int x = 0; x < HF_RANKS; ++x)
            {
                const int left = 4 - hole_rank_count[x] - b->rank_count[x];
                unsigned next_board;
                int top;
                if (left <= 0)
                    continue;
                next_board = b->rank_mask | (1u << x);
                top = hf_hero_straight_top(hole_ranks, next_board);
                if (top <= current)
                    continue;
                out_ranks++;
                f->straight_outs = (uint8_t)(f->straight_outs + left);
                if (top == hf_nut_straight_top(next_board, NULL))
                    f->straight_draw_to_nuts = 1;
            }
            f->straight_draw = out_ranks == 0   ? PE_HF_STRAIGHT_DRAW_NONE
                               : out_ranks == 1 ? PE_HF_STRAIGHT_DRAW_GUTSHOT
                               : out_ranks == 2 ? PE_HF_STRAIGHT_DRAW_OPEN_ENDED
                                                : PE_HF_STRAIGHT_DRAW_WRAP;
        }
        f->combo_draw = f->flush_draw >= PE_HF_FLUSH_DRAW &&
                        f->straight_draw >= PE_HF_STRAIGHT_DRAW_OPEN_ENDED;
        f->redraw = made == StdRules_HandType_STRAIGHT &&
                    (f->flush_draw >= PE_HF_FLUSH_DRAW ||
                     f->straight_draw != PE_HF_STRAIGHT_DRAW_NONE);
    }

    /* Blockers. */
    for (int s = 0; s < HF_SUITS; ++s)
    {
        const int live = b->suit_count[s] >= 3 ||
                         (b->suit_count[s] == 2 && b->card_count < 5);
        if (!live)
            continue;
        for (int i = 0; i < nh; ++i)
            if (hole[i] == b->nut_flush_card[s])
                f->blockers |= PE_HF_BLOCKS_NUT_FLUSH;
    }
    if (b->nut_straight_top >= 0 && (hole_ranks & b->nut_straight_need))
        f->blockers |= PE_HF_BLOCKS_NUT_STRAIGHT;
    for (int r = 0; r < HF_RANKS; ++r)
        if (b->rank_count[r] >= 2 && hole_rank_count[r])
            f->blockers |= PE_HF_BLOCKS_BOARD_PAIR;
    if (b->top_rank >= 0 && hole_rank_count[b->top_rank])
        f->blockers |= PE_HF_BLOCKS_TOP_CARD;
}

pe_solver_status_t pe_hand_features_compute(const pe_hf_board_t *board,
                                            mask_t hole,
                                            pe_hand_features_t *out)
{
    int cards[PE_HF_MAX_HOLE];
    uint8_t rank_count[HF_RANKS] = {0};
    uint8_t suit_count[HF_SUITS] = {0};
    unsigned rank_mask = 0;
    int nh;

    if (!board || !out)
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if ((hole >> HF_DECK) || (hole & board->board))
        return PE_SOLVER_ERR_INVALID_CONFIG;
    nh = hf_cards(hole, cards, PE_HF_MAX_HOLE);
    if (nh < (int)PE_HF_MIN_HOLE)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    out->hole_count = (uint8_t)nh;
    hf_private_structure(cards, nh, out, rank_count, suit_count, &rank_mask);
    if (board->card_count >= 3)
        hf_evaluate(board, cards, nh, rank_count, suit_count, rank_mask, out);
    return PE_SOLVER_OK;
}

pe_solver_status_t pe_hand_features_compute_batch(const pe_hf_board_t *board,
                                                  const mask_t *holes,
                                                  size_t count,
                                                  pe_hand_features_t *out,
                                                  size_t *out_failed)
{
    if (!board || (count && (!holes || !out)))
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    for (size_t i = 0; i < count; ++i)
    {
        pe_solver_status_t st = pe_hand_features_compute(board, holes[i], &out[i]);
        if (st != PE_SOLVER_OK)
        {
            if (out_failed)
                *out_failed = i;
            return st;
        }
    }
    return PE_SOLVER_OK;
}

/* ------------------------------------------------------------------ *
 * Keys
 * ------------------------------------------------------------------ */

/* Bit layout of the packed values; the selected dimensions sit above. */
enum {
    K_MADE = 0,        /* 4 bits */
    K_DETAIL = 4,      /* 4 bits */
    K_NUTS = 8,        /* 1 bit */
    K_FLUSH = 9,       /* 2 bits */
    K_STRAIGHT = 11,   /* 2 bits */
    K_COMBO = 13,      /* 1 bit */
    K_REDRAW = 14,     /* 1 bit */
    K_BLOCKERS = 15,   /* 4 bits */
    K_CONNECT = 19,    /* 3 bits */
    K_SUITS = 22,      /* 4 x 3 bits */
    K_PAIRS = 34,      /* 2 + 2 bits */
    K_DIMS = 48
};

static uint64_t hf_field(uint64_t key, int shift, int bits)
{
    return (key >> shift) & ((UINT64_C(1) << bits) - 1u);
}

uint64_t pe_hf_key(const pe_hand_features_t *f, uint32_t dims)
{
    uint64_t key;
    const int ev = f && f->evaluated;

    dims &= PE_HF_DIM_ALL;
    key = (uint64_t)dims << K_DIMS;
    if (!f)
        return key;
    if ((dims & PE_HF_DIM_MADE) && ev)
        key |= (uint64_t)(f->made & 0xFu) << K_MADE;
    if ((dims & PE_HF_DIM_DETAIL) && ev)
        key |= (uint64_t)(f->detail & 0xFu) << K_DETAIL;
    if ((dims & PE_HF_DIM_NUTS) && ev)
        key |= (uint64_t)(f->is_nuts & 1u) << K_NUTS;
    if ((dims & PE_HF_DIM_FLUSH_DRAW) && ev)
        key |= (uint64_t)(f->flush_draw & 3u) << K_FLUSH;
    if ((dims & PE_HF_DIM_STRAIGHT_DRAW) && ev)
        key |= (uint64_t)(f->straight_draw & 3u) << K_STRAIGHT;
    if ((dims & PE_HF_DIM_COMBO_DRAW) && ev)
        key |= (uint64_t)(f->combo_draw & 1u) << K_COMBO;
    if ((dims & PE_HF_DIM_REDRAW) && ev)
        key |= (uint64_t)(f->redraw & 1u) << K_REDRAW;
    if ((dims & PE_HF_DIM_BLOCKERS) && ev)
        key |= (uint64_t)(f->blockers & 0xFu) << K_BLOCKERS;
    if (dims & PE_HF_DIM_CONNECTIVITY)
        key |= (uint64_t)(f->connectivity & 7u) << K_CONNECT;
    if (dims & PE_HF_DIM_SUITS)
        for (int i = 0; i < HF_SUITS; ++i)
            key |= (uint64_t)(f->suit_shape[i] & 7u) << (K_SUITS + 3 * i);
    if (dims & PE_HF_DIM_PAIRS)
    {
        unsigned paired = f->paired_ranks > 3 ? 3u : f->paired_ranks;
        unsigned trips = f->trips_ranks > 3 ? 3u : f->trips_ranks;
        key |= (uint64_t)(paired | (trips << 2)) << K_PAIRS;
    }
    return key;
}

uint32_t pe_hf_key_dims(uint64_t key)
{
    return (uint32_t)(key >> K_DIMS) & PE_HF_DIM_ALL;
}

int pe_hf_key_matches(const pe_hand_features_t *features, uint64_t key)
{
    return features && pe_hf_key(features, pe_hf_key_dims(key)) == key;
}

static void hf_append(char *buf, size_t size, size_t *len, const char *text)
{
    size_t n = strlen(text);
    if (buf && size && *len < size - 1u)
    {
        size_t room = size - 1u - *len;
        size_t copy = n < room ? n : room;
        memcpy(buf + *len, text, copy);
        buf[*len + copy] = '\0';
    }
    *len += n;
}

size_t pe_hf_key_format(uint64_t key, char *buf, size_t size)
{
    static const char *const made_names[] = {
        "high_card", "pair", "two_pair", "trips", "straight",
        "flush", "full_house", "quads", "straight_flush"};
    static const char *const detail_names[] = {
        "none", "overpair", "underpair", "top_pair", "second_pair",
        "low_pair", "board_pair", "top_two_pair", "top_set", "middle_set",
        "bottom_set", "trips", "board_trips"};
    static const char *const flush_names[] = {"none", "backdoor", "regular",
                                              "nut"};
    static const char *const straight_names[] = {"none", "gutshot",
                                                 "open_ended", "wrap"};
    static const char *const connect_names[] = {"none", "rundown", "high",
                                                "medium", "low"};
    const uint32_t dims = pe_hf_key_dims(key);
    size_t len = 0;
    char tmp[32];

    if (buf && size)
        buf[0] = '\0';

#define HF_SEP() hf_append(buf, size, &len, len ? "," : "")
    if (dims & PE_HF_DIM_MADE)
    {
        uint64_t v = hf_field(key, K_MADE, 4);
        HF_SEP();
        hf_append(buf, size, &len, "made=");
        hf_append(buf, size, &len, v < PE_HF_MADE_COUNT ? made_names[v] : "?");
    }
    if (dims & PE_HF_DIM_DETAIL)
    {
        uint64_t v = hf_field(key, K_DETAIL, 4);
        HF_SEP();
        hf_append(buf, size, &len, "detail=");
        hf_append(buf, size, &len,
                  v < PE_HF_DETAIL_COUNT ? detail_names[v] : "?");
    }
    if (dims & PE_HF_DIM_NUTS)
    {
        HF_SEP();
        hf_append(buf, size, &len,
                  hf_field(key, K_NUTS, 1) ? "nuts=yes" : "nuts=no");
    }
    if (dims & PE_HF_DIM_FLUSH_DRAW)
    {
        HF_SEP();
        hf_append(buf, size, &len, "flush_draw=");
        hf_append(buf, size, &len, flush_names[hf_field(key, K_FLUSH, 2)]);
    }
    if (dims & PE_HF_DIM_STRAIGHT_DRAW)
    {
        HF_SEP();
        hf_append(buf, size, &len, "straight_draw=");
        hf_append(buf, size, &len,
                  straight_names[hf_field(key, K_STRAIGHT, 2)]);
    }
    if (dims & PE_HF_DIM_COMBO_DRAW)
    {
        HF_SEP();
        hf_append(buf, size, &len,
                  hf_field(key, K_COMBO, 1) ? "combo_draw=yes"
                                            : "combo_draw=no");
    }
    if (dims & PE_HF_DIM_REDRAW)
    {
        HF_SEP();
        hf_append(buf, size, &len,
                  hf_field(key, K_REDRAW, 1) ? "redraw=yes" : "redraw=no");
    }
    if (dims & PE_HF_DIM_BLOCKERS)
    {
        static const char *const blocker_names[] = {
            "nut_flush", "nut_straight", "board_pair", "top_card"};
        uint64_t v = hf_field(key, K_BLOCKERS, 4);
        int any = 0;
        HF_SEP();
        hf_append(buf, size, &len, "blockers=");
        for (int i = 0; i < 4; ++i)
            if (v & (1u << i))
            {
                if (any)
                    hf_append(buf, size, &len, "+");
                hf_append(buf, size, &len, blocker_names[i]);
                any = 1;
            }
        if (!any)
            hf_append(buf, size, &len, "none");
    }
    if (dims & PE_HF_DIM_CONNECTIVITY)
    {
        uint64_t v = hf_field(key, K_CONNECT, 3);
        HF_SEP();
        hf_append(buf, size, &len, "connectivity=");
        hf_append(buf, size, &len,
                  v < PE_HF_CONNECT_COUNT ? connect_names[v] : "?");
    }
    if (dims & PE_HF_DIM_SUITS)
    {
        int first = 1;
        HF_SEP();
        hf_append(buf, size, &len, "suits=");
        for (int i = 0; i < HF_SUITS; ++i)
        {
            uint64_t v = hf_field(key, K_SUITS + 3 * i, 3);
            if (!v && !first)
                break;
            tmp[0] = (char)('0' + (int)v);
            tmp[1] = '\0';
            if (!first)
                hf_append(buf, size, &len, "-");
            hf_append(buf, size, &len, tmp);
            first = 0;
        }
    }
    if (dims & PE_HF_DIM_PAIRS)
    {
        uint64_t v = hf_field(key, K_PAIRS, 4);
        HF_SEP();
        hf_append(buf, size, &len, "pairs=");
        tmp[0] = (char)('0' + (int)(v & 3u));
        tmp[1] = 'p';
        tmp[2] = (char)('0' + (int)(v >> 2));
        tmp[3] = 't';
        tmp[4] = '\0';
        hf_append(buf, size, &len, tmp);
    }
#undef HF_SEP
    return len;
}

/* ------------------------------------------------------------------ *
 * Aggregation
 * ------------------------------------------------------------------ */

static int hf_finite_nonneg(double v) { return isfinite(v) && v >= 0.0; }

pe_solver_status_t pe_strategy_bucket_aggregate(const pe_hf_board_t *board,
                                                const pe_strategy_row_t *rows,
                                                size_t row_count,
                                                size_t action_count,
                                                uint32_t dims,
                                                pe_strategy_bucket_t *buckets,
                                                size_t capacity,
                                                size_t *out_count)
{
    size_t count = 0;
    double total = 0.0;

    if (!board || !out_count || (row_count && !rows) || (capacity && !buckets))
        return PE_SOLVER_ERR_NULL_ARGUMENT;
    *out_count = 0;
    if (action_count == 0 || action_count > PE_HF_MAX_ACTIONS)
        return PE_SOLVER_ERR_INVALID_CONFIG;

    for (size_t i = 0; i < row_count; ++i)
    {
        const pe_strategy_row_t *row = &rows[i];
        pe_hand_features_t f;
        uint64_t key;
        size_t lo = 0, hi = count;
        pe_strategy_bucket_t *bucket;
        double best_ev = 0.0;

        if (!row->freq)
            return PE_SOLVER_ERR_NULL_ARGUMENT;
        if (!hf_finite_nonneg(row->weight))
            return PE_SOLVER_ERR_INVALID_CONFIG;
        for (size_t a = 0; a < action_count; ++a)
        {
            if (!hf_finite_nonneg(row->freq[a]))
                return PE_SOLVER_ERR_INVALID_CONFIG;
            if (row->ev && !isfinite(row->ev[a]))
                return PE_SOLVER_ERR_INVALID_CONFIG;
        }
        if (pe_hand_features_compute(board, row->hand, &f) != PE_SOLVER_OK)
            return PE_SOLVER_ERR_INVALID_CONFIG;
        key = pe_hf_key(&f, dims);

        /* Buckets stay sorted by key: binary search, insert on a miss. */
        while (lo < hi)
        {
            size_t mid = lo + (hi - lo) / 2u;
            if (buckets[mid].key < key)
                lo = mid + 1u;
            else
                hi = mid;
        }
        if (lo == count || buckets[lo].key != key)
        {
            if (count == capacity)
                return PE_SOLVER_ERR_BUDGET_EXCEEDED;
            memmove(&buckets[lo + 1u], &buckets[lo],
                    (count - lo) * sizeof(*buckets));
            memset(&buckets[lo], 0, sizeof(*buckets));
            buckets[lo].key = key;
            count++;
        }
        bucket = &buckets[lo];

        bucket->combos++;
        bucket->weight += row->weight;
        total += row->weight;
        for (size_t a = 0; a < action_count; ++a)
            bucket->freq[a] += row->weight * row->freq[a];
        if (row->ev)
        {
            best_ev = row->ev[0];
            for (size_t a = 1; a < action_count; ++a)
                if (row->ev[a] > best_ev)
                    best_ev = row->ev[a];
            bucket->ev_combos++;
            bucket->ev_weight += row->weight;
            for (size_t a = 0; a < action_count; ++a)
            {
                bucket->ev[a] += row->weight * row->ev[a];
                bucket->ev_delta[a] += row->weight * (row->ev[a] - best_ev);
            }
        }
    }

    for (size_t b = 0; b < count; ++b)
    {
        pe_strategy_bucket_t *bucket = &buckets[b];
        if (bucket->weight > 0.0)
            for (size_t a = 0; a < action_count; ++a)
                bucket->freq[a] /= bucket->weight;
        if (bucket->ev_weight > 0.0)
            for (size_t a = 0; a < action_count; ++a)
            {
                bucket->ev[a] /= bucket->ev_weight;
                bucket->ev_delta[a] /= bucket->ev_weight;
            }
        bucket->weight_share = total > 0.0 ? bucket->weight / total : 0.0;
    }
    *out_count = count;
    return PE_SOLVER_OK;
}
