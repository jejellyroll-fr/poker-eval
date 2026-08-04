#include <poker_eval/range.h>
#include <poker_eval/range/StudRangeParser.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/deck/deck_std.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>

/* --- Internal Helpers --- */

static int char_to_rank(char c) {
    switch (toupper(c)) {
        case 'A': return 12;
        case 'K': return 11;
        case 'Q': return 10;
        case 'J': return 9;
        case 'T': return 8;
        case '9': return 7;
        case '8': return 6;
        case '7': return 5;
        case '6': return 4;
        case '5': return 3;
        case '4': return 2;
        case '3': return 1;
        case '2': return 0;
        default: return -1;
    }
}

static int char_to_suit(char c) {
    switch (toupper(c)) {
        case 'S': return StdDeck_Suit_SPADES;
        case 'H': return StdDeck_Suit_HEARTS;
        case 'D': return StdDeck_Suit_DIAMONDS;
        case 'C': return StdDeck_Suit_CLUBS;
        default: return -1;
    }
}

/* Helper: Add a combo to pe_range_t */
static int pe_range_add_combo(pe_range_t *range, StdDeck_CardMask hand, double weight) {
    if (range->count >= range->capacity) {
        size_t new_cap = range->capacity ? range->capacity * 2 : 256;
        pe_combo_t *new_combos = realloc(range->combos, new_cap * sizeof(pe_combo_t));
        if (!new_combos) return 0;
        range->combos = new_combos;
        range->capacity = new_cap;
    }
    range->combos[range->count].hand = hand;
    range->combos[range->count].weight = weight;
    range->count++;
    range->total_weight += weight;
    return 1;
}

/* Comparator for pe_combo_t (sort by hand mask) */
static int pe_combo_compare(const void *a, const void *b) {
    const pe_combo_t *c1 = (const pe_combo_t *)a;
    const pe_combo_t *c2 = (const pe_combo_t *)b;

    /* Compare Card Masks.
       We access internals depending on USE_INT64 macro, but since we are inside implementation
       and have deck_std.h, we can rely on what macros expose OR manual access.
       The safest is memcmp for canonical ordering, but we want numeric sort.
       Wait, StdDeck_CardMask is a union.
    */
#ifdef USE_INT64
    if (c1->hand.cards_n < c2->hand.cards_n) return -1;
    if (c1->hand.cards_n > c2->hand.cards_n) return 1;
    return 0;
#else
    if (c1->hand.cards_nn.n1 < c2->hand.cards_nn.n1) return -1;
    if (c1->hand.cards_nn.n1 > c2->hand.cards_nn.n1) return 1;
    if (c1->hand.cards_nn.n2 < c2->hand.cards_nn.n2) return -1;
    if (c1->hand.cards_nn.n2 > c2->hand.cards_nn.n2) return 1;
    return 0;
#endif
}

/* Helper: Convert ARP range to PE range */
static void convert_arp_to_pe(const arp_range_t *src, pe_range_t *dst) {
    for (size_t i = 0; i < src->count; i++) {
        double w = (src->weights) ? src->weights[i] : 1.0;
        pe_range_add_combo(dst, src->hands[i], w);
    }
}

/* Helper: Convert PE range to ARP range (Temporary for compatibility) */
static void convert_pe_to_arp(const pe_range_t *src, arp_range_t *dst) {
    memset(dst, 0, sizeof(arp_range_t));
    dst->count = src->count;
    dst->capacity = src->count;
    dst->hands = malloc(src->count * sizeof(StdDeck_CardMask));
    dst->weights = malloc(src->count * sizeof(double));
    dst->game_type = src->game_type;
    dst->has_weights = 1;

    if (src->combos) {
        for(size_t i=0; i<src->count; i++) {
            dst->hands[i] = src->combos[i].hand;
            dst->weights[i] = src->combos[i].weight;
        }
    }
}

/* Helper: Free manual ARP range */
static void free_manual_arp(arp_range_t *arp) {
    if (arp->hands) free(arp->hands);
    if (arp->weights) free(arp->weights);
}

/* --- Hold'em Parsing Logic --- */

static int add_pair(pe_range_t *range, int rank, double weight, StdDeck_CardMask dead) {
    /* 6 combos: S-H, S-D, S-C, H-D, H-C, D-C */
    int suits[4] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS, StdDeck_Suit_DIAMONDS, StdDeck_Suit_CLUBS};
    int added = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 4; j++) {
            StdDeck_CardMask hand;
            StdDeck_CardMask_RESET(hand);
            /* Create temp card masks using StdDeck_MASK */
            StdDeck_CardMask c1 = StdDeck_MASK(StdDeck_MAKE_CARD(rank, suits[i]));
            StdDeck_CardMask c2 = StdDeck_MASK(StdDeck_MAKE_CARD(rank, suits[j]));
            StdDeck_CardMask_OR(hand, hand, c1);
            StdDeck_CardMask_OR(hand, hand, c2);

            if (!StdDeck_CardMask_ANY_SET(hand, dead)) {
                pe_range_add_combo(range, hand, weight);
                added++;
            }
        }
    }
    return added;
}

static int add_offsuit(pe_range_t *range, int r1, int r2, double weight, StdDeck_CardMask dead) {
    /* 12 combos: r1 != r2, suits distinct */
    /* Offsuit means suits are different. */
    int suits[4] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS, StdDeck_Suit_DIAMONDS, StdDeck_Suit_CLUBS};
    int added = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i == j) continue; /* Suited */
            StdDeck_CardMask hand;
            StdDeck_CardMask_RESET(hand);
            StdDeck_CardMask c1 = StdDeck_MASK(StdDeck_MAKE_CARD(r1, suits[i]));
            StdDeck_CardMask c2 = StdDeck_MASK(StdDeck_MAKE_CARD(r2, suits[j]));
            StdDeck_CardMask_OR(hand, hand, c1);
            StdDeck_CardMask_OR(hand, hand, c2);

            if (!StdDeck_CardMask_ANY_SET(hand, dead)) {
                pe_range_add_combo(range, hand, weight);
                added++;
            }
        }
    }
    return added;
}

static int add_suited(pe_range_t *range, int r1, int r2, double weight, StdDeck_CardMask dead) {
    /* 4 combos: r1 != r2, same suit */
    int suits[4] = {StdDeck_Suit_SPADES, StdDeck_Suit_HEARTS, StdDeck_Suit_DIAMONDS, StdDeck_Suit_CLUBS};
    int added = 0;
    for (int i = 0; i < 4; i++) {
        StdDeck_CardMask hand;
        StdDeck_CardMask_RESET(hand);
        StdDeck_CardMask c1 = StdDeck_MASK(StdDeck_MAKE_CARD(r1, suits[i]));
        StdDeck_CardMask c2 = StdDeck_MASK(StdDeck_MAKE_CARD(r2, suits[i]));
        StdDeck_CardMask_OR(hand, hand, c1);
        StdDeck_CardMask_OR(hand, hand, c2);

        if (!StdDeck_CardMask_ANY_SET(hand, dead)) {
            pe_range_add_combo(range, hand, weight);
            added++;
        }
    }
    return added;
}

static int parse_holdem_token(pe_range_t *range, char *token, StdDeck_CardMask dead_cards, double default_weight) {
    /* Check for weight override ":weight" */
    double weight = default_weight;
    char *w_ptr = strchr(token, ':');
    if (w_ptr) {
        *w_ptr = '\0';
        weight = atof(w_ptr + 1);
    }

    /* Check for Percentage */
    if (strchr(token, '%')) {
        double pct = atof(token) / 100.0;  /* Convert from percentage (0-100) to ratio (0.0-1.0) */
        pe_range_t *sub = NULL;
        pe_range_top_percent(range->game_type, pct, dead_cards, &sub);
        if (sub) {
            for (size_t i = 0; i < sub->count; i++) {
                pe_range_add_combo(range, sub->combos[i].hand, weight); /* Use override weight if specified? Or keep relative? Assume flat for now */
            }
            pe_range_free(sub);
            return 1;
        }
        return 0;
    }

    /* Specific Hand: AsKh (len 4) */
    if (strnlen(token, 16) == 4 && char_to_suit(token[1]) != -1 && char_to_suit(token[3]) != -1) {
        int r1 = char_to_rank(token[0]);
        int s1 = char_to_suit(token[1]);
        int r2 = char_to_rank(token[2]);
        int s2 = char_to_suit(token[3]);
        if (r1 >= 0 && s1 >= 0 && r2 >= 0 && s2 >= 0) {
            StdDeck_CardMask hand;
            StdDeck_CardMask_RESET(hand);
            StdDeck_CardMask c1 = StdDeck_MASK(StdDeck_MAKE_CARD(r1, s1));
            StdDeck_CardMask c2 = StdDeck_MASK(StdDeck_MAKE_CARD(r2, s2));
            StdDeck_CardMask_OR(hand, hand, c1);
            StdDeck_CardMask_OR(hand, hand, c2);
            if (!StdDeck_CardMask_ANY_SET(hand, dead_cards)) {
                pe_range_add_combo(range, hand, weight);
                return 1;
            }
            return 1; // Hand valid but dead, effectively added 0
        }
    }

    /* Range/Pair/Suited/Offsuit */
    int len = (int)strnlen(token, 16);
    int is_plus = (len > 0 && token[len-1] == '+');
    if (is_plus) token[len-1] = '\0';

    int is_dash = 0;
    char *dash = strchr(token, '-');
    char p1[16] = {0}, p2[16] = {0};
    if (dash) {
        is_dash = 1;
        snprintf(p1, sizeof(p1), "%.*s", (int)(dash - token), token);
        snprintf(p2, sizeof(p2), "%s", dash + 1);
    } else {
        snprintf(p1, sizeof(p1), "%s", token);
    }

    /* Parse part 1 */
    int r1a = char_to_rank(p1[0]);
    int r1b = char_to_rank(p1[1]);
    char type1 = (strlen(p1) > 2) ? p1[2] : 0; /* 's' or 'o' or 0 */

    /* If Pair */
    if (r1a == r1b && r1a >= 0) {
        if (is_plus) {
            /* 88+ -> 88, 99... AA */
            for (int r = r1a; r <= 12; r++) {
                add_pair(range, r, weight, dead_cards);
            }
        } else if (is_dash) {
            /* 99-77 */
            int r2a = char_to_rank(p2[0]);
            int start = r1a;
            int end = r2a;
            if (start < end) { int t=start; start=end; end=t; } /* Swap if wrong order */
            for (int r = end; r <= start; r++) {
                add_pair(range, r, weight, dead_cards);
            }
        } else {
            /* 88 */
            add_pair(range, r1a, weight, dead_cards);
        }
        return 1;
    }

    /* If Hand (AK, AKs, AKo) */
    if (r1a != r1b && r1a >= 0 && r1b >= 0) {
        /* Normalize so r1 is high */
        if (r1a < r1b) { int t=r1a; r1a=r1b; r1b=t; }

        if (is_plus) {
            /* AJs+ -> AJs, AQs, AKs */
            /* KJs+ -> KJs, KQs (Keep high card, increment low card) */
            for (int r = r1b; r < r1a; r++) {
                if (type1 == 's') add_suited(range, r1a, r, weight, dead_cards);
                else if (type1 == 'o') add_offsuit(range, r1a, r, weight, dead_cards);
                else {
                    add_suited(range, r1a, r, weight, dead_cards);
                    add_offsuit(range, r1a, r, weight, dead_cards);
                }
            }
        } else if (is_dash) {
            /* AK-AJ */
            int r2a = char_to_rank(p2[0]);
            int r2b = char_to_rank(p2[1]);
            if (r2a < r2b) { int t=r2a; r2a=r2b; r2b=t; }

            /* Assuming High card matches (e.g. AK-AJ). If not (KQ-J9), it's ambiguous/complex, assume same high card */
            if (r1a == r2a) {
                int start = r1b;
                int end = r2b;
                if (start < end) { int t=start; start=end; end=t; }
                for (int r = end; r <= start; r++) {
                    if (type1 == 's') add_suited(range, r1a, r, weight, dead_cards);
                    else if (type1 == 'o') add_offsuit(range, r1a, r, weight, dead_cards);
                    else {
                        add_suited(range, r1a, r, weight, dead_cards);
                        add_offsuit(range, r1a, r, weight, dead_cards);
                    }
                }
            }
        } else {
            /* AKs */
            if (type1 == 's') add_suited(range, r1a, r1b, weight, dead_cards);
            else if (type1 == 'o') add_offsuit(range, r1a, r1b, weight, dead_cards);
            else {
                add_suited(range, r1a, r1b, weight, dead_cards);
                add_offsuit(range, r1a, r1b, weight, dead_cards);
            }
        }
        return 1;
    }

    return 0; /* Failed to parse */
}


/* --- Public API Implementation --- */

void pe_range_opts_init(pe_parse_opts_t *opts) {
    if (opts) {
        opts->strict_syntax = 0;
        opts->allow_weights = 1;
        opts->default_weight = 1.0;
    }
}

pe_status_t pe_range_parse(
    enum_game_t variant,
    const char *range_str,
    StdDeck_CardMask dead_cards,
    const pe_parse_opts_t *opts,
    pe_range_t **out_range
) {
    if (!range_str || !out_range) return PE_STATUS_INVALID_ARG;

    pe_range_t *range;
    pe_status_t status = pe_range_create(variant, &range);
    if (status != PE_STATUS_OK) return status;

    /* For Stud games, use Stud parser */
    if (variant == game_7stud || variant == game_7stud8 || 
        variant == game_7studnsq || variant == game_razz) {
        arp_range_t arp_range;
        memset(&arp_range, 0, sizeof(arp_range_t));
        int ret = ARP_ParseStudPattern(range_str, dead_cards, &arp_range);
        if (ret) {
            convert_arp_to_pe(&arp_range, range);
            ARP_FreeRange(&arp_range);
            *out_range = range;
            return PE_STATUS_OK;
        } else {
            pe_range_free(range);
            *out_range = NULL;
            return PE_STATUS_PARSE_ERROR;
        }
    }

    /* For Badugi, use the generic range parser which supports 4-card hands */
    if (variant == game_badugi) {
        arp_range_t arp_range;
        memset(&arp_range, 0, sizeof(arp_range_t));
        int ret = ARP_ParseRange(range_str, dead_cards, variant, &arp_range);
        if (ret) {
            convert_arp_to_pe(&arp_range, range);
            ARP_FreeRange(&arp_range);
            *out_range = range;
            return PE_STATUS_OK;
        } else {
            pe_range_free(range);
            *out_range = NULL;
            return PE_STATUS_PARSE_ERROR;
        }
    }

    /* For Omaha and other non-Hold'em, use Omaha parser */
    if (variant != game_holdem) {
        arp_range_t arp_range;
        memset(&arp_range, 0, sizeof(arp_range_t));
        int ret = ARP_ParseOmahaRange(range_str, dead_cards, variant, &arp_range);
        if (ret) {
            convert_arp_to_pe(&arp_range, range);
            ARP_FreeRange(&arp_range);
            *out_range = range;
            return PE_STATUS_OK;
        } else {
            pe_range_free(range);
            *out_range = NULL;
            return PE_STATUS_PARSE_ERROR;
        }
    }

    /* Hold'em Parsing */
    char *str_copy = strdup(range_str);
    if (!str_copy) {
        pe_range_free(range);
        return PE_STATUS_OUT_OF_MEMORY;
    }

    char *token = strtok(str_copy, ",");
    while (token) {
        /* Trim whitespace */
        while(isspace(*token)) token++;
        char *end = token + strlen(token) - 1;
        while(end > token && isspace(*end)) *end-- = '\0';

        if (*token) {
            parse_holdem_token(range, token, dead_cards, opts ? opts->default_weight : 1.0);
        }
        token = strtok(NULL, ",");
    }

    free(str_copy);
    *out_range = range;
    return PE_STATUS_OK;
}

void pe_range_free(pe_range_t *range) {
    if (range) {
        if (range->combos) free(range->combos);
        free(range);
    }
}

pe_status_t pe_range_create(enum_game_t variant, pe_range_t **out_range) {
    if (!out_range) return PE_STATUS_INVALID_ARG;

    pe_range_t *range = malloc(sizeof(pe_range_t));
    if (!range) return PE_STATUS_OUT_OF_MEMORY;

    memset(range, 0, sizeof(pe_range_t));
    range->game_type = variant;
    range->capacity = 64;
    range->combos = malloc(range->capacity * sizeof(pe_combo_t));
    if (!range->combos) {
        free(range);
        return PE_STATUS_OUT_OF_MEMORY;
    }

    *out_range = range;
    return PE_STATUS_OK;
}

pe_status_t pe_range_compile(
    const pe_range_t *in_range,
    const pe_compile_opts_t *opts,
    pe_compiled_range_t **out_range
) {
    if (!in_range || !out_range) return PE_STATUS_INVALID_ARG;

    /* Clone first */
    pe_range_t *res;
    if (pe_range_create(in_range->game_type, &res) != PE_STATUS_OK) return PE_STATUS_OUT_OF_MEMORY;

    for (size_t i = 0; i < in_range->count; i++) {
        pe_range_add_combo(res, in_range->combos[i].hand, in_range->combos[i].weight);
    }

    /* Sort */
    if (res->count > 1) {
        qsort(res->combos, res->count, sizeof(pe_combo_t), pe_combo_compare);
    }

    /* Dedup */
    if (res->count > 0) {
        size_t write_idx = 0;
        for (size_t i = 1; i < res->count; i++) {
            if (pe_combo_compare(&res->combos[write_idx], &res->combos[i]) == 0) {
                /* Same hand, merge weight (ADD) */
                res->combos[write_idx].weight += res->combos[i].weight;
                /* Note: total_weight already sums all inputs, but now we have fewer combos.
                   Wait, merging weights preserves total weight. */
            } else {
                write_idx++;
                res->combos[write_idx] = res->combos[i];
            }
        }
        res->count = write_idx + 1;
    }

    *out_range = res;
    return PE_STATUS_OK;
}

pe_status_t pe_range_combine(
    const pe_range_t *range1,
    const pe_range_t *range2,
    pe_range_op_t op,
    pe_range_t **out_range
) {
    if (!range1 || !range2 || !out_range) return PE_STATUS_INVALID_ARG;

    /* Compile inputs first to ensure sortedness for linear merge */
    pe_range_t *c1 = NULL, *c2 = NULL;

    /* Optimization: If already compiled (sorted), skip sort? We don't track that yet.
       We'll compile anyway. */
    if (pe_range_compile(range1, NULL, &c1) != PE_STATUS_OK) return PE_STATUS_OUT_OF_MEMORY;
    if (pe_range_compile(range2, NULL, &c2) != PE_STATUS_OK) {
        pe_range_free(c1);
        return PE_STATUS_OUT_OF_MEMORY;
    }

    pe_range_t *res = NULL;
    pe_status_t status = pe_range_create(range1->game_type, &res);
    if (status != PE_STATUS_OK) {
        pe_range_free(c1);
        pe_range_free(c2);
        return status;
    }

    size_t i = 0, j = 0;
    while ((i < c1->count || j < c2->count) && status == PE_STATUS_OK) {
        int cmp = 0;
        if (i < c1->count && j < c2->count) {
            cmp = pe_combo_compare(&c1->combos[i], &c2->combos[j]);
        } else if (i < c1->count) {
            cmp = -1; /* c1 remains */
        } else {
            cmp = 1; /* c2 remains */
        }

        if (cmp < 0) {
            /* Hand in 1 only */
            if (op == PE_OP_UNION || op == PE_OP_DIFFERENCE) {
                if (!pe_range_add_combo(
                        res, c1->combos[i].hand, c1->combos[i].weight)) {
                    status = PE_STATUS_OUT_OF_MEMORY;
                }
            }
            i++;
        } else if (cmp > 0) {
            /* Hand in 2 only */
            if (op == PE_OP_UNION) {
                if (!pe_range_add_combo(
                        res, c2->combos[j].hand, c2->combos[j].weight)) {
                    status = PE_STATUS_OUT_OF_MEMORY;
                }
            }
            j++;
        } else {
            /* Hand in both */
            if (op == PE_OP_UNION) {
                /* Combine weights? Max? Sum? Usually max or sum.
                   For sets, union is simple inclusion.
                   If weights differ, usually take max or assume consistency.
                   Let's take max for now to avoid >100% implicitly unless weights are additive counts. */
                 double w = (c1->combos[i].weight > c2->combos[j].weight) ? c1->combos[i].weight : c2->combos[j].weight;
                 if (!pe_range_add_combo(res, c1->combos[i].hand, w)) {
                     status = PE_STATUS_OUT_OF_MEMORY;
                 }
            } else if (op == PE_OP_INTERSECT) {
                 double w = (c1->combos[i].weight < c2->combos[j].weight) ? c1->combos[i].weight : c2->combos[j].weight;
                 if (!pe_range_add_combo(res, c1->combos[i].hand, w)) {
                     status = PE_STATUS_OUT_OF_MEMORY;
                 }
            }
            /* If DIFFERENCE, we exclude it, so do nothing */
            i++;
            j++;
        }
    }

    pe_range_free(c1);
    pe_range_free(c2);

    if (status != PE_STATUS_OK) {
        pe_range_free(res);
        *out_range = NULL;
        return status;
    }

    *out_range = res;
    return PE_STATUS_OK;
}

pe_status_t pe_range_filter_dead(
    const pe_range_t *in_range,
    StdDeck_CardMask dead_cards,
    pe_range_t **out_range
) {
    if (!in_range || !out_range) return PE_STATUS_INVALID_ARG;

    pe_range_t *res;
    if (pe_range_create(in_range->game_type, &res) != PE_STATUS_OK) return PE_STATUS_OUT_OF_MEMORY;

    for (size_t i = 0; i < in_range->count; i++) {
        if (!StdDeck_CardMask_ANY_SET(in_range->combos[i].hand, dead_cards)) {
            pe_range_add_combo(res, in_range->combos[i].hand, in_range->combos[i].weight);
        }
    }

    *out_range = res;
    return PE_STATUS_OK;
}

pe_status_t pe_range_top_percent(
    enum_game_t variant,
    double percent,
    StdDeck_CardMask dead_cards,
    pe_range_t **out_range
) {
    if (!out_range) return PE_STATUS_INVALID_ARG;
    *out_range = NULL;

    pe_range_t *range = NULL;
    pe_status_t status = pe_range_create(variant, &range);
    if (status != PE_STATUS_OK) return status;

    arp_range_t arp_res;
    memset(&arp_res, 0, sizeof(arp_range_t));

    int ret;
    if (variant == game_omaha || variant == game_omaha5 || variant == game_omaha6) {
        ret = ARP_GetOmahaTopPercentage((float)percent, variant, dead_cards, &arp_res);
    } else if (variant == game_7stud || variant == game_7stud8 ||
               variant == game_7studnsq || variant == game_razz) {
        ret = ARP_GetStudTopPercentage((float)percent, variant, dead_cards, &arp_res);
    } else {
        ret = ARP_GetTopPercentage((float)percent, variant, dead_cards, &arp_res);
    }

    if (ret) {
        convert_arp_to_pe(&arp_res, range);
        ARP_FreeRange(&arp_res);
        *out_range = range;
        return PE_STATUS_OK;
    }

    pe_range_free(range);
    return PE_STATUS_ERROR;
}

const char* pe_error_string(pe_status_t status) {
    switch (status) {
        case PE_STATUS_OK: return "No error";
        case PE_STATUS_ERROR: return "Unknown error";
        case PE_STATUS_INVALID_ARG: return "Invalid argument";
        case PE_STATUS_PARSE_ERROR: return "Parsing error";
        case PE_STATUS_OUT_OF_MEMORY: return "Out of memory";
        case PE_STATUS_NOT_IMPLEMENTED: return "Not implemented";
        default: return "Invalid status code";
    }
}
