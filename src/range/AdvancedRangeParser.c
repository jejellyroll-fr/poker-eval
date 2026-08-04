/*
 * Copyright (C) 2024
 *           Poker-eval contributors
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/distributions/plo_integration.h>
#include <poker_eval/range/StudRangeParser.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <poker_eval/core/pthread_compat.h>
#include <time.h>

/* Cache configuration */
#define ARP_CACHE_SIZE 32
#define ARP_CACHE_TTL_SECONDS 3600  /* 1 hour */

/* Cache entry */
typedef struct {
    enum_game_t game_type;
    float percentage;
    arp_range_t cached_range;
    time_t created_at;
    bool valid;
} arp_cache_entry_t;

/* Global cache (thread-safe with mutex) */
typedef struct {
    arp_cache_entry_t entries[ARP_CACHE_SIZE];
    int count;
    pthread_mutex_t lock;
    bool initialized;
} arp_percentage_cache_t;

static arp_percentage_cache_t global_cache = {
    .count = 0,
    .initialized = false
};

/* Internal helper functions */
static int arp_char_to_rank(char c);
static int arp_char_to_suit(char c);
static char arp_rank_to_char(int rank);
static char arp_suit_to_char(int suit);
static int arp_tokenize(arp_context_t *ctx);
static int arp_parse_tokens(arp_context_t *ctx, StdDeck_CardMask dead_cards, enum_game_t game_type, arp_range_t *result);
typedef struct arp_hash_table_s arp_hash_table_t;
static void arp_hash_free(arp_hash_table_t *ht);
static int arp_add_hand_to_range(arp_range_t *range, StdDeck_CardMask hand, double weight);
static int arp_remove_hand_from_range(arp_range_t *range, StdDeck_CardMask hand);
static int arp_remove_range(arp_range_t *target, const arp_range_t *to_remove);
static void arp_normalize_range_weights(arp_range_t *range);
static void arp_apply_weight_to_range(arp_range_t *range, double weight);
static int arp_expand_pair_range(int high_rank, int low_rank, StdDeck_CardMask dead_cards, arp_range_t *range);
static int arp_expand_hand_range(int rank1, int rank2, bool suited, bool offsuit, StdDeck_CardMask dead_cards, arp_range_t *range);
static int arp_expand_nonpropair_range(int rank1, int rank2_high, int rank2_low, bool suited, bool offsuit, StdDeck_CardMask dead_cards, arp_range_t *range);
static int arp_expand_percentage(float percentage, enum_game_t game_type, StdDeck_CardMask dead_cards, arp_range_t *range);
static int arp_is_plo_pattern(const char *str, size_t len);
static int arp_parse_plo_pattern(const char *pattern, StdDeck_CardMask dead_cards, arp_range_t *range);
static int arp_expand_omaha_percentage(float percentage, enum_game_t game_type, StdDeck_CardMask dead_cards, arp_range_t *range);
static int arp_is_omaha_game(enum_game_t game_type);
static int arp_init_range_with_game(arp_range_t *range, enum_game_t game_type);
static int arp_init_range(arp_range_t *range);
static int arp_lookup_plo_category(const char *str, size_t len, PLOHandCategory *category);

static bool arp_strncaseeq(const char *lhs, const char *rhs, size_t len)
{
    if (!lhs || !rhs)
    {
        return false;
    }

    for (size_t i = 0; i < len; ++i)
    {
        unsigned char a = (unsigned char)lhs[i];
        unsigned char b = (unsigned char)rhs[i];
        if (tolower(a) != tolower(b))
        {
            return false;
        }
    }

    return true;
}

/* Initialize cache (call once at startup) */
void ARP_InitCache(void) {
    if (global_cache.initialized)
        return;

    /* Initialize mutex */
    /* Note: In a real library constructor we might need a more robust init
       but for now we rely on explicit call or auto-init on first use */
    pthread_mutex_init(&global_cache.lock, NULL);

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        global_cache.entries[i].valid = false;
    }

    global_cache.count = 0;
    global_cache.initialized = true;
}

/* Cache key hash */
static uint32_t arp_cache_hash(enum_game_t game_type, float percentage) {
    uint32_t hash = (uint32_t)game_type;
    uint32_t percent_bits;
    memcpy(&percent_bits, &percentage, sizeof(float));
    hash ^= percent_bits;
    return hash % ARP_CACHE_SIZE;
}

/* Lookup in cache - Thread Safe */
static bool arp_cache_lookup(
    enum_game_t game_type,
    float percentage,
    arp_range_t *dest)
{
    if (!global_cache.initialized || !dest)
        return false;

    pthread_mutex_lock(&global_cache.lock);

    uint32_t hash = arp_cache_hash(game_type, percentage);
    arp_cache_entry_t *entry = &global_cache.entries[hash];

    if (!entry->valid) {
        pthread_mutex_unlock(&global_cache.lock);
        return false;
    }

    /* Check exact match */
    if (entry->game_type != game_type ||
        fabs(entry->percentage - percentage) > 0.0001f) {
        pthread_mutex_unlock(&global_cache.lock);
        return false;
    }

    /* Check TTL */
    time_t now = time(NULL);
    if (now - entry->created_at > ARP_CACHE_TTL_SECONDS) {
        entry->valid = false;
        if (global_cache.count > 0) global_cache.count--;
        pthread_mutex_unlock(&global_cache.lock);
        return false;
    }

    /* Cache hit! Copy to dest */
    size_t required_capacity = entry->cached_range.count;

    /* Reallocate if necessary (dest->hands/weights should be valid ptrs or NULL) */
    if (dest->capacity < required_capacity) {
        StdDeck_CardMask *new_hands = realloc(dest->hands, required_capacity * sizeof(StdDeck_CardMask));
        double *new_weights = realloc(dest->weights, required_capacity * sizeof(double));

        if (!new_hands || !new_weights) {
            /* Allocation failed */
            pthread_mutex_unlock(&global_cache.lock);
            return false;
        }
        dest->hands = new_hands;
        dest->weights = new_weights;
        dest->capacity = required_capacity;
    }

    memcpy(dest->hands, entry->cached_range.hands, required_capacity * sizeof(StdDeck_CardMask));
    memcpy(dest->weights, entry->cached_range.weights, required_capacity * sizeof(double));

    dest->count = entry->cached_range.count;
    dest->total_weight = entry->cached_range.total_weight;
    dest->has_weights = entry->cached_range.has_weights;
    dest->percentage_used = entry->cached_range.percentage_used;
    dest->is_percentage = entry->cached_range.is_percentage;
    dest->game_type = entry->cached_range.game_type;
    dest->is_omaha = entry->cached_range.is_omaha;

    pthread_mutex_unlock(&global_cache.lock);
    return true;
}

/* Insert into cache */
static void arp_cache_insert(
    enum_game_t game_type,
    float percentage,
    const arp_range_t *range)
{
    if (!global_cache.initialized)
        ARP_InitCache();

    pthread_mutex_lock(&global_cache.lock);

    uint32_t hash = arp_cache_hash(game_type, percentage);
    arp_cache_entry_t *entry = &global_cache.entries[hash];

    /* Free old entry if exists */
    if (entry->valid) {
        if (entry->cached_range.hands) free(entry->cached_range.hands);
        if (entry->cached_range.weights) free(entry->cached_range.weights);
        entry->valid = false;
        if (global_cache.count > 0) global_cache.count--;
    }

    /* Clone the range */
    entry->cached_range.hands = malloc(range->count * sizeof(StdDeck_CardMask));
    entry->cached_range.weights = malloc(range->count * sizeof(double));

    if (entry->cached_range.hands && entry->cached_range.weights) {
        memcpy(entry->cached_range.hands, range->hands,
               range->count * sizeof(StdDeck_CardMask));
        memcpy(entry->cached_range.weights, range->weights,
               range->count * sizeof(double));

        entry->cached_range.count = range->count;
        entry->cached_range.capacity = range->count;
        entry->cached_range.total_weight = range->total_weight;
        entry->cached_range.has_weights = range->has_weights;
        entry->cached_range.percentage_used = range->percentage_used;
        entry->cached_range.is_percentage = range->is_percentage;
        entry->cached_range.game_type = range->game_type;
        entry->cached_range.is_omaha = range->is_omaha;

        entry->cached_range.internal_data = NULL;

        entry->game_type = game_type;
        entry->percentage = percentage;
        entry->created_at = time(NULL);
        entry->valid = true;

        global_cache.count++;
    } else {
        if (entry->cached_range.hands) free(entry->cached_range.hands);
        if (entry->cached_range.weights) free(entry->cached_range.weights);
    }

    pthread_mutex_unlock(&global_cache.lock);
}

/* Clear entire cache */
void ARP_ClearCache(void) {
    if (!global_cache.initialized)
        return;

    pthread_mutex_lock(&global_cache.lock);

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (global_cache.entries[i].valid) {
             /* Use ARP_FreeRange to free internal structures properly */
             /* We need to zero out pointers after freeing to avoid double free */
             /* But ARP_FreeRange expects the range structure itself. */
             /* We can just free the arrays directly as we do here. */
             /* Note: We use the logic from ARP_FreeRange but without calling it
                because cached_range is a struct, not a pointer */
             if (global_cache.entries[i].cached_range.hands) {
                 free(global_cache.entries[i].cached_range.hands);
                 global_cache.entries[i].cached_range.hands = NULL;
             }
             if (global_cache.entries[i].cached_range.weights) {
                 free(global_cache.entries[i].cached_range.weights);
                 global_cache.entries[i].cached_range.weights = NULL;
             }
             /* Don't forget hash table if present */
             if (global_cache.entries[i].cached_range.internal_data) {
                 arp_hash_table_t *ht =
                     (arp_hash_table_t *)global_cache.entries[i].cached_range.internal_data;
                 arp_hash_free(ht); /* frees the slot array, not just the header */
                 free(ht);
                 global_cache.entries[i].cached_range.internal_data = NULL;
             }
             global_cache.entries[i].valid = false;
        }
    }

    global_cache.count = 0;
    pthread_mutex_unlock(&global_cache.lock);
}

/* Get cache statistics */
void ARP_GetCacheStats(arp_cache_stats_t *stats) {
    if (!global_cache.initialized || !stats)
        return;

    pthread_mutex_lock(&global_cache.lock);

    stats->total_entries = ARP_CACHE_SIZE;
    stats->valid_entries = 0;
    stats->total_memory_bytes = 0;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (global_cache.entries[i].valid) {
            stats->valid_entries++;
            stats->total_memory_bytes +=
                global_cache.entries[i].cached_range.count *
                (sizeof(StdDeck_CardMask) + sizeof(double));
        }
    }

    pthread_mutex_unlock(&global_cache.lock);
}


/* Phase 2: Expression parsing functions */
static arp_expr_node_t *arp_create_range_node(arp_token_t *token);
static arp_expr_node_t *arp_create_operation_node(arp_operation_t op, arp_expr_node_t *left, arp_expr_node_t *right);
static arp_expr_node_t *arp_parse_expression(arp_context_t *ctx);
static arp_expr_node_t *arp_parse_term(arp_context_t *ctx);
static arp_expr_node_t *arp_parse_factor(arp_context_t *ctx);
static arp_expr_node_t *arp_parse_primary(arp_context_t *ctx);
static int arp_evaluate_expression_node(arp_expr_node_t *node, StdDeck_CardMask dead_cards,
                                         enum_game_t game_type, arp_range_t *result);
static int arp_range_union(const arp_range_t *range1, const arp_range_t *range2, arp_range_t *result);
static int arp_range_subtract(const arp_range_t *range1, const arp_range_t *range2, arp_range_t *result);
static int arp_range_complement(const arp_range_t *range, enum_game_t game_type, arp_range_t *result);
static bool arp_is_unary_exclude_node(const arp_expr_node_t *node);
static void arp_range_move(arp_range_t *dest, arp_range_t *src);
static int arp_apply_token_operation(arp_range_t *accumulator, bool *has_accumulator,
                                     arp_range_t *token_range, arp_operation_t op,
                                     enum_game_t game_type);

/* Character to rank conversion */
static int arp_char_to_rank(char c)
{
    switch (toupper(c))
    {
    case '2':
        return StdDeck_Rank_2;
    case '3':
        return StdDeck_Rank_3;
    case '4':
        return StdDeck_Rank_4;
    case '5':
        return StdDeck_Rank_5;
    case '6':
        return StdDeck_Rank_6;
    case '7':
        return StdDeck_Rank_7;
    case '8':
        return StdDeck_Rank_8;
    case '9':
        return StdDeck_Rank_9;
    case 'T':
        return StdDeck_Rank_TEN;
    case 'J':
        return StdDeck_Rank_JACK;
    case 'Q':
        return StdDeck_Rank_QUEEN;
    case 'K':
        return StdDeck_Rank_KING;
    case 'A':
        return StdDeck_Rank_ACE;
    default:
        return -1;
    }
}

/* Character to suit conversion */
static int arp_char_to_suit(char c)
{
    switch (tolower(c))
    {
    case 'c':
        return StdDeck_Suit_CLUBS;
    case 'd':
        return StdDeck_Suit_DIAMONDS;
    case 'h':
        return StdDeck_Suit_HEARTS;
    case 's':
        return StdDeck_Suit_SPADES;
    default:
        return -1;
    }
}

/* Rank to character conversion */
static char arp_rank_to_char(int rank)
{
    switch (rank)
    {
    case StdDeck_Rank_2:
        return '2';
    case StdDeck_Rank_3:
        return '3';
    case StdDeck_Rank_4:
        return '4';
    case StdDeck_Rank_5:
        return '5';
    case StdDeck_Rank_6:
        return '6';
    case StdDeck_Rank_7:
        return '7';
    case StdDeck_Rank_8:
        return '8';
    case StdDeck_Rank_9:
        return '9';
    case StdDeck_Rank_TEN:
        return 'T';
    case StdDeck_Rank_JACK:
        return 'J';
    case StdDeck_Rank_QUEEN:
        return 'Q';
    case StdDeck_Rank_KING:
        return 'K';
    case StdDeck_Rank_ACE:
        return 'A';
    default:
        return '?';
    }
}

/* Suit to character conversion */
static char arp_suit_to_char(int suit)
{
    switch (suit)
    {
    case StdDeck_Suit_CLUBS:
        return 'c';
    case StdDeck_Suit_DIAMONDS:
        return 'd';
    case StdDeck_Suit_HEARTS:
        return 'h';
    case StdDeck_Suit_SPADES:
        return 's';
    default:
        return '?';
    }
}

/* Initialize range with specific capacity */
/* Smallest table we ever allocate (power of two). */
#define ARP_HASH_MIN_SLOTS 1024

typedef struct {
    StdDeck_CardMask hand;
    size_t index;  /* Index in range->hands array */
    bool occupied;
} arp_hash_entry_t;

struct arp_hash_table_s {
    arp_hash_entry_t *table; /* slots, heap allocated, grows with the range */
    size_t mask;             /* slots - 1; slots is always a power of two */
    size_t num_entries;
};

/*
 * Hash function for CardMask.
 *
 * The whole 52-bit mask must take part: a hash folding the four suit fields
 * together with XOR is invariant under suit permutation, so As-Kh and Ah-Ks —
 * distinct hands — collide. Omaha patterns such as "AAxxds" are built almost
 * entirely of suit permutations of one another, which piled thousands of hands
 * onto a handful of buckets.
 */
static uint32_t arp_hash_cardmask(StdDeck_CardMask hand) {
#ifdef USE_INT64
    uint64_t key = hand.cards_n;
#else
    uint64_t key = ((uint64_t)hand.cards.spades) |
                   ((uint64_t)hand.cards.clubs << 13) |
                   ((uint64_t)hand.cards.diamonds << 26) |
                   ((uint64_t)hand.cards.hearts << 39);
#endif

    /* splitmix64 finalizer */
    key ^= key >> 30;
    key *= 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 27;
    key *= 0x94d049bb133111ebULL;
    key ^= key >> 31;

    return (uint32_t)key;
}

/* Free hash table contents (the struct itself is owned by the caller) */
static void arp_hash_free(arp_hash_table_t *ht) {
    if (!ht)
        return;
    free(ht->table);
    ht->table = NULL;
    ht->mask = 0;
    ht->num_entries = 0;
}

/* Insert into a table known to have a free slot and not to hold `hand`. */
static void arp_hash_put(arp_hash_table_t *ht, StdDeck_CardMask hand, size_t index) {
    size_t i = (size_t)arp_hash_cardmask(hand) & ht->mask;
    while (ht->table[i].occupied)
        i = (i + 1) & ht->mask;
    ht->table[i].hand = hand;
    ht->table[i].index = index;
    ht->table[i].occupied = true;
}

/* Grow (or first allocate) the table so it can hold min_entries below 70% load */
static bool arp_hash_reserve(arp_hash_table_t *ht, size_t min_entries) {
    if (!ht)
        return false;

    size_t slots = ARP_HASH_MIN_SLOTS;
    while (slots * 7 < (min_entries + 1) * 10)
        slots <<= 1;

    if (ht->table && slots <= ht->mask + 1)
        return true; /* already large enough */

    arp_hash_entry_t *fresh = calloc(slots, sizeof(*fresh));
    if (!fresh)
        return false;

    arp_hash_entry_t *old = ht->table;
    size_t old_slots = old ? ht->mask + 1 : 0;
    ht->table = fresh;
    ht->mask = slots - 1;

    for (size_t i = 0; i < old_slots; i++)
        if (old[i].occupied)
            arp_hash_put(ht, old[i].hand, old[i].index);
    free(old);
    return true;
}

/* Initialize hash table sized for `expected` entries */
static bool arp_hash_init(arp_hash_table_t *ht, size_t expected) {
    if (!ht)
        return false;
    ht->table = NULL;
    ht->mask = 0;
    ht->num_entries = 0;
    return arp_hash_reserve(ht, expected);
}

/*
 * Look a hand up. On success stores its position in range->hands into
 * *index_out. Probing runs until an empty slot is reached; the load factor is
 * held below 70% by arp_hash_insert(), so the walk always terminates.
 */
static bool arp_hash_lookup(const arp_hash_table_t *ht, StdDeck_CardMask hand,
                            size_t *index_out) {
    if (!ht || !ht->table)
        return false;

    size_t i = (size_t)arp_hash_cardmask(hand) & ht->mask;
    while (ht->table[i].occupied) {
        if (StdDeck_CardMask_EQUAL(ht->table[i].hand, hand)) {
            if (index_out)
                *index_out = ht->table[i].index;
            return true;
        }
        i = (i + 1) & ht->mask;
    }
    return false;
}

/* Check if hand exists */
static bool arp_hash_contains(const arp_hash_table_t *ht, StdDeck_CardMask hand) {
    return arp_hash_lookup(ht, hand, NULL);
}

/* Insert into hash table. Returns false if `hand` was already present. */
static bool arp_hash_insert(arp_hash_table_t *ht, StdDeck_CardMask hand, size_t index) {
    if (!ht || !ht->table)
        return false;

    if (arp_hash_contains(ht, hand))
        return false; /* Duplicate */

    if (!arp_hash_reserve(ht, ht->num_entries + 1))
        return false; /* Out of memory — caller keeps the linear fallback */

    arp_hash_put(ht, hand, index);
    ht->num_entries++;
    return true;
}

/* Initialize parsing context */
int ARP_InitContext(arp_context_t *ctx, const char *range_string)
{
    if (!ctx || !range_string)
        return 0;

    memset(ctx, 0, sizeof(arp_context_t));
    size_t range_length = strnlen(range_string, 8193);
    if (range_length > 8192) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                 "Range string exceeds maximum length");
        return 0;
    }

    ctx->input = range_string;
    ctx->length = range_length;
    ctx->position = 0;

    /* Allocate initial token array */
    ctx->token_capacity = 32;
    ctx->tokens = malloc(ctx->token_capacity * sizeof(arp_token_t));
    if (!ctx->tokens)
    {
        snprintf(ctx->error_message, sizeof(ctx->error_message), "Memory allocation failed");
        return 0;
    }

    return 1;
}

/* Check if game type is Omaha variant */
static int arp_is_omaha_game(enum_game_t game_type)
{
    switch (game_type)
    {
    case game_omaha:
    case game_omaha5:
    case game_omaha6:
    case game_omaha8:
    case game_omaha85:
    case game_omaha86:
        return 1;
    case game_holdem:
    case game_holdem8:
    case game_7stud:
    case game_7stud8:
    case game_7studnsq:
    case game_razz:
    case game_5draw:
    case game_5draw8:
    case game_5drawnsq:
    case game_lowball:
    case game_lowball27:
    case game_sdholdem:
    case game_doubleflop_holdem:
    case game_drawmaha:
    case game_pineapple:
    case game_27_triple_draw:
    case game_a5_triple_draw:
    case game_badacey:
    case game_badeucy:
    case game_badugi:
    case game_fusion:
    case game_courchevel:
    case game_courchevel8:
    case game_irish:
    case game_ofc:
    case game_manila:
    case game_NUMGAMES:
        return 0;
    default:
        return 0; /* Default case for any new game types */
    }
}

/* Initialize range with specific capacity */
static int arp_init_range_with_capacity(arp_range_t *range, size_t estimated_capacity) {
    if (!range)
        return 0;

    memset(range, 0, sizeof(arp_range_t));

    /* Use estimated capacity, minimum 16, maximum ARP_MAX_RANGE_SIZE */
    if (estimated_capacity < 16)
        estimated_capacity = 16;
    if (estimated_capacity > ARP_MAX_RANGE_SIZE)
        estimated_capacity = ARP_MAX_RANGE_SIZE;

    range->capacity = estimated_capacity;
    range->hands = malloc(range->capacity * sizeof(StdDeck_CardMask));
    range->weights = malloc(range->capacity * sizeof(double));

    if (!range->hands || !range->weights) {
        if (range->hands) free(range->hands);
        if (range->weights) free(range->weights);
        return 0;
    }

    range->count = 0;
    range->total_weight = 0.0;
    range->has_weights = false;
    range->is_percentage = false;
    range->percentage_used = 0.0f;
    range->internal_data = NULL; /* Hash table initially NULL */

    return 1;
}

/* Initialize range structure with game type */
static int arp_init_range_with_game(arp_range_t *range, enum_game_t game_type)
{
    if (!arp_init_range(range))
        return 0;

    range->game_type = game_type;
    range->is_omaha = arp_is_omaha_game(game_type);

    return 1;
}

/* Initialize range structure with game type and capacity */
static int arp_init_range_with_game_and_capacity(arp_range_t *range, enum_game_t game_type, size_t capacity)
{
    if (!arp_init_range_with_capacity(range, capacity))
        return 0;

    range->game_type = game_type;
    range->is_omaha = arp_is_omaha_game(game_type);

    return 1;
}

/* Estimate range size based on token type */
static size_t arp_estimate_range_size(const arp_token_t *token) {
    if (!token)
        return 256;  /* Default */

    switch (token->type) {
        case ARP_TOKEN_UNKNOWN:
        case ARP_TOKEN_OPERATION:
        case ARP_TOKEN_COMMA:
        case ARP_TOKEN_LPAREN:
        case ARP_TOKEN_RPAREN:
        case ARP_TOKEN_END:
            return 256; /* Should not happen for data-carrying tokens */

        case ARP_TOKEN_PAIR_RANGE: {
            /* Each pair has 6 combinations: C(4,2) = 6 */
            int num_pairs = token->data.pair_range.high_rank -
                           token->data.pair_range.low_rank + 1;
            if (num_pairs < 1) num_pairs = 1;
            return num_pairs * 6;
        }

        case ARP_TOKEN_HAND_RANGE: {
            /* AK-AJ: 3 hands * (4 suited + 12 offsuit) */
            int num_hands = token->data.hand_range.rank2_high -
                           token->data.hand_range.rank2_low + 1;
            if (num_hands < 1) num_hands = 1;

            int combos_per_hand;
            if (token->data.hand_range.both) {
                combos_per_hand = 16;  /* 4 suited + 12 offsuit */
            } else if (token->data.hand_range.suited) {
                combos_per_hand = 4;   /* Suited only */
            } else {
                combos_per_hand = 12;  /* Offsuit only */
            }

            return num_hands * combos_per_hand;
        }

        case ARP_TOKEN_SUITED:
            return 4;   /* 4 suits */

        case ARP_TOKEN_OFFSUIT:
            return 12;  /* 4 * 3 offsuit combinations */

        case ARP_TOKEN_BOTH:
            return 16;  /* 4 + 12 */

        case ARP_TOKEN_SPECIFIC_HAND:
            return 1;   /* Exact hand */

        case ARP_TOKEN_PERCENTAGE:
            /* Estimate based on percentage */
            if (token->data.percentage <= 0.05f)
                return 70; /* 5% of 1326 is ~66 */
            else if (token->data.percentage <= 0.10f)
                return 135;
            else if (token->data.percentage <= 0.20f)
                return 270;
            else if (token->data.percentage <= 0.50f)
                return 670;
            else
                return 1326;

        case ARP_TOKEN_PLO_PATTERN:
            /* PLO patterns can be large */
            return 512;

        case ARP_TOKEN_PLO_CATEGORY:
            return 256;

        case ARP_TOKEN_STUD_PATTERN:
            return 128;

        default:
            return 256;  /* Default fallback */
    }
}

/* Check if a string looks like a PLO pattern */
static int arp_is_plo_pattern(const char *str, size_t len)
{
    if (!str || len < 4)
        return 0;

    /* Check for common PLO patterns:
     * - 4+ characters with ranks and possibly 'x'
     * - Ends with suit indicators (ds, ss, r, etc.)
     * - Contains specific PLO notation
     */

    int rank_count = 0;
    int x_count = 0;
    bool has_suit_suffix = false;

    /* Check for suit suffixes */
    if (len >= 6)
    {
        const char *end = str + len - 2;
        if (strncmp(end, "ds", 2) == 0 || strncmp(end, "ss", 2) == 0 ||
            (len >= 7 && str[len - 1] == 'r'))
        {
            has_suit_suffix = true;
        }
    }

    /* Count ranks and x's first to determine if this could be PLO */
    for (size_t i = 0; i < len; i++)
    {
        char c = str[i];
        if (arp_char_to_rank(c) >= 0)
        {
            rank_count++;
        }
        else if (c == 'x' || c == 'X')
        {
            x_count++;
        }
        else if (c == 'd' || c == 's' || c == 'r')
        {
            /* Suit indicators at end */
            break;
        }
    }

    /* PLO patterns typically have 4+ cards (ranks or x's) */
    int total_cards = rank_count + x_count;

    /* If we have 4+ cards, check if it contains Hold'em operators that would
     * indicate it's actually a range expression like "AA-KK" (4 ranks but with '-')
     */
    if (total_cards >= 4)
    {
        /* Check for Hold'em range operators in the main part (before suit suffixes) */
        for (size_t i = 0; i < len; i++)
        {
            char c = str[i];
            /* If we hit a suit suffix, we're done checking */
            if (c == 'd' || c == 's' || c == 'r')
                break;
            /* If we find Hold'em operators, this is NOT a PLO pattern */
            if (c == '-' || c == ',' || c == '+' || c == ' ' || c == '!')
            {
                return 0;
            }
        }
        /* Has 4+ cards and no Hold'em operators - it's a PLO pattern */
        return 1;
    }

    /* Also check for specific 4-card hands like AsKdQhJc */
    if (len == 8)
    {
        /* Check if it's alternating rank-suit pattern (4 cards) */
        int valid = 1;
        for (size_t i = 0; i < 8; i += 2)
        {
            if (arp_char_to_rank(str[i]) < 0)
            {
                valid = 0;
                break;
            }
            if (i + 1 < 8 && arp_char_to_suit(str[i + 1]) < 0)
            {
                valid = 0;
                break;
            }
        }
        if (valid)
            return 1;
    }

    /* Suppress unused variable warning */
    (void)has_suit_suffix;

    return 0;
}

static int arp_lookup_plo_category(const char *str, size_t len, PLOHandCategory *category)
{
    if (!str || len == 0 || !category)
        return 0;

    char normalized[64];
    size_t out = 0;

    for (size_t i = 0; i < len && out < sizeof(normalized) - 1; ++i)
    {
        unsigned char ch = (unsigned char)str[i];
        if (isalnum(ch))
        {
            normalized[out++] = (char)toupper(ch);
        }
    }

    normalized[out] = '\0';

    if (out == 0)
        return 0;

    bool all_digits = true;
    for (size_t i = 0; i < out; ++i)
    {
        if (!isdigit((unsigned char)normalized[i]))
        {
            all_digits = false;
            break;
        }
    }

    if (all_digits)
    {
        /* The digits were validated above, but atoi is undefined on overflow:
         * a long run of digits would not merely fall outside the range test. */
        char *end = NULL;
        long value;
        errno = 0;
        value = strtol(normalized, &end, 10);
        if (end != normalized && *end == '\0' && errno != ERANGE &&
            value >= (long)PLO_CAT_UNPAIRED_DS && value <= (long)PLO_CAT_RAGGED_RB)
        {
            *category = (PLOHandCategory)value;
            return 1;
        }
        return 0;
    }

    static const struct
    {
        const char *key;
        PLOHandCategory value;
    } category_map[] = {
        {"UNPAIREDDS", PLO_CAT_UNPAIRED_DS},
        {"UNPAIREDDOUBLESUITED", PLO_CAT_UNPAIRED_DS},
        {"UNPAIREDSS", PLO_CAT_UNPAIRED_SS},
        {"UNPAIREDSINGLESUITED", PLO_CAT_UNPAIRED_SS},
        {"UNPAIREDRAINBOW", PLO_CAT_UNPAIRED_RB},
        {"UNPAIREDRB", PLO_CAT_UNPAIRED_RB},

        {"PAIRDS", PLO_CAT_PAIR_DS},
        {"ONEPAIRDS", PLO_CAT_PAIR_DS},
        {"PAIRSS", PLO_CAT_PAIR_SS},
        {"ONEPAIRSS", PLO_CAT_PAIR_SS},
        {"PAIRRAINBOW", PLO_CAT_PAIR_RB},
        {"PAIRRB", PLO_CAT_PAIR_RB},

        {"TWOPAIRDS", PLO_CAT_TWO_PAIR_DS},
        {"TWOPAIREDS", PLO_CAT_TWO_PAIR_DS},
        {"TWOPAIRSS", PLO_CAT_TWO_PAIR_SS},
        {"TWOPAIRESS", PLO_CAT_TWO_PAIR_SS},
        {"TWOPAIRRAINBOW", PLO_CAT_TWO_PAIR_RB},
        {"TWOPAIRRB", PLO_CAT_TWO_PAIR_RB},

        {"TRIPSDS", PLO_CAT_TRIPS_DS},
        {"SETDS", PLO_CAT_TRIPS_DS},
        {"TRIPSSS", PLO_CAT_TRIPS_SS},
        {"SETSS", PLO_CAT_TRIPS_SS},
        {"TRIPSRAINBOW", PLO_CAT_TRIPS_RB},
        {"TRIPSRB", PLO_CAT_TRIPS_RB},
        {"SETRB", PLO_CAT_TRIPS_RB},

        {"AADS", PLO_CAT_AA_DS},
        {"ACESDS", PLO_CAT_AA_DS},
        {"AASS", PLO_CAT_AA_SS},
        {"ACESSS", PLO_CAT_AA_SS},
        {"AARAINBOW", PLO_CAT_AA_RB},
        {"AARB", PLO_CAT_AA_RB},
        {"ACESRB", PLO_CAT_AA_RB},

        {"BROADWAYDS", PLO_CAT_BROADWAY_DS},
        {"BROADWAYDOUBLESUITED", PLO_CAT_BROADWAY_DS},
        {"BROADWAYSS", PLO_CAT_BROADWAY_SS},
        {"BROADWAYSINGLESUITED", PLO_CAT_BROADWAY_SS},
        {"BROADWAYRAINBOW", PLO_CAT_BROADWAY_RB},
        {"BROADWAYRB", PLO_CAT_BROADWAY_RB},
        {"BROADWAYHEAVYDS", PLO_CAT_BROADWAY_DS},
        {"BROADWAYHEAVYSS", PLO_CAT_BROADWAY_SS},
        {"BROADWAYHEAVYRB", PLO_CAT_BROADWAY_RB},

        {"RAGGEDDS", PLO_CAT_RAGGED_DS},
        {"LOWDS", PLO_CAT_RAGGED_DS},
        {"RAGGEDSS", PLO_CAT_RAGGED_SS},
        {"LOWSS", PLO_CAT_RAGGED_SS},
        {"RAGGEDRAINBOW", PLO_CAT_RAGGED_RB},
        {"RAGGEDRB", PLO_CAT_RAGGED_RB},
        {"LOWRAINBOW", PLO_CAT_RAGGED_RB},
        {"LOWRB", PLO_CAT_RAGGED_RB},
    };

    for (size_t i = 0; i < sizeof(category_map) / sizeof(category_map[0]); ++i)
    {
        if (strcmp(normalized, category_map[i].key) == 0)
        {
            *category = category_map[i].value;
            return 1;
        }
    }

    return 0;
}

/* Parse a PLO pattern and add hands to range */
static int arp_parse_plo_pattern(const char *pattern, StdDeck_CardMask dead_cards, arp_range_t *range)
{
    if (!pattern || !range)
        return 0;


    /* Use PLO integration to generate hands */
    OmahaHandList hand_list;
    if (!OmahaHandList_Init(&hand_list, OMAHA_HANDLIST_INITIAL_CAPACITY))
    {
        return 0;
    }

    /* Generate hands using PLO pattern */
    int result = PLO_GenerateHands(pattern, dead_cards, &hand_list);
    if (result < 0)
    {
        OmahaHandList_Free(&hand_list);
        return 0;
    }

    /* Add all generated hands to the range */
    for (int i = 0; i < hand_list.count; i++)
    {
        if (!arp_add_hand_to_range(range, hand_list.hands[i], 1.0))
        {
            OmahaHandList_Free(&hand_list);
            return 0;
        }
    }

    OmahaHandList_Free(&hand_list);
    return 1;
}

/* Expand Omaha percentage range */
static int arp_expand_omaha_percentage(float percentage, enum_game_t game_type, StdDeck_CardMask dead_cards, arp_range_t *range)
{
    static const char *tiers_pct[] = {
        /* Tier 1: Top ~2% — ultra-premium */
        "AAxxds, KKxxds, QQxxds, JJxxds, AAxxss, KKxxss",
        /* Tier 2: Top ~5% — add premium pairs and top rundowns */
        "QQxxss, JJxxss, TTxxds, TTxxss, AKQJds, AKQTds, AKJTds, AQJTds, KQJTds",
        /* Tier 3: Top ~10% — add premium pairs any suit, top rundowns */
        "AAxx, KKxx, QQxx, JJxx, TTxx, 99xxds, 99xxss, AKQJ, AKQT, AKJT, AQJT, KQJT",
        /* Tier 4: Top ~15% — add 99, 88, more rundowns */
        "99xx, 88xxds, 88xxss, AKQ9, AKJ9, AQJ9, KQJ9, AKT9, AQT9, KQT9",
        /* Tier 5: Top ~20% — add 77, broadway combos */
        "77xxds, 77xxss, AJT9, KJT9, QJT9, AK98, AQ98, KQ98, AJ98, KJ98, QJ98, AT98, KT98, QT98, JT98",
        /* Tier 6: Top ~30% — add 55-22, more rundowns, connectors */
        "66xx, 55xx, 44xx, 33xx, 22xx, AKQx, AKJx, AQJx, KQJx, AKTx, AQTx, KQTx, AJTx, KJTx, QJTx, T9xy, 98xy, 87xy",
    };

    if (!range || percentage <= 0.0f || percentage > 1.0f)
        return 0;

    if (!arp_is_omaha_game(game_type))
        return 0;

    arp_range_t tier_range;
    int tiers_count = (int)(sizeof(tiers_pct) / sizeof(tiers_pct[0]));
    int pct = (int)(percentage * 100.0f + 0.5f);

    for (int t = 0; t < tiers_count; t++)
    {
        if (pct <= 2 + 3 * t)
            break;

        memset(&tier_range, 0, sizeof(tier_range));
        if (!ARP_ParseRange(tiers_pct[t], dead_cards, game_type, &tier_range))
            continue;

        for (size_t i = 0; i < tier_range.count; i++)
            arp_add_hand_to_range(range, tier_range.hands[i], 1.0);
        ARP_FreeRange(&tier_range);
    }

    return range->count > 0 ? 1 : 0;
}

/* Omaha-specific API functions */

/* Parse an Omaha range string */
int ARP_ParseOmahaRange(const char *range_string, StdDeck_CardMask dead_cards,
                        enum_game_t game_type, arp_range_t *result)
{

    if (!arp_is_omaha_game(game_type))
    {
        return 0;
    }

    return ARP_ParseRange(range_string, dead_cards, game_type, result);
}

/* Convert arp_range_t to OmahaHandList */
int ARP_ToOmahaHandList(const arp_range_t *arp_range, OmahaHandList *omaha_list)
{
    if (!arp_range || !omaha_list || !arp_range->is_omaha)
        return 0;

    /* Clear the list first */
    OmahaHandList_Clear(omaha_list);

    /* Add all hands from the range */
    for (size_t i = 0; i < arp_range->count; i++)
    {
        if (!OmahaHandList_AddHand(omaha_list, arp_range->hands[i]))
        {
            return 0;
        }
    }

    return 1;
}

/* Get top percentage of Omaha hands */
int ARP_GetOmahaTopPercentage(float percentage, enum_game_t game_type,
                              StdDeck_CardMask dead_cards, arp_range_t *result)
{
    if (!arp_is_omaha_game(game_type) || !result)
        return 0;

    if (!arp_init_range_with_game(result, game_type))
        return 0;

    return arp_expand_omaha_percentage(percentage, game_type, dead_cards, result);
}

/* Get top percentage of hands (generic) */
int ARP_GetTopPercentage(float percentage, enum_game_t game_type,
                         StdDeck_CardMask dead_cards, arp_range_t *result)
{
    if (!result || percentage <= 0.0f || percentage > 1.0f)
        return 0;

    /* Check cache first (only if no dead cards) */
    StdDeck_CardMask empty;
    StdDeck_CardMask_RESET(empty);

    /* Initialize cache on first use */
    if (!global_cache.initialized) {
        ARP_InitCache();
    }

    if (StdDeck_CardMask_EQUAL(dead_cards, empty)) {
        /* Ensure result is clean before lookup (for realloc safety) */
        memset(result, 0, sizeof(arp_range_t));

        if (arp_cache_lookup(game_type, percentage, result)) {
            return 1;  /* Success from cache */
        }
    }

    /* Estimate capacity */
    arp_token_t token;
    token.type = ARP_TOKEN_PERCENTAGE;
    token.data.percentage = percentage;
    size_t estimated_size = arp_estimate_range_size(&token);

    if (!arp_init_range_with_game_and_capacity(result, game_type, estimated_size))
        return 0;

    result->is_percentage = true;
    result->percentage_used = percentage;

    int success;
    if (arp_is_omaha_game(game_type)) {
        success = arp_expand_omaha_percentage(percentage, game_type, dead_cards, result);
    } else {
        success = arp_expand_percentage(percentage, game_type, dead_cards, result);
    }

    /* Store in cache if no dead cards and success */
    if (success && StdDeck_CardMask_EQUAL(dead_cards, empty)) {
        arp_cache_insert(game_type, percentage, result);
    }

    return success;
}

/* Parse a PLO pattern and add to range */
int ARP_AddPLOPattern(const char *pattern, StdDeck_CardMask dead_cards, arp_range_t *range)
{
    if (!pattern || !range)
        return 0;

    return arp_parse_plo_pattern(pattern, dead_cards, range);
}

/* Validate an Omaha range string */
int ARP_ValidateOmahaRangeString(const char *range_string, char *error_buffer, size_t error_buffer_size)
{
    if (!range_string)
        return 0;

    arp_context_t ctx;
    if (!ARP_InitContext(&ctx, range_string))
    {
        if (error_buffer && error_buffer_size > 0)
        {
            snprintf(error_buffer, error_buffer_size, "Failed to initialize parser");
        }
        return 0;
    }

    int result = arp_tokenize(&ctx);

    if (!result && error_buffer && error_buffer_size > 0)
    {
        strncpy(error_buffer, ctx.error_message, error_buffer_size - 1);
        error_buffer[error_buffer_size - 1] = '\0';
    }

    ARP_FreeContext(&ctx);
    return result;
}

/* ============================================================================
 * Stud Top Percentage
 * ============================================================================ */

/*
 * Stud 3rd-street hand ranking table.
 * Ordered from strongest to weakest starting hands.
 * Each entry is a pattern string parseable by ARP_ParseStudPattern.
 * Cumulative percentage is computed from combo counts.
 *
 * Reference: standard 7-Card Stud hand strength rankings.
 * Total starting hands = C(52,3) = 22100.
 */
typedef struct {
    const char *pattern;
    int tier;  /* 1=premium, 2=strong, 3=playable, 4=marginal, 5=weak */
} stud_ranking_entry_t;

static const stud_ranking_entry_t stud_hand_rankings[] = {
    /* Tier 1: Rolled-up trips (4 combos each, 13 ranks = 52 total) */
    {"(AA)A", 1},
    {"(KK)K", 1},
    {"(QQ)Q", 1},
    {"(JJ)J", 1},
    {"(TT)T", 1},
    {"(99)9", 1},
    {"(88)8", 1},
    {"(77)7", 1},
    {"(66)6", 1},
    {"(55)5", 1},
    {"(44)4", 1},
    {"(33)3", 1},
    {"(22)2", 1},

    /* Tier 2: High pairs with high kicker */
    {"(AA)K", 2},
    {"(AA)Q", 2},
    {"(AA)J", 2},
    {"(KK)A", 2},
    {"(KK)Q", 2},
    {"(KK)J", 2},
    {"(QQ)A", 2},
    {"(QQ)K", 2},
    {"(QQ)J", 2},

    /* Tier 2: Remaining high pair kickers */
    {"(AA)T", 2},
    {"(KK)T", 2},
    {"(QQ)T", 2},
    {"(JJ)A", 2},
    {"(JJ)K", 2},
    {"(JJ)Q", 2},
    {"(JJ)T", 2},

    /* Tier 3: Medium pairs with high kickers */
    {"(TT)A", 3},
    {"(TT)K", 3},
    {"(TT)Q", 3},
    {"(TT)J", 3},
    {"(99)A", 3},
    {"(99)K", 3},
    {"(99)Q", 3},
    {"(99)J", 3},
    {"(99)T", 3},

    /* Tier 3: Low kickers on high pairs */
    {"(AA)9", 3},
    {"(AA)8", 3},
    {"(AA)7", 3},
    {"(AA)6", 3},
    {"(AA)5", 3},
    {"(AA)4", 3},
    {"(AA)3", 3},
    {"(AA)2", 3},
    {"(KK)9", 3},
    {"(KK)8", 3},
    {"(KK)7", 3},
    {"(KK)6", 3},
    {"(KK)5", 3},
    {"(KK)4", 3},
    {"(KK)3", 3},
    {"(KK)2", 3},
    {"(QQ)9", 3},
    {"(QQ)8", 3},
    {"(QQ)7", 3},
    {"(QQ)6", 3},
    {"(QQ)5", 3},
    {"(QQ)4", 3},
    {"(QQ)3", 3},
    {"(QQ)2", 3},
    {"(JJ)9", 3},
    {"(JJ)8", 3},
    {"(JJ)7", 3},
    {"(JJ)6", 3},
    {"(JJ)5", 3},
    {"(JJ)4", 3},
    {"(JJ)3", 3},
    {"(JJ)2", 3},

    /* Tier 4: Low-medium pairs */
    {"(88)A", 4},
    {"(88)K", 4},
    {"(88)Q", 4},
    {"(88)J", 4},
    {"(88)T", 4},
    {"(88)9", 4},
    {"(77)A", 4},
    {"(77)K", 4},
    {"(77)Q", 4},
    {"(77)J", 4},
    {"(77)T", 4},
    {"(77)9", 4},
    {"(77)8", 4},
    {"(66)A", 4},
    {"(66)K", 4},
    {"(66)Q", 4},
    {"(66)J", 4},
    {"(66)T", 4},
    {"(66)9", 4},
    {"(66)8", 4},
    {"(66)7", 4},

    /* Tier 4: Remaining medium pair kickers */
    {"(TT)9", 4},
    {"(TT)8", 4},
    {"(TT)7", 4},
    {"(TT)6", 4},
    {"(TT)5", 4},
    {"(TT)4", 4},
    {"(TT)3", 4},
    {"(TT)2", 4},
    {"(99)8", 4},
    {"(99)7", 4},
    {"(99)6", 4},
    {"(99)5", 4},
    {"(99)4", 4},
    {"(99)3", 4},
    {"(99)2", 4},

    /* Tier 5: Low pairs */
    {"(55)A", 5},
    {"(55)K", 5},
    {"(55)Q", 5},
    {"(55)J", 5},
    {"(55)T", 5},
    {"(55)9", 5},
    {"(55)8", 5},
    {"(55)7", 5},
    {"(55)6", 5},
    {"(44)A", 5},
    {"(44)K", 5},
    {"(44)Q", 5},
    {"(44)J", 5},
    {"(44)T", 5},
    {"(44)9", 5},
    {"(44)8", 5},
    {"(44)7", 5},
    {"(44)6", 5},
    {"(44)5", 5},
    {"(33)A", 5},
    {"(33)K", 5},
    {"(33)Q", 5},
    {"(33)J", 5},
    {"(33)T", 5},
    {"(33)9", 5},
    {"(33)8", 5},
    {"(33)7", 5},
    {"(33)6", 5},
    {"(33)5", 5},
    {"(33)4", 5},
    {"(22)A", 5},
    {"(22)K", 5},
    {"(22)Q", 5},
    {"(22)J", 5},
    {"(22)T", 5},
    {"(22)9", 5},
    {"(22)8", 5},
    {"(22)7", 5},
    {"(22)6", 5},
    {"(22)5", 5},
    {"(22)4", 5},
    {"(22)3", 5},

    /* Remaining low pair kickers */
    {"(88)7", 5},
    {"(88)6", 5},
    {"(88)5", 5},
    {"(88)4", 5},
    {"(88)3", 5},
    {"(88)2", 5},
    {"(77)6", 5},
    {"(77)5", 5},
    {"(77)4", 5},
    {"(77)3", 5},
    {"(77)2", 5},
    {"(66)5", 5},
    {"(66)4", 5},
    {"(66)3", 5},
    {"(66)2", 5},
    {"(55)4", 5},
    {"(55)3", 5},
    {"(55)2", 5},
    {"(44)3", 5},
    {"(44)2", 5},
    {"(33)2", 5},
};

#define STUD_HAND_RANKINGS_COUNT (sizeof(stud_hand_rankings) / sizeof(stud_hand_rankings[0]))

/* Total 3-card starting hands in Stud = C(52,3) = 22100 */
#define STUD_TOTAL_STARTING_HANDS 22100

int ARP_GetStudTopPercentage(float percentage, enum_game_t game_type,
                             StdDeck_CardMask dead_cards, arp_range_t *result)
{
    (void)game_type;

    if (!result || percentage <= 0.0f || percentage > 1.0f)
        return 0;

    memset(result, 0, sizeof(arp_range_t));

    /* Target number of combos based on percentage of total starting hands */
    int target_combos = (int)(percentage * (float)STUD_TOTAL_STARTING_HANDS + 0.5f);
    int total_combos = 0;

    for (size_t i = 0; i < STUD_HAND_RANKINGS_COUNT; i++)
    {
        if (total_combos >= target_combos)
            break;

        /* Parse the pattern and add matching hands */
        arp_range_t temp;
        memset(&temp, 0, sizeof(arp_range_t));

        if (ARP_ParseStudPattern(stud_hand_rankings[i].pattern, dead_cards, &temp))
        {
            /* Add all hands from temp to result */
            for (size_t j = 0; j < temp.count; j++)
            {
                /* Initialize result arrays on first use */
                if (!result->hands)
                {
                    size_t cap = 1024;
                    result->hands = malloc(cap * sizeof(StdDeck_CardMask));
                    result->weights = malloc(cap * sizeof(double));
                    if (!result->hands || !result->weights) {
                        free(result->hands);
                        free(result->weights);
                        ARP_FreeRange(&temp);
                        return 0;
                    }
                    result->capacity = cap;
                }

                if (result->count >= result->capacity)
                {
                    size_t new_cap = result->capacity * 2;
                    StdDeck_CardMask *nh = realloc(result->hands, new_cap * sizeof(StdDeck_CardMask));
                    double *nw = realloc(result->weights, new_cap * sizeof(double));
                    if (!nh || !nw) {
                        if (nh) result->hands = nh;
                        if (nw) result->weights = nw;
                        ARP_FreeRange(&temp);
                        return 0;
                    }
                    result->hands = nh;
                    result->weights = nw;
                    result->capacity = new_cap;
                }

                result->hands[result->count] = temp.hands[j];
                result->weights[result->count] = temp.weights[j];
                result->count++;
                result->total_weight += temp.weights[j];
            }
            total_combos += (int)temp.count;
            ARP_FreeRange(&temp);
        }

        if (total_combos >= target_combos)
            break;
    }

    result->is_percentage = true;
    result->percentage_used = percentage;
    return (result->count > 0) ? 1 : 0;
}

/* Phase 2: Expression parsing implementation */

/* Create a range node (leaf) */
static arp_expr_node_t *arp_create_range_node(arp_token_t *token)
{
    if (!token)
        return NULL;

    arp_expr_node_t *node = malloc(sizeof(arp_expr_node_t));
    if (!node)
        return NULL;

    node->type = ARP_EXPR_RANGE;
    node->data.range.token = *token;

    return node;
}

/* Create an operation node (internal) */
static arp_expr_node_t *arp_create_operation_node(arp_operation_t op, arp_expr_node_t *left, arp_expr_node_t *right)
{
    arp_expr_node_t *node = malloc(sizeof(arp_expr_node_t));
    if (!node)
        return NULL;

    node->type = ARP_EXPR_OPERATION;
    node->data.operation.op = op;
    node->data.operation.left = left;
    node->data.operation.right = right;

    return node;
}

/* Free expression tree */
void ARP_FreeExpressionTree(arp_expr_node_t *expr_tree)
{
    if (!expr_tree)
        return;

    if (expr_tree->type == ARP_EXPR_OPERATION)
    {
        ARP_FreeExpressionTree(expr_tree->data.operation.left);
        ARP_FreeExpressionTree(expr_tree->data.operation.right);
    }

    free(expr_tree);
}

/* Get current token */
static arp_token_t *arp_current_token(arp_context_t *ctx)
{
    if (!ctx || ctx->current_token >= ctx->token_count)
        return NULL;
    return &ctx->tokens[ctx->current_token];
}

/* Advance to next token */
static void arp_advance_token(arp_context_t *ctx)
{
    if (ctx && ctx->current_token < ctx->token_count)
        ctx->current_token++;
}

/* Check if current token matches type */
static bool arp_match_token(arp_context_t *ctx, arp_token_type_t type)
{
    arp_token_t *token = arp_current_token(ctx);
    return token && token->type == type;
}

/* Parse primary expression (parentheses, ranges, percentages) */
static arp_expr_node_t *arp_parse_primary(arp_context_t *ctx)
{
    arp_token_t *token = arp_current_token(ctx);
    if (!token)
    {
        snprintf(ctx->error_message, sizeof(ctx->error_message), "Unexpected end of expression");
        return NULL;
    }

    /* Handle parentheses */
    if (token->type == ARP_TOKEN_LPAREN)
    {
        arp_advance_token(ctx); /* Skip '(' */
        arp_expr_node_t *expr = arp_parse_expression(ctx);
        if (!expr)
            return NULL;

        token = arp_current_token(ctx);
        if (!token || token->type != ARP_TOKEN_RPAREN)
        {
            snprintf(ctx->error_message, sizeof(ctx->error_message), "Expected ')' after expression");
            ARP_FreeExpressionTree(expr);
            return NULL;
        }
        arp_advance_token(ctx); /* Skip ')' */
        return expr;
    }

    /* Handle range tokens */
    if (token->type == ARP_TOKEN_PERCENTAGE ||
        token->type == ARP_TOKEN_PLO_PATTERN ||
        token->type == ARP_TOKEN_PLO_CATEGORY ||
        token->type == ARP_TOKEN_PAIR_RANGE ||
        token->type == ARP_TOKEN_HAND_RANGE ||
        token->type == ARP_TOKEN_STUD_PATTERN ||
        token->type == ARP_TOKEN_SUITED ||
        token->type == ARP_TOKEN_OFFSUIT ||
        token->type == ARP_TOKEN_BOTH ||
        token->type == ARP_TOKEN_SPECIFIC_HAND)
    {
        arp_expr_node_t *node = arp_create_range_node(token);
        arp_advance_token(ctx);
        return node;
    }

    snprintf(ctx->error_message, sizeof(ctx->error_message), "Unexpected token in expression");
    return NULL;
}

/* Parse factor (unary operators like !) */
static arp_expr_node_t *arp_parse_factor(arp_context_t *ctx)
{
    arp_token_t *token = arp_current_token(ctx);
    if (!token)
        return NULL;

    /* Handle unary NOT operator */
    if (token->type == ARP_TOKEN_OPERATION && token->operation == ARP_OP_EXCLUDE)
    {
        arp_advance_token(ctx); /* Skip '!' */
        arp_expr_node_t *operand = arp_parse_factor(ctx);
        if (!operand)
            return NULL;

        return arp_create_operation_node(ARP_OP_EXCLUDE, operand, NULL);
    }

    return arp_parse_primary(ctx);
}

/* Parse term (+ and - operators) */
static arp_expr_node_t *arp_parse_term(arp_context_t *ctx)
{
    arp_expr_node_t *left = arp_parse_factor(ctx);
    if (!left)
        return NULL;

    while (true)
    {
        arp_token_t *token = arp_current_token(ctx);
        if (!token || token->type != ARP_TOKEN_OPERATION)
            break;

        if (token->operation != ARP_OP_ADD && token->operation != ARP_OP_SUBTRACT)
            break;

        arp_operation_t op = token->operation;
        arp_advance_token(ctx);

        arp_expr_node_t *right = arp_parse_factor(ctx);
        if (!right)
        {
            ARP_FreeExpressionTree(left);
            return NULL;
        }

        left = arp_create_operation_node(op, left, right);
        if (!left)
        {
            ARP_FreeExpressionTree(right);
            return NULL;
        }
    }

    return left;
}

/* Parse expression (comma operator - lowest precedence) */
static arp_expr_node_t *arp_parse_expression(arp_context_t *ctx)
{
    arp_expr_node_t *left = arp_parse_term(ctx);
    if (!left)
        return NULL;

    while (true)
    {
        arp_token_t *token = arp_current_token(ctx);
        if (!token || token->type != ARP_TOKEN_COMMA)
            break;

        arp_advance_token(ctx); /* Skip ',' */

        arp_expr_node_t *right = arp_parse_term(ctx);
        if (!right)
        {
            ARP_FreeExpressionTree(left);
            return NULL;
        }

        /* Comma is treated as union (addition) */
        left = arp_create_operation_node(ARP_OP_ADD, left, right);
        if (!left)
        {
            ARP_FreeExpressionTree(right);
            return NULL;
        }
    }

    return left;
}

/* Range operations implementation */

/* Union of two ranges (public wrapper) */
int ARP_UnionRanges(const arp_range_t *range1, const arp_range_t *range2, arp_range_t *result)
{
    return arp_range_union(range1, range2, result);
}

/* Union of two ranges */
static int arp_range_union(const arp_range_t *range1, const arp_range_t *range2, arp_range_t *result)
{
    if (!range1 || !range2 || !result)
    {
        return 0;
    }

    /* Initialize result with range1 */
    if (!arp_init_range_with_game(result, range1->game_type))
    {
        return 0;
    }

    /* Add all hands from range1 */
    for (size_t i = 0; i < range1->count; i++)
    {
        double w = range1->weights ? range1->weights[i] : 1.0;
        if (!arp_add_hand_to_range(result, range1->hands[i], w))
        {
            ARP_FreeRange(result);
            return 0;
        }
    }

    /* Add all hands from range2 (duplicates are automatically filtered) */
    for (size_t i = 0; i < range2->count; i++)
    {
        double w = range2->weights ? range2->weights[i] : 1.0;
        if (!arp_add_hand_to_range(result, range2->hands[i], w))
        {
            ARP_FreeRange(result);
            return 0;
        }
    }

    if (range1->has_weights || range2->has_weights)
        result->has_weights = true;

    return 1;
}

/* Subtract range2 from range1 (public wrapper) */
int ARP_SubtractRanges(const arp_range_t *range1, const arp_range_t *range2, arp_range_t *result)
{
    return arp_range_subtract(range1, range2, result);
}

/* Subtract range2 from range1 */
static int arp_range_subtract(const arp_range_t *range1, const arp_range_t *range2, arp_range_t *result)
{
    if (!range1 || !range2 || !result)
        return 0;

    /* Initialize result */
    if (!arp_init_range_with_game(result, range1->game_type))
        return 0;

    for (size_t i = 0; i < range1->count; ++i)
    {
        double w = range1->weights ? range1->weights[i] : 1.0;
        if (!arp_add_hand_to_range(result, range1->hands[i], w))
        {
            ARP_FreeRange(result);
            return 0;
        }
    }

    if (!arp_remove_range(result, range2))
    {
        ARP_FreeRange(result);
        return 0;
    }

    return 1;
}

/* Complement of a range (all possible hands minus the range) */
static int arp_range_complement(const arp_range_t *range, enum_game_t game_type, arp_range_t *result)
{
    if (!range || !result)
        return 0;

    /* Initialize result */
    if (!arp_init_range_with_game(result, game_type))
        return 0;

    if (arp_is_omaha_game(game_type))
    {
        /* For Omaha, generate all C(52,4) combinations */
        /* This is computationally expensive and should be used with caution */
        int cards[4];
        for (cards[0] = 0; cards[0] < 52; cards[0]++) {
            for (cards[1] = cards[0] + 1; cards[1] < 52; cards[1]++) {
                for (cards[2] = cards[1] + 1; cards[2] < 52; cards[2]++) {
                    for (cards[3] = cards[2] + 1; cards[3] < 52; cards[3]++) {
                        StdDeck_CardMask hand;
                        StdDeck_CardMask_RESET(hand);
                        StdDeck_CardMask_SET(hand, cards[0]);
                        StdDeck_CardMask_SET(hand, cards[1]);
                        StdDeck_CardMask_SET(hand, cards[2]);
                        StdDeck_CardMask_SET(hand, cards[3]);

                        bool found_in_range = false;
                        for (size_t j = 0; j < range->count; j++) {
                            if (StdDeck_CardMask_EQUAL(hand, range->hands[j])) {
                                found_in_range = true;
                                break;
                            }
                        }

                        if (!found_in_range) {
                            if (!arp_add_hand_to_range(result, hand, 1.0)) {
                               return 0;
                            }
                        }
                    }
                }
            }
        }
    }
    else /* Hold'em */
    {
        /* Generate all C(52,2) combinations */
        for (int c1 = 0; c1 < 52; c1++) {
            for (int c2 = c1 + 1; c2 < 52; c2++) {
                StdDeck_CardMask hand;
                StdDeck_CardMask_RESET(hand);
                StdDeck_CardMask_SET(hand, c1);
                StdDeck_CardMask_SET(hand, c2);

                bool found_in_range = false;
                for (size_t i = 0; i < range->count; i++) {
                    if (StdDeck_CardMask_EQUAL(hand, range->hands[i])) {
                        found_in_range = true;
                        break;
                    }
                }

                if (!found_in_range) {
                    if (!arp_add_hand_to_range(result, hand, 1.0)) {
                        return 0;
                    }
                }
            }
        }
    }

    return 1;
}

static bool arp_is_unary_exclude_node(const arp_expr_node_t *node)
{
    return node &&
           node->type == ARP_EXPR_OPERATION &&
           node->data.operation.op == ARP_OP_EXCLUDE &&
           node->data.operation.right == NULL;
}

static void arp_range_move(arp_range_t *dest, arp_range_t *src)
{
    if (!dest || !src)
        return;

    *dest = *src;
    memset(src, 0, sizeof(*src));
}

static int arp_apply_token_operation(arp_range_t *accumulator, bool *has_accumulator,
                                     arp_range_t *token_range, arp_operation_t op,
                                     enum_game_t game_type)
{
    if (!accumulator || !has_accumulator || !token_range)
        return 0;

    if (!*has_accumulator)
    {
        if (op == ARP_OP_SUBTRACT || op == ARP_OP_EXCLUDE)
        {
            arp_range_t complement;
            memset(&complement, 0, sizeof(complement));
            if (!arp_range_complement(token_range, game_type, &complement))
            {
                return 0;
            }
            complement.is_percentage = false;
            complement.percentage_used = 0.0f;

            arp_range_move(accumulator, &complement);
            *has_accumulator = true;
            return 1;
        }

        arp_range_move(accumulator, token_range);
        *has_accumulator = true;
        return 1;
    }

    arp_range_t new_range;
    memset(&new_range, 0, sizeof(new_range));
    int success = 0;

    switch (op)
    {
    case ARP_OP_SUBTRACT:
    case ARP_OP_EXCLUDE:
        success = arp_range_subtract(accumulator, token_range, &new_range);
        break;
    case ARP_OP_ADD:
    case ARP_OP_NONE:
    default:
        success = arp_range_union(accumulator, token_range, &new_range);
        break;
    }

    if (!success)
    {
        return 0;
    }

    new_range.is_percentage = accumulator->is_percentage || token_range->is_percentage;
    if (new_range.is_percentage)
    {
        new_range.percentage_used = accumulator->is_percentage ? accumulator->percentage_used
                                                               : token_range->percentage_used;
    }

    ARP_FreeRange(accumulator);
    arp_range_move(accumulator, &new_range);
    return 1;
}

/* Evaluate a single range token */
static int arp_evaluate_range_token(arp_token_t *token, StdDeck_CardMask dead_cards,
                                    enum_game_t game_type, arp_range_t *result)
{
    if (!token || !result)
        return 0;

    switch (token->type)
    {
    case ARP_TOKEN_PERCENTAGE:
        /* Check cache for percentage if no dead cards */
        {
            StdDeck_CardMask empty;
            StdDeck_CardMask_RESET(empty);

            if (StdDeck_CardMask_EQUAL(dead_cards, empty)) {
                if (arp_cache_lookup(game_type, token->data.percentage, result)) {
                    return 1;
                }
            }

            /* Normal calculation */
            result->is_percentage = true;
            result->percentage_used = token->data.percentage;
            int success = 0;
            if (arp_is_omaha_game(game_type))
            {
                success = arp_expand_omaha_percentage(token->data.percentage, game_type, dead_cards, result);
            }
            else
            {
                success = arp_expand_percentage(token->data.percentage, game_type, dead_cards, result);
            }

            /* Cache result if successful and no dead cards */
            if (success && StdDeck_CardMask_EQUAL(dead_cards, empty)) {
                arp_cache_insert(game_type, token->data.percentage, result);
            }

            return success;
        }

    case ARP_TOKEN_PLO_PATTERN:
        return arp_parse_plo_pattern(token->data.plo.pattern, dead_cards, result);

    case ARP_TOKEN_PAIR_RANGE:
        return arp_expand_pair_range(token->data.pair_range.high_rank,
                                     token->data.pair_range.low_rank,
                                     dead_cards, result);

    case ARP_TOKEN_HAND_RANGE:
        return arp_expand_nonpropair_range(token->data.hand_range.rank1_high,
                                           token->data.hand_range.rank2_high,
                                           token->data.hand_range.rank2_low,
                                           token->data.hand_range.suited || token->data.hand_range.both,
                                           token->data.hand_range.offsuit || token->data.hand_range.both,
                                           dead_cards, result);

    case ARP_TOKEN_STUD_PATTERN:
        return ARP_ParseStudPattern(token->data.stud.pattern, dead_cards, result);

    case ARP_TOKEN_SUITED:
        return arp_expand_hand_range(token->data.hand.rank1, token->data.hand.rank2,
                                     true, false, dead_cards, result);

    case ARP_TOKEN_OFFSUIT:
        return arp_expand_hand_range(token->data.hand.rank1, token->data.hand.rank2,
                                     false, true, dead_cards, result);

    case ARP_TOKEN_BOTH:
        return arp_expand_hand_range(token->data.hand.rank1, token->data.hand.rank2,
                                     true, true, dead_cards, result);

    case ARP_TOKEN_SPECIFIC_HAND:
    {
        int card1 = StdDeck_MAKE_CARD(token->data.specific.rank1, token->data.specific.suit1);
        int card2 = StdDeck_MAKE_CARD(token->data.specific.rank2, token->data.specific.suit2);

        StdDeck_CardMask hand;
        StdDeck_CardMask_RESET(hand);
        StdDeck_CardMask_SET(hand, card1);
        StdDeck_CardMask_SET(hand, card2);

        if (!StdDeck_CardMask_ANY_SET(dead_cards, hand))
        {
        return arp_add_hand_to_range(result, hand, 1.0);
        }
        return 1; /* Hand is dead, but not an error */
    }

    case ARP_TOKEN_PLO_CATEGORY:
    {
        if (!arp_is_omaha_game(game_type) || token->data.plo.category == PLO_CAT_INVALID)
        {
            return 0;
        }

        OmahaHandList hand_list;
        if (!OmahaHandList_Init(&hand_list, OMAHA_HANDLIST_INITIAL_CAPACITY))
        {
            return 0;
        }

        int generated = PLO_GetCategoryHands(token->data.plo.category, dead_cards, &hand_list);
        if (generated < 0)
        {
            OmahaHandList_Free(&hand_list);
            return 0;
        }

        for (int i = 0; i < hand_list.count; ++i)
        {
            if (!arp_add_hand_to_range(result, hand_list.hands[i], 1.0))
            {
                OmahaHandList_Free(&hand_list);
                return 0;
            }
        }

        OmahaHandList_Free(&hand_list);
        return 1;
    }

    case ARP_TOKEN_UNKNOWN:
    case ARP_TOKEN_OPERATION:
    case ARP_TOKEN_COMMA:
    case ARP_TOKEN_LPAREN:
    case ARP_TOKEN_RPAREN:
    case ARP_TOKEN_END:
    default:
        return 0;
    }
}

/* Evaluate expression tree recursively */
static int arp_evaluate_expression_node(arp_expr_node_t *node, StdDeck_CardMask dead_cards,
                                         enum_game_t game_type, arp_range_t *result)
{
    if (!node || !result)
        return 0;

    if (node->type == ARP_EXPR_RANGE)
    {
        /* Leaf node - evaluate the range token */
        size_t estimated_size = arp_estimate_range_size(&node->data.range.token);
        if (!arp_init_range_with_game_and_capacity(result, game_type, estimated_size))
            return 0;

        if (!arp_evaluate_range_token(&node->data.range.token, dead_cards, game_type, result))
            return 0;

        if (node->data.range.token.has_weight)
        {
            arp_apply_weight_to_range(result, node->data.range.token.weight);
        }

        return 1;
    }
    else if (node->type == ARP_EXPR_OPERATION)
    {
        /* Internal node - evaluate operation */
        arp_operation_t op = node->data.operation.op;

        if (op == ARP_OP_EXCLUDE)
        {
            /* Unary NOT operation */
            arp_range_t operand_range;
            if (!arp_evaluate_expression_node(node->data.operation.left, dead_cards, game_type, &operand_range))
                return 0;

            int success = arp_range_complement(&operand_range, game_type, result);
            ARP_FreeRange(&operand_range);
            return success;
        }
        else
        {
            /* Binary operations */
            arp_range_t left_range;

            if (!arp_evaluate_expression_node(node->data.operation.left, dead_cards, game_type, &left_range))
            {
                return 0;
            }

            if (op == ARP_OP_ADD && arp_is_unary_exclude_node(node->data.operation.right))
            {
                arp_range_t exclude_range;
                if (!arp_evaluate_expression_node(node->data.operation.right->data.operation.left,
                                                  dead_cards, game_type, &exclude_range))
                {
                    ARP_FreeRange(&left_range);
                    return 0;
                }

                int success = arp_range_subtract(&left_range, &exclude_range, result);
                ARP_FreeRange(&left_range);
                ARP_FreeRange(&exclude_range);
                return success;
            }

            arp_range_t right_range;
            if (!arp_evaluate_expression_node(node->data.operation.right, dead_cards, game_type, &right_range))
            {
                ARP_FreeRange(&left_range);
                return 0;
            }

            int success = 0;
            switch (op)
            {
            case ARP_OP_ADD:
                success = arp_range_union(&left_range, &right_range, result);
                break;
            case ARP_OP_SUBTRACT:
                success = arp_range_subtract(&left_range, &right_range, result);
                break;
            case ARP_OP_NONE:
                success = 0;
                break;
            case ARP_OP_EXCLUDE:
                success = 0;
                break;
            default:
                success = 0;
                break;
            }

            ARP_FreeRange(&left_range);
            ARP_FreeRange(&right_range);
            return success;
        }
    }

    return 0;
}

/* Free parsing context */
void ARP_FreeContext(arp_context_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->tokens)
    {
        free(ctx->tokens);
        ctx->tokens = NULL;
    }

    if (ctx->expr_tree)
    {
        ARP_FreeExpressionTree(ctx->expr_tree);
        ctx->expr_tree = NULL;
    }

    memset(ctx, 0, sizeof(arp_context_t));
}

/* Internal helper to parse with an existing context */
static int arp_parse_with_context(arp_context_t *ctx, StdDeck_CardMask dead_cards,
                                  enum_game_t game_type, arp_range_t *result)
{
    if (!ctx || !result)
        return 0;

    /* Tokenize the expression */
    if (!arp_tokenize(ctx))
    {
        return 0;
    }

    /* Parse expression into tree */
    ctx->current_token = 0;
    ctx->expr_tree = arp_parse_expression(ctx);
    if (!ctx->expr_tree)
    {
        return 0;
    }

    /* Check that we consumed all tokens */
    if (ctx->current_token < ctx->token_count)
    {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                 "Unexpected tokens at end of expression");
        return 0;
    }

    /* Evaluate the expression tree */
    int success = arp_evaluate_expression_node(ctx->expr_tree, dead_cards, game_type, result);
    if (success)
    {
        arp_normalize_range_weights(result);
    }

    return success;
}

/* Phase 2: Public API functions for complex expressions */

/* Parse complex expression with operators and parentheses */
int ARP_ParseComplexExpression(const char *expression, StdDeck_CardMask dead_cards,
                               enum_game_t game_type, arp_range_t *result)
{
    if (!expression || !result)
        return 0;

    /* Initialize parsing context */
    arp_context_t ctx;
    if (!ARP_InitContext(&ctx, expression))
    {
        return 0;
    }

    int success = arp_parse_with_context(&ctx, dead_cards, game_type, result);

    ARP_FreeContext(&ctx);
    return success;
}

/* Evaluate expression tree */
int ARP_EvaluateExpressionTree(arp_expr_node_t *expr_tree, StdDeck_CardMask dead_cards,
                               enum_game_t game_type, arp_range_t *result)
{
    return arp_evaluate_expression_node(expr_tree, dead_cards, game_type, result);
}

/* Validate complex expression */
int ARP_ValidateComplexExpression(const char *expression, char *error_buffer, size_t error_buffer_size)
{
    if (!expression)
    {
        if (error_buffer && error_buffer_size > 0)
        {
            snprintf(error_buffer, error_buffer_size, "Expression is NULL");
        }
        return 0;
    }

    /* Initialize parsing context */
    arp_context_t ctx;
    if (!ARP_InitContext(&ctx, expression))
    {
        if (error_buffer && error_buffer_size > 0)
        {
            snprintf(error_buffer, error_buffer_size, "Failed to initialize parser");
        }
        return 0;
    }

    /* Tokenize the expression */
    if (!arp_tokenize(&ctx))
    {
        if (error_buffer && error_buffer_size > 0)
        {
            strncpy(error_buffer, ctx.error_message, error_buffer_size - 1);
            error_buffer[error_buffer_size - 1] = '\0';
        }
        ARP_FreeContext(&ctx);
        return 0;
    }

    /* Try to parse expression into tree */
    ctx.current_token = 0;
    ctx.expr_tree = arp_parse_expression(&ctx);
    if (!ctx.expr_tree)
    {
        if (error_buffer && error_buffer_size > 0)
        {
            strncpy(error_buffer, ctx.error_message, error_buffer_size - 1);
            error_buffer[error_buffer_size - 1] = '\0';
        }
        ARP_FreeContext(&ctx);
        return 0;
    }

    /* Check that we consumed all tokens */
    if (ctx.current_token < ctx.token_count)
    {
        if (error_buffer && error_buffer_size > 0)
        {
            snprintf(error_buffer, error_buffer_size, "Unexpected tokens at end of expression");
        }
        ARP_FreeContext(&ctx);
        return 0;
    }

    ARP_FreeContext(&ctx);
    return 1;
}

/* Utility Functions */

size_t ARP_CountCombinations(
    const char *range_string,
    StdDeck_CardMask dead_cards,
    enum_game_t game_type)
{
    if (!range_string)
        return 0;

    arp_range_t range;
    if (!ARP_ParseRange(range_string, dead_cards, game_type, &range)) {
        return 0;
    }

    size_t count = range.count;
    ARP_FreeRange(&range);

    return count;
}

int ARP_CloneRange(const arp_range_t *source, arp_range_t *dest) {
    if (!source || !dest)
        return 0;

    if (!arp_init_range_with_capacity(dest, source->capacity)) {
        return 0;
    }

    /* Copy all fields */
    memcpy(dest->hands, source->hands, source->count * sizeof(StdDeck_CardMask));
    memcpy(dest->weights, source->weights, source->count * sizeof(double));

    dest->count = source->count;
    dest->total_weight = source->total_weight;
    dest->has_weights = source->has_weights;
    dest->percentage_used = source->percentage_used;
    dest->is_percentage = source->is_percentage;
    dest->game_type = source->game_type;
    dest->is_omaha = source->is_omaha;

    /* Don't copy hash table - will be rebuilt if needed */
    dest->internal_data = NULL;

    return 1;
}

bool ARP_RangesEqual(const arp_range_t *range1, const arp_range_t *range2) {
    if (!range1 || !range2)
        return false;

    if (range1->count != range2->count)
        return false;

    /* Check each hand exists in both ranges */
    for (size_t i = 0; i < range1->count; i++) {
        bool found = false;
        for (size_t j = 0; j < range2->count; j++) {
            if (StdDeck_CardMask_EQUAL(range1->hands[i], range2->hands[j])) {
                /* Check weights match */
                if (fabs(range1->weights[i] - range2->weights[j]) < 0.0001) {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            return false;
    }

    return true;
}

int ARP_IntersectRanges(
    const arp_range_t *range1,
    const arp_range_t *range2,
    arp_range_t *result)
{
    if (!range1 || !range2 || !result)
        return 0;

    if (!arp_init_range(result))
        return 0;

    /* Find hands that exist in both ranges */
    for (size_t i = 0; i < range1->count; i++) {
        for (size_t j = 0; j < range2->count; j++) {
            if (StdDeck_CardMask_EQUAL(range1->hands[i], range2->hands[j])) {
                /* Hand exists in both - add to result */
                /* Use minimum weight */
                double weight = (range1->weights[i] < range2->weights[j]) ?
                               range1->weights[i] : range2->weights[j];

                if (!arp_add_hand_to_range(result, range1->hands[i], weight)) {
                    ARP_FreeRange(result);
                    return 0;
                }
                break;
            }
        }
    }

    return 1;
}

bool ARP_ContainsHand(const arp_range_t *range, StdDeck_CardMask hand) {
    if (!range)
        return false;

    /* Use hash table if available */
    if (range->internal_data) {
        arp_hash_table_t *ht = (arp_hash_table_t *)range->internal_data;
        return arp_hash_contains(ht, hand);
    }

    /* Linear search */
    for (size_t i = 0; i < range->count; i++) {
        if (StdDeck_CardMask_EQUAL(range->hands[i], hand)) {
            return true;
        }
    }

    return false;
}

int ARP_ExportRange(
    const arp_range_t *range,
    FILE *output,
    enum_game_t game_type)
{
    if (!range || !output)
        return 0;

    fprintf(output, "# Range Export\n");
    fprintf(output, "# Game: %d\n", game_type);
    fprintf(output, "# Hands: %zu\n", range->count);
    fprintf(output, "# Total Weight: %.6f\n", range->total_weight);
    fprintf(output, "# Has Weights: %s\n", range->has_weights ? "yes" : "no");
    fprintf(output, "\n");

    /* Export each hand */
    for (size_t i = 0; i < range->count; i++) {
        char hand_str[32];

        /* Convert hand to string */
        /* TODO: Handle Omaha hands if needed */
        if (range->is_omaha) {
             /* Basic support: stringify as list of cards */
             /* This is simplified */
             StdDeck_maskToString(range->hands[i], hand_str);
        } else {
             StdDeck_maskToString(range->hands[i], hand_str);
        }

        if (range->has_weights) {
            fprintf(output, "%s\t%.6f\n", hand_str, range->weights[i]);
        } else {
            fprintf(output, "%s\n", hand_str);
        }
    }

    return (int)range->count;
}

/**
 * Helper: Parse hand string to CardMask
 * Format: "AsAh", "KdQd", etc. (2-7 cards)
 *
 * @param hand_str Input string (e.g., "AsKh")
 * @param hand Output CardMask
 * @return 1 on success, 0 on failure
 */
static int parse_hand_string(const char *hand_str, StdDeck_CardMask *hand) {
    if (!hand_str || !hand)
        return 0;

    StdDeck_CardMask_RESET(*hand);

    size_t len = strnlen(hand_str, 32);
    if (len < 2 || len > 14 || len % 2 != 0)
        return 0;  /* Invalid length */

    /* Parse each card (2 chars: rank + suit) */
    for (size_t i = 0; i < len; i += 2) {
        char rank_char = hand_str[i];
        char suit_char = hand_str[i + 1];

        /* Parse rank */
        int rank = arp_char_to_rank(rank_char);
        if (rank < 0)
            return 0;

        /* Parse suit */
        int suit = arp_char_to_suit(suit_char);
        if (suit < 0)
            return 0;

        /* Create and add card */
        int card = StdDeck_MAKE_CARD(rank, suit);
        StdDeck_CardMask_SET(*hand, card);
    }

    return 1;
}

/**
 * Export a hold'em range as a 13x13 weight grid.
 *
 * Row/column labels run Ace-high to deuce-low. The diagonal holds pocket
 * pairs; cells above the diagonal are suited combinations; cells below are
 * offsuit. Each cell prints the summed weight of that hand class as a
 * percentage of the range's total weight, or "-" when absent.
 */
int ARP_ExportRangeMatrix(const arp_range_t *range, FILE *output)
{
    static const int matrix_ranks[13] = {
        StdDeck_Rank_ACE, StdDeck_Rank_KING, StdDeck_Rank_QUEEN,
        StdDeck_Rank_JACK, StdDeck_Rank_TEN, StdDeck_Rank_9,
        StdDeck_Rank_8, StdDeck_Rank_7, StdDeck_Rank_6,
        StdDeck_Rank_5, StdDeck_Rank_4, StdDeck_Rank_3, StdDeck_Rank_2
    };
    static const char *rank_labels[13] = {
        "A", "K", "Q", "J", "T", "9", "8", "7", "6", "5", "4", "3", "2"
    };
    double cell[13][13];
    int i, j;
    double total;

    if (!range || !output)
        return 0;

    for (i = 0; i < 13; i++)
        for (j = 0; j < 13; j++)
            cell[i][j] = 0.0;

    for (size_t h = 0; h < range->count; h++)
    {
        int ranks[2];
        int suits[2];
        int ncards = 0;

        for (int card = 0; card < StdDeck_N_CARDS && ncards < 2; card++)
        {
            if (StdDeck_CardMask_CARD_IS_SET(range->hands[h], card))
            {
                ranks[ncards] = StdDeck_RANK(card);
                suits[ncards] = StdDeck_SUIT(card);
                ncards++;
            }
        }

        if (ncards != 2)
            continue;

        int r1 = -1, r2 = -1;
        for (i = 0; i < 13; i++)
        {
            if (matrix_ranks[i] == ranks[0]) r1 = i;
            if (matrix_ranks[i] == ranks[1]) r2 = i;
        }
        if (r1 < 0 || r2 < 0)
            continue;

        double w = range->has_weights ? range->weights[h] : 1.0;

        if (r1 == r2)
        {
            cell[r1][r1] += w;
        }
        else if (suits[0] == suits[1])
        {
            int hi = (r1 < r2) ? r1 : r2;
            int lo = (r1 < r2) ? r2 : r1;
            cell[hi][lo] += w;
        }
        else
        {
            int hi = (r1 < r2) ? r1 : r2;
            int lo = (r1 < r2) ? r2 : r1;
            cell[lo][hi] += w;
        }
    }

    total = (range->has_weights && range->total_weight > 0.0) ? range->total_weight : (double)range->count;
    if (total <= 0.0)
        total = 1.0;

    fprintf(output, "      ");
    for (j = 0; j < 13; j++)
        fprintf(output, " %s", rank_labels[j]);
    fprintf(output, "\n");
    fprintf(output, "      ");
    for (j = 0; j < 13; j++)
        fprintf(output, " --");
    fprintf(output, "\n");

    for (i = 0; i < 13; i++)
    {
        fprintf(output, " %s |", rank_labels[i]);
        for (j = 0; j < 13; j++)
        {
            if (cell[i][j] > 0.0)
                fprintf(output, " %3.1f", (cell[i][j] / total) * 100.0);
            else
                fprintf(output, "  -");
        }
        fprintf(output, "\n");
    }

    return 1;
}

int ARP_ImportRange(FILE *input, arp_range_t *result) {
    if (!input || !result)
        return 0;

    if (!arp_init_range(result))
        return 0;

    char line[256];
    bool has_weights = false;

    while (fgets(line, sizeof(line), input)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1])) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        char* hand_part = line;
        double weight = 1.0;

        char* last_space = strrchr(line, ' ');
        if (last_space) {
            char* p_end;
            double w = strtod(last_space + 1, &p_end);
            if (p_end > last_space + 1 && (*p_end == '\0' || isspace((unsigned char)*p_end))) {
                weight = w;
                has_weights = true;
                *last_space = '\0';
            }
        }

        char hand_str_compact[32] = {0};
        int j = 0;
        for (int i = 0; hand_part[i] != '\0' && j < 31; i++) {
            if (!isspace((unsigned char)hand_part[i])) {
                hand_str_compact[j++] = hand_part[i];
            }
        }

        if (strlen(hand_str_compact) == 0) continue;

        StdDeck_CardMask hand;
        if (!parse_hand_string(hand_str_compact, &hand)) {
            continue;
        }

        if (!arp_add_hand_to_range(result, hand, weight)) {
            ARP_FreeRange(result);
            return 0;
        }
    }

    result->has_weights = has_weights;
    return (result->count > 0) ? 1 : 0;
}

/* Initialize range structure */
static int arp_init_range(arp_range_t *range)
{
    /* Use default capacity */
    return arp_init_range_with_capacity(range, 256);
}

/* Set error with detailed information */
static void arp_set_error(
    arp_context_t *ctx,
    arp_error_code_t code,
    const char *token,
    const char *message,
    const char *suggestion)
{
    /* Note: Error details support will be added to arp_context_t in Phase 4.2 fully.
       For now, we just use existing string buffer */
    if (!ctx)
        return;

    if (message) {
        snprintf(ctx->error_message, sizeof(ctx->error_message), "%s", message);
    }
    /* TODO: Store other fields when context struct is updated */
}

/* Format error for display */
int ARP_FormatError(
    const arp_error_details_t *details,
    char *buffer,
    size_t buffer_size)
{
    if (!details || !buffer || buffer_size == 0)
        return 0;

    int written = 0;

    written += snprintf(buffer + written, buffer_size - written,
                       "Error at position %d: %s\n",
                       details->error_position,
                       details->error_message);

    if (details->error_token[0] != '\0') {
        written += snprintf(buffer + written, buffer_size - written,
                           "Token: '%s'\n",
                           details->error_token);
    }

    if (details->suggestion[0] != '\0') {
        written += snprintf(buffer + written, buffer_size - written,
                           "Suggestion: %s\n",
                           details->suggestion);
    }

    return written;
}

/* Enhanced ParseRange with error details */
int ARP_ParseRangeWithError(
    const char *range_string,
    StdDeck_CardMask dead_cards,
    enum_game_t game_type,
    arp_range_t *result,
    arp_error_details_t *error_details)
{
    arp_context_t ctx;
    if (!ARP_InitContext(&ctx, range_string)) {
        if (error_details) {
            error_details->error_code = ARP_ERROR_MEMORY_ALLOCATION;
            snprintf(error_details->error_message,
                     sizeof(error_details->error_message),
                     "Failed to initialize parser");
        }
        return 0;
    }

    /* Try to parse */
    int success = arp_parse_with_context(&ctx, dead_cards, game_type, result);

    if (success) {
        arp_normalize_range_weights(result);
    } else {
        /* Copy error details if failed */
        if (error_details) {
            /* Map generic string error to details roughly */
            error_details->error_position = (int)ctx.position;

            size_t msg_len = strlen(ctx.error_message);
            if (msg_len > 255) msg_len = 255;
            memcpy(error_details->error_message, ctx.error_message, msg_len);
            error_details->error_message[msg_len] = '\0';

            error_details->error_code = ARP_ERROR_INVALID_SYNTAX; /* Generic */
            error_details->error_token[0] = '\0';
            error_details->suggestion[0] = '\0';
        }
    }

    ARP_FreeContext(&ctx);

    return success;
}


/* Initialize hash table when needed */
static void arp_range_ensure_hash_table(arp_range_t *range) {
    if (!range || range->internal_data)
        return;

    arp_hash_table_t *ht = malloc(sizeof(arp_hash_table_t));
    if (!ht)
        return;

    if (!arp_hash_init(ht, range->count)) {
        free(ht);
        return; /* caller falls back to the linear scan */
    }

    /* Populate with existing hands */
    for (size_t i = 0; i < range->count; i++) {
        arp_hash_insert(ht, range->hands[i], i);
    }
    range->internal_data = ht;
}

/* Drop the hash table; it is rebuilt lazily on the next insertion. Must be
 * called by anything that reorders or removes entries in range->hands, since
 * the table stores positions into that array. */
static void arp_range_invalidate_hash_table(arp_range_t *range) {
    if (!range || !range->internal_data)
        return;
    arp_hash_table_t *ht = (arp_hash_table_t *)range->internal_data;
    arp_hash_free(ht);
    free(ht);
    range->internal_data = NULL;
}

/* Add a hand to range */
static int arp_add_hand_to_range(arp_range_t *range, StdDeck_CardMask hand, double weight)
{
    if (!range || !range->hands || !range->weights)
        return 0;

    if (fabs(weight) < 1e-9)
        return 1;

    /* Use hash table for duplicate detection if range is large */
    if (range->count > 50) {
        /* Ensure hash table exists */
        arp_range_ensure_hash_table(range);

        if (range->internal_data) {
            arp_hash_table_t *ht = (arp_hash_table_t *)range->internal_data;
            size_t found;
            if (arp_hash_lookup(ht, hand, &found) && found < range->count) {
                range->weights[found] += weight;
                range->total_weight += weight;
                if (fabs(range->weights[found] - 1.0) > 1e-9)
                    range->has_weights = true;
                return 1;
            }
            goto add_hand;
        }
    }

    /* Fallback: Linear search for small ranges */
    for (size_t i = 0; i < range->count; i++)
    {
        if (StdDeck_CardMask_EQUAL(range->hands[i], hand))
        {
            range->weights[i] += weight;
            range->total_weight += weight;
            if (fabs(range->weights[i] - 1.0) > 1e-9)
                range->has_weights = true;
            return 1; /* Already exists, no need to add */
        }
    }

add_hand:
    /* Expand capacity if needed */
    if (range->count >= range->capacity)
    {
        size_t new_capacity = range->capacity * 2;
        StdDeck_CardMask *new_hands = realloc(range->hands, new_capacity * sizeof(StdDeck_CardMask));
        double *new_weights = realloc(range->weights, new_capacity * sizeof(double));
        if (!new_hands || !new_weights)
        {
            if (new_hands)
                range->hands = new_hands;
            if (new_weights)
                range->weights = new_weights;
            return 0;
        }
        range->hands = new_hands;
        range->weights = new_weights;
        range->capacity = new_capacity;
    }

    /* Add the hand */
    range->hands[range->count] = hand;
    range->weights[range->count] = weight;
    range->total_weight += weight;

    /* Update hash table */
    if (range->internal_data) {
        arp_hash_table_t *ht = (arp_hash_table_t *)range->internal_data;
        arp_hash_insert(ht, hand, range->count);
    }

    range->count++;

    if (fabs(weight - 1.0) > 1e-9)
        range->has_weights = true;

    return 1;
}

static int arp_remove_hand_from_range(arp_range_t *range, StdDeck_CardMask hand)
{
    if (!range || !range->hands || !range->weights)
        return 0;

    /* Entries below shift down, so every stored index would go stale. */
    arp_range_invalidate_hash_table(range);

    for (size_t i = 0; i < range->count; ++i)
    {
        if (StdDeck_CardMask_EQUAL(range->hands[i], hand))
        {
            if (range->total_weight > 0.0)
            {
                range->total_weight -= range->weights[i];
                if (range->total_weight < 0.0)
                    range->total_weight = 0.0;
            }

            if (i + 1 < range->count)
            {
                memmove(&range->hands[i], &range->hands[i + 1], (range->count - i - 1) * sizeof(StdDeck_CardMask));
                memmove(&range->weights[i], &range->weights[i + 1], (range->count - i - 1) * sizeof(double));
            }
            range->count--;
            --i;
        }
    }

    if (range->count == 0)
    {
        range->has_weights = false;
        range->total_weight = 0.0;
    }
    else if (range->has_weights)
    {
        bool all_uniform = true;
        for (size_t j = 0; j < range->count; ++j)
        {
            if (fabs(range->weights[j] - 1.0) > 1e-9)
            {
                all_uniform = false;
                break;
            }
        }
        if (all_uniform)
        {
            range->has_weights = false;
            range->total_weight = (double)range->count;
        }
    }

    return 1;
}

static int arp_remove_range(arp_range_t *target, const arp_range_t *to_remove)
{
    if (!target || !to_remove)
        return 0;

    for (size_t i = 0; i < to_remove->count; ++i)
    {
        if (!arp_remove_hand_from_range(target, to_remove->hands[i]))
            return 0;
    }
    return 1;
}

static void arp_normalize_range_weights(arp_range_t *range)
{
    if (!range || !range->has_weights || range->total_weight <= 0.0)
        return;

    double scale = 1.0 / range->total_weight;
    for (size_t i = 0; i < range->count; ++i)
    {
        range->weights[i] *= scale;
    }
    range->total_weight = 1.0;
}

static void arp_apply_weight_to_range(arp_range_t *range, double weight)
{
    if (!range || range->count == 0)
        return;

    if (weight <= 0.0)
    {
        for (size_t i = 0; i < range->count; ++i)
        {
            range->weights[i] = 0.0;
        }
        range->total_weight = 0.0;
        range->has_weights = true;
        return;
    }

    double current_total = range->total_weight;
    if (current_total <= 0.0)
    {
        double per = weight / (double)range->count;
        for (size_t i = 0; i < range->count; ++i)
        {
            range->weights[i] = per;
        }
    }
    else
    {
        double scale = weight / current_total;
        for (size_t i = 0; i < range->count; ++i)
        {
            range->weights[i] *= scale;
        }
    }
    range->total_weight = weight;
    range->has_weights = true;
}

/* Expand pair range (e.g., AA-TT) */
static int arp_expand_pair_range(int high_rank, int low_rank, StdDeck_CardMask dead_cards, arp_range_t *range)
{
    if (!range)
        return 0;

    /* Ensure high_rank >= low_rank */
    if (high_rank < low_rank)
    {
        int temp = high_rank;
        high_rank = low_rank;
        low_rank = temp;
    }

    for (int rank = high_rank; rank >= low_rank; rank--)
    {
        /* Generate all combinations of this pocket pair */
        for (int suit1 = 0; suit1 < 4; suit1++)
        {
            for (int suit2 = suit1 + 1; suit2 < 4; suit2++)
            {
                int card1 = StdDeck_MAKE_CARD(rank, suit1);
                int card2 = StdDeck_MAKE_CARD(rank, suit2);

                StdDeck_CardMask hand;
                StdDeck_CardMask_RESET(hand);
                StdDeck_CardMask_SET(hand, card1);
                StdDeck_CardMask_SET(hand, card2);

                /* Skip if any card is dead */
                if (StdDeck_CardMask_ANY_SET(dead_cards, hand))
                {
                    continue;
                }

                if (!arp_add_hand_to_range(range, hand, 1.0))
                {
                    return 0;
                }
            }
        }
    }

    return 1;
}

/* Expand non-pair hand range (e.g., AK-AJ, AKs-AJs, AKo-AJo) */
static int arp_expand_nonpropair_range(int rank1, int rank2_high, int rank2_low, bool suited, bool offsuit, StdDeck_CardMask dead_cards, arp_range_t *range)
{
    if (!range)
        return 0;

    /* Validate ranks */
    if (rank1 < StdDeck_Rank_2 || rank1 > StdDeck_Rank_ACE)
        return 0;
    if (rank2_high < StdDeck_Rank_2 || rank2_high > StdDeck_Rank_ACE)
        return 0;
    if (rank2_low < StdDeck_Rank_2 || rank2_low > StdDeck_Rank_ACE)
        return 0;

    /* rank1 must be different from rank2 range */
    if (rank1 == rank2_high || rank1 == rank2_low)
        return 0;

    /* rank2_high must be higher than rank2_low */
    if (rank2_high <= rank2_low)
        return 0;

    /* Iterate through each rank in the range */
    for (int rank2 = rank2_high; rank2 >= rank2_low; rank2--)
    {
        /* Skip if this creates a pair */
        if (rank1 == rank2)
            continue;

        /* Expand this specific hand using existing function */
        if (!arp_expand_hand_range(rank1, rank2, suited, offsuit,
                                   dead_cards, range))
        {
            return 0;
        }
    }

    return 1;
}

/* Expand hand range (e.g., AK, AKs, AKo) */
static int arp_expand_hand_range(int rank1, int rank2, bool suited, bool offsuit, StdDeck_CardMask dead_cards, arp_range_t *range)
{
    if (!range || rank1 == rank2)
        return 0; /* Can't have same rank for non-pair */

    for (int suit1 = 0; suit1 < 4; suit1++)
    {
        for (int suit2 = 0; suit2 < 4; suit2++)
        {
            if (suit1 == suit2 && !suited)
                continue; /* Skip suited if not wanted */
            if (suit1 != suit2 && !offsuit)
                continue; /* Skip offsuit if not wanted */

            int card1 = StdDeck_MAKE_CARD(rank1, suit1);
            int card2 = StdDeck_MAKE_CARD(rank2, suit2);

            StdDeck_CardMask hand;
            StdDeck_CardMask_RESET(hand);
            StdDeck_CardMask_SET(hand, card1);
            StdDeck_CardMask_SET(hand, card2);

            /* Skip if any card is dead */
            if (StdDeck_CardMask_ANY_SET(dead_cards, hand))
            {
                continue;
            }

            if (!arp_add_hand_to_range(range, hand, 1.0))
            {
                return 0;
            }
        }
    }

    return 1;
}

/* Hand ranking table for Hold'em starting hands (from strongest to weakest) */
static const struct {
    int rank1;
    int rank2;
    int type; /* 0=offsuit, 1=suited, 2=pair */
} holdem_hand_rankings[] = {
    /* Premium pairs */
    {StdDeck_Rank_ACE, StdDeck_Rank_ACE, 2},     /* AA */
    {StdDeck_Rank_KING, StdDeck_Rank_KING, 2},   /* KK */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_QUEEN, 2}, /* QQ */
    {StdDeck_Rank_JACK, StdDeck_Rank_JACK, 2},   /* JJ */
    {StdDeck_Rank_ACE, StdDeck_Rank_KING, 1},    /* AKs */
    {StdDeck_Rank_TEN, StdDeck_Rank_TEN, 2},     /* TT */
    {StdDeck_Rank_ACE, StdDeck_Rank_KING, 0},    /* AKo */
    {StdDeck_Rank_ACE, StdDeck_Rank_QUEEN, 1},   /* AQs */
    {StdDeck_Rank_KING, StdDeck_Rank_QUEEN, 1},  /* KQs */
    {StdDeck_Rank_ACE, StdDeck_Rank_JACK, 1},    /* AJs */
    {StdDeck_Rank_KING, StdDeck_Rank_JACK, 1},   /* KJs */
    {StdDeck_Rank_9, StdDeck_Rank_9, 2},         /* 99 */
    {StdDeck_Rank_ACE, StdDeck_Rank_TEN, 1},     /* ATs */
    {StdDeck_Rank_ACE, StdDeck_Rank_QUEEN, 0},   /* AQo */
    {StdDeck_Rank_KING, StdDeck_Rank_TEN, 1},    /* KTs */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_JACK, 1},  /* QJs */
    {StdDeck_Rank_KING, StdDeck_Rank_QUEEN, 0},  /* KQo */
    {StdDeck_Rank_8, StdDeck_Rank_8, 2},         /* 88 */
    {StdDeck_Rank_ACE, StdDeck_Rank_9, 1},       /* A9s */
    {StdDeck_Rank_ACE, StdDeck_Rank_JACK, 0},    /* AJo */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_TEN, 1},   /* QTs */
    {StdDeck_Rank_KING, StdDeck_Rank_JACK, 0},   /* KJo */
    {StdDeck_Rank_JACK, StdDeck_Rank_TEN, 1},    /* JTs */
    {StdDeck_Rank_ACE, StdDeck_Rank_8, 1},       /* A8s */
    {StdDeck_Rank_KING, StdDeck_Rank_9, 1},      /* K9s */
    {StdDeck_Rank_ACE, StdDeck_Rank_TEN, 0},     /* ATo */
    {StdDeck_Rank_ACE, StdDeck_Rank_7, 1},       /* A7s */
    {StdDeck_Rank_KING, StdDeck_Rank_TEN, 0},    /* KTo */
    {StdDeck_Rank_7, StdDeck_Rank_7, 2},         /* 77 */
    {StdDeck_Rank_ACE, StdDeck_Rank_6, 1},       /* A6s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_JACK, 0},  /* QJo */
    {StdDeck_Rank_ACE, StdDeck_Rank_5, 1},       /* A5s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_9, 1},     /* Q9s */
    {StdDeck_Rank_ACE, StdDeck_Rank_4, 1},       /* A4s */
    {StdDeck_Rank_JACK, StdDeck_Rank_9, 1},      /* J9s */
    {StdDeck_Rank_KING, StdDeck_Rank_8, 1},      /* K8s */
    {StdDeck_Rank_ACE, StdDeck_Rank_3, 1},       /* A3s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_TEN, 0},   /* QTo */
    {StdDeck_Rank_ACE, StdDeck_Rank_2, 1},       /* A2s */
    {StdDeck_Rank_JACK, StdDeck_Rank_8, 1},      /* J8s */
    {StdDeck_Rank_6, StdDeck_Rank_6, 2},         /* 66 */
    {StdDeck_Rank_KING, StdDeck_Rank_7, 1},      /* K7s */
    {StdDeck_Rank_TEN, StdDeck_Rank_9, 1},       /* T9s */
    {StdDeck_Rank_ACE, StdDeck_Rank_9, 0},       /* A9o */
    {StdDeck_Rank_KING, StdDeck_Rank_6, 1},      /* K6s */
    {StdDeck_Rank_JACK, StdDeck_Rank_TEN, 0},    /* JTo */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_8, 1},     /* Q8s */
    {StdDeck_Rank_KING, StdDeck_Rank_5, 1},      /* K5s */
    {StdDeck_Rank_JACK, StdDeck_Rank_7, 1},      /* J7s */
    {StdDeck_Rank_KING, StdDeck_Rank_4, 1},      /* K4s */
    {StdDeck_Rank_KING, StdDeck_Rank_9, 0},      /* K9o */
    {StdDeck_Rank_5, StdDeck_Rank_5, 2},         /* 55 */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_7, 1},     /* Q7s */
    {StdDeck_Rank_TEN, StdDeck_Rank_8, 1},       /* T8s */
    {StdDeck_Rank_KING, StdDeck_Rank_3, 1},      /* K3s */
    {StdDeck_Rank_JACK, StdDeck_Rank_6, 1},      /* J6s */
    {StdDeck_Rank_9, StdDeck_Rank_8, 1},         /* 98s */
    {StdDeck_Rank_KING, StdDeck_Rank_2, 1},      /* K2s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_6, 1},     /* Q6s */
    {StdDeck_Rank_ACE, StdDeck_Rank_8, 0},       /* A8o */
    {StdDeck_Rank_JACK, StdDeck_Rank_5, 1},      /* J5s */
    {StdDeck_Rank_TEN, StdDeck_Rank_7, 1},       /* T7s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_5, 1},     /* Q5s */
    {StdDeck_Rank_ACE, StdDeck_Rank_7, 0},       /* A7o */
    {StdDeck_Rank_JACK, StdDeck_Rank_4, 1},      /* J4s */
    {StdDeck_Rank_9, StdDeck_Rank_7, 1},         /* 97s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_4, 1},     /* Q4s */
    {StdDeck_Rank_TEN, StdDeck_Rank_6, 1},       /* T6s */
    {StdDeck_Rank_4, StdDeck_Rank_4, 2},         /* 44 */
    {StdDeck_Rank_JACK, StdDeck_Rank_3, 1},      /* J3s */
    {StdDeck_Rank_8, StdDeck_Rank_7, 1},         /* 87s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_3, 1},     /* Q3s */
    {StdDeck_Rank_9, StdDeck_Rank_6, 1},         /* 96s */
    {StdDeck_Rank_ACE, StdDeck_Rank_6, 0},       /* A6o */
    {StdDeck_Rank_JACK, StdDeck_Rank_2, 1},      /* J2s */
    {StdDeck_Rank_TEN, StdDeck_Rank_5, 1},       /* T5s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_2, 1},     /* Q2s */
    {StdDeck_Rank_8, StdDeck_Rank_6, 1},         /* 86s */
    {StdDeck_Rank_ACE, StdDeck_Rank_5, 0},       /* A5o */
    {StdDeck_Rank_9, StdDeck_Rank_5, 1},         /* 95s */
    {StdDeck_Rank_7, StdDeck_Rank_6, 1},         /* 76s */
    {StdDeck_Rank_TEN, StdDeck_Rank_4, 1},       /* T4s */
    {StdDeck_Rank_ACE, StdDeck_Rank_4, 0},       /* A4o */
    {StdDeck_Rank_8, StdDeck_Rank_5, 1},         /* 85s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_9, 0},     /* Q9o */
    {StdDeck_Rank_9, StdDeck_Rank_4, 1},         /* 94s */
    {StdDeck_Rank_7, StdDeck_Rank_5, 1},         /* 75s */
    {StdDeck_Rank_TEN, StdDeck_Rank_3, 1},       /* T3s */
    {StdDeck_Rank_3, StdDeck_Rank_3, 2},         /* 33 */
    {StdDeck_Rank_ACE, StdDeck_Rank_3, 0},       /* A3o */
    {StdDeck_Rank_8, StdDeck_Rank_4, 1},         /* 84s */
    {StdDeck_Rank_JACK, StdDeck_Rank_9, 0},      /* J9o */
    {StdDeck_Rank_6, StdDeck_Rank_5, 1},         /* 65s */
    {StdDeck_Rank_9, StdDeck_Rank_3, 1},         /* 93s */
    {StdDeck_Rank_TEN, StdDeck_Rank_2, 1},       /* T2s */
    {StdDeck_Rank_7, StdDeck_Rank_4, 1},         /* 74s */
    {StdDeck_Rank_ACE, StdDeck_Rank_2, 0},       /* A2o */
    {StdDeck_Rank_8, StdDeck_Rank_3, 1},         /* 83s */
    {StdDeck_Rank_6, StdDeck_Rank_4, 1},         /* 64s */
    {StdDeck_Rank_JACK, StdDeck_Rank_8, 0},      /* J8o */
    {StdDeck_Rank_9, StdDeck_Rank_2, 1},         /* 92s */
    {StdDeck_Rank_5, StdDeck_Rank_4, 1},         /* 54s */
    {StdDeck_Rank_7, StdDeck_Rank_3, 1},         /* 73s */
    {StdDeck_Rank_KING, StdDeck_Rank_8, 0},      /* K8o */
    {StdDeck_Rank_8, StdDeck_Rank_2, 1},         /* 82s */
    {StdDeck_Rank_6, StdDeck_Rank_3, 1},         /* 63s */
    {StdDeck_Rank_2, StdDeck_Rank_2, 2},         /* 22 */
    {StdDeck_Rank_5, StdDeck_Rank_3, 1},         /* 53s */
    {StdDeck_Rank_7, StdDeck_Rank_2, 1},         /* 72s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_8, 0},     /* Q8o */
    {StdDeck_Rank_4, StdDeck_Rank_3, 1},         /* 43s */
    {StdDeck_Rank_6, StdDeck_Rank_2, 1},         /* 62s */
    {StdDeck_Rank_JACK, StdDeck_Rank_7, 0},      /* J7o */
    {StdDeck_Rank_5, StdDeck_Rank_2, 1},         /* 52s */
    {StdDeck_Rank_KING, StdDeck_Rank_7, 0},      /* K7o */
    {StdDeck_Rank_4, StdDeck_Rank_2, 1},         /* 42s */
    {StdDeck_Rank_TEN, StdDeck_Rank_9, 0},       /* T9o */
    {StdDeck_Rank_KING, StdDeck_Rank_6, 0},      /* K6o */
    {StdDeck_Rank_3, StdDeck_Rank_2, 1},         /* 32s */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_7, 0},     /* Q7o */
    {StdDeck_Rank_JACK, StdDeck_Rank_6, 0},      /* J6o */
    {StdDeck_Rank_KING, StdDeck_Rank_5, 0},      /* K5o */
    {StdDeck_Rank_TEN, StdDeck_Rank_8, 0},       /* T8o */
    {StdDeck_Rank_KING, StdDeck_Rank_4, 0},      /* K4o */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_6, 0},     /* Q6o */
    {StdDeck_Rank_JACK, StdDeck_Rank_5, 0},      /* J5o */
    {StdDeck_Rank_KING, StdDeck_Rank_3, 0},      /* K3o */
    {StdDeck_Rank_9, StdDeck_Rank_8, 0},         /* 98o */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_5, 0},     /* Q5o */
    {StdDeck_Rank_JACK, StdDeck_Rank_4, 0},      /* J4o */
    {StdDeck_Rank_KING, StdDeck_Rank_2, 0},      /* K2o */
    {StdDeck_Rank_TEN, StdDeck_Rank_7, 0},       /* T7o */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_4, 0},     /* Q4o */
    {StdDeck_Rank_JACK, StdDeck_Rank_3, 0},      /* J3o */
    {StdDeck_Rank_9, StdDeck_Rank_7, 0},         /* 97o */
    {StdDeck_Rank_TEN, StdDeck_Rank_6, 0},       /* T6o */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_3, 0},     /* Q3o */
    {StdDeck_Rank_JACK, StdDeck_Rank_2, 0},      /* J2o */
    {StdDeck_Rank_8, StdDeck_Rank_7, 0},         /* 87o */
    {StdDeck_Rank_TEN, StdDeck_Rank_5, 0},       /* T5o */
    {StdDeck_Rank_QUEEN, StdDeck_Rank_2, 0},     /* Q2o */
    {StdDeck_Rank_9, StdDeck_Rank_6, 0},         /* 96o */
    {StdDeck_Rank_TEN, StdDeck_Rank_4, 0},       /* T4o */
    {StdDeck_Rank_8, StdDeck_Rank_6, 0},         /* 86o */
    {StdDeck_Rank_TEN, StdDeck_Rank_3, 0},       /* T3o */
    {StdDeck_Rank_9, StdDeck_Rank_5, 0},         /* 95o */
    {StdDeck_Rank_TEN, StdDeck_Rank_2, 0},       /* T2o */
    {StdDeck_Rank_7, StdDeck_Rank_6, 0},         /* 76o */
    {StdDeck_Rank_8, StdDeck_Rank_5, 0},         /* 85o */
    {StdDeck_Rank_9, StdDeck_Rank_4, 0},         /* 94o */
    {StdDeck_Rank_7, StdDeck_Rank_5, 0},         /* 75o */
    {StdDeck_Rank_8, StdDeck_Rank_4, 0},         /* 84o */
    {StdDeck_Rank_9, StdDeck_Rank_3, 0},         /* 93o */
    {StdDeck_Rank_6, StdDeck_Rank_5, 0},         /* 65o */
    {StdDeck_Rank_7, StdDeck_Rank_4, 0},         /* 74o */
    {StdDeck_Rank_8, StdDeck_Rank_3, 0},         /* 83o */
    {StdDeck_Rank_9, StdDeck_Rank_2, 0},         /* 92o */
    {StdDeck_Rank_6, StdDeck_Rank_4, 0},         /* 64o */
    {StdDeck_Rank_7, StdDeck_Rank_3, 0},         /* 73o */
    {StdDeck_Rank_5, StdDeck_Rank_4, 0},         /* 54o */
    {StdDeck_Rank_8, StdDeck_Rank_2, 0},         /* 82o */
    {StdDeck_Rank_6, StdDeck_Rank_3, 0},         /* 63o */
    {StdDeck_Rank_7, StdDeck_Rank_2, 0},         /* 72o */
    {StdDeck_Rank_5, StdDeck_Rank_3, 0},         /* 53o */
    {StdDeck_Rank_4, StdDeck_Rank_3, 0},         /* 43o */
    {StdDeck_Rank_6, StdDeck_Rank_2, 0},         /* 62o */
    {StdDeck_Rank_5, StdDeck_Rank_2, 0},         /* 52o */
    {StdDeck_Rank_4, StdDeck_Rank_2, 0},         /* 42o */
    {StdDeck_Rank_3, StdDeck_Rank_2, 0}          /* 32o */
};

#define HOLDEM_HAND_RANKINGS_COUNT (sizeof(holdem_hand_rankings) / sizeof(holdem_hand_rankings[0]))

/* Expand percentage range (e.g., 20%) */
static int arp_expand_percentage(float percentage, enum_game_t game_type, StdDeck_CardMask dead_cards, arp_range_t *range)
{
    if (!range || percentage <= 0.0f || percentage > 1.0f)
        return 0;

    /* For now, only support Hold'em */
    if (game_type != game_holdem)
    {
        return 0; /* Unsupported game type */
    }

    /* Calculate how many hands to include */
    int total_hands = HOLDEM_HAND_RANKINGS_COUNT;
    int hands_to_include = (int)(percentage * (float)total_hands + 0.5f); /* Round to nearest */

    if (hands_to_include > total_hands)
    {
        hands_to_include = total_hands;
    }

    /* Add hands from the ranking table */
    for (int i = 0; i < hands_to_include; i++)
    {
        int rank1 = holdem_hand_rankings[i].rank1;
        int rank2 = holdem_hand_rankings[i].rank2;
        int type = holdem_hand_rankings[i].type;

        if (type == 2)
        {
            /* Pocket pair */
            if (!arp_expand_pair_range(rank1, rank1, dead_cards, range))
            {
                return 0;
            }
        }
        else
        {
            /* Non-pair hand */
            bool suited = (type == 1);
            bool offsuit = (type == 0);
            if (!arp_expand_hand_range(rank1, rank2, suited, offsuit, dead_cards, range))
            {
                return 0;
            }
        }
    }

    return 1;
}

/* Basic tokenizer - this is a simplified version for now */
static int arp_tokenize(arp_context_t *ctx)
{
    if (!ctx)
        return 0;

    ctx->token_count = 0;
    /* Check for empty string */
    if (ctx->length == 0) {
        snprintf(ctx->error_message, sizeof(ctx->error_message), "Empty range string");
        return 0;
    }


    while (ctx->position < ctx->length)
    {
        char c = ctx->input[ctx->position];

        /* Skip whitespace */
        if (isspace(c))
        {
            ctx->position++;
            continue;
        }

        /* Ensure we have space for another token */
        if (ctx->token_count >= ctx->token_capacity)
        {
            size_t new_capacity = ctx->token_capacity * 2;
            arp_token_t *new_tokens = realloc(ctx->tokens, new_capacity * sizeof(arp_token_t));
            if (!new_tokens)
            {
                snprintf(ctx->error_message, sizeof(ctx->error_message), "Memory allocation failed");
                return 0;
            }
            ctx->tokens = new_tokens;
            ctx->token_capacity = new_capacity;
        }

        arp_token_t *token = &ctx->tokens[ctx->token_count];
        memset(token, 0, sizeof(arp_token_t));
        token->weight = 1.0;
        token->has_weight = false;
        bool allow_weight = false;

        /* Parse different token types */
        if (c == ',')
        {
            token->type = ARP_TOKEN_COMMA;
            ctx->position++;
        }
        else if (c == '(' || ARP_IsStudPattern(&ctx->input[ctx->position], ctx->length - ctx->position))
        {
            /* Check for Stud pattern (AA)K or A23 vs Expression (AA, KK) */
            /* If it starts with '(', it could be expression or Stud. ARP_IsStudPattern distinguishes.
               If it doesn't start with '(', but ARP_IsStudPattern says yes (A23 case), we take it. */

            size_t remaining = ctx->length - ctx->position;
            int is_stud = ARP_IsStudPattern(&ctx->input[ctx->position], remaining);

            /* If it's just '(', but NOT stud, it's LPAREN */
            if (c == '(' && !is_stud) {
                token->type = ARP_TOKEN_LPAREN;
                ctx->position++;
            }
            else if (is_stud)
            {
                token->type = ARP_TOKEN_STUD_PATTERN;
                size_t len = 0;
                /* Find end of stud pattern */
                const char *p = &ctx->input[ctx->position];
                /* A23 has length 3. (AA)K has length 5. */
                if (p[0] != '(') {
                    /* Triplet case A23 */
                    len = 3;
                } else {
                    /* Standard case */
                    while (p[len] && !isspace(p[len]) && p[len] != ',' && p[len] != '+' && p[len] != '-')
                        len++;
                }

                if (len >= sizeof(token->data.stud.pattern))
                    len = sizeof(token->data.stud.pattern) - 1;

                strncpy(token->data.stud.pattern, p, len);
                token->data.stud.pattern[len] = '\0';
                ctx->position += len;
                allow_weight = true;
            }
        }
        else if (c == ')')
        {
            token->type = ARP_TOKEN_RPAREN;
            ctx->position++;
        }
        else if (c == '+' || c == '-' || c == '!')
        {
            token->type = ARP_TOKEN_OPERATION;
            if (c == '+') token->operation = ARP_OP_ADD;
            else if (c == '-') token->operation = ARP_OP_SUBTRACT;
            else if (c == '!') token->operation = ARP_OP_EXCLUDE;
            ctx->position++;
        }
        else if (isalpha(c) || isdigit(c))
        {
            /* A token starting with a digit is a percentage only when immediately
               followed by '%'. Otherwise it is a hand token whose rank begins with a
               digit (e.g. "22", "9", "9Ts"). */
            if (isdigit(c))
            {
                char *endptr;
                float percentage = strtof(&ctx->input[ctx->position], &endptr);
                if (endptr > &ctx->input[ctx->position] && *endptr == '%')
                {
                    token->type = ARP_TOKEN_PERCENTAGE;
                    token->data.percentage = percentage / 100.0f;
                    ctx->position = endptr - ctx->input + 1;
                    goto token_complete;
                }
                /* Not a percentage: fall through and parse as a hand token. */
            }

            size_t start_pos = ctx->position;
            size_t remaining = ctx->length - ctx->position;
            bool parsed_category = false;
            size_t prefix_len = 0;

            if (remaining >= 4 && arp_strncaseeq(&ctx->input[ctx->position], "cat:", 4))
            {
                prefix_len = 4;
            }
            else if (remaining >= 4 && arp_strncaseeq(&ctx->input[ctx->position], "cat=", 4))
            {
                prefix_len = 4;
            }
            else if (remaining >= 9 && arp_strncaseeq(&ctx->input[ctx->position], "category:", 9))
            {
                prefix_len = 9;
            }
            else if (remaining >= 9 && arp_strncaseeq(&ctx->input[ctx->position], "category=", 9))
            {
                prefix_len = 9;
            }

            if (prefix_len > 0)
            {
                ctx->position += prefix_len;
                while (ctx->position < ctx->length &&
                       isspace((unsigned char)ctx->input[ctx->position]))
                {
                    ctx->position++;
                }

                size_t category_start = ctx->position;
                while (ctx->position < ctx->length)
                {
                    char ch = ctx->input[ctx->position];
                    if (isalnum((unsigned char)ch) || ch == '_' || ch == '-')
                    {
                        ctx->position++;
                    }
                    else
                    {
                        break;
                    }
                }

                size_t category_len = ctx->position - category_start;
                if (category_len == 0)
                {
                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                             "Missing category name at position %zu", start_pos);
                    return 0;
                }

                PLOHandCategory category_value;
                if (!arp_lookup_plo_category(&ctx->input[category_start], category_len, &category_value))
                {
                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                             "Unknown PLO category '%.*s'",
                             (int)category_len, &ctx->input[category_start]);
                    return 0;
                }

                token->type = ARP_TOKEN_PLO_CATEGORY;
                token->data.plo.category = category_value;
                size_t copy_len = category_len;
                if (copy_len >= sizeof(token->data.plo.pattern))
                    copy_len = sizeof(token->data.plo.pattern) - 1;
                memcpy(token->data.plo.pattern, &ctx->input[category_start], copy_len);
                token->data.plo.pattern[copy_len] = '\0';
                allow_weight = true;
                parsed_category = true;
            }

            if (!parsed_category)
            {
                /* Parse hand or pattern notation */
                size_t token_end = ctx->position;
                while (token_end < ctx->length)
                {
                    char ch = ctx->input[token_end];
                    if (ch == ',' || ch == '+' || ch == '-' || ch == '!' ||
                        ch == '(' || ch == ')' || ch == '{' || ch == '}' ||
                        ch == '@' || isspace((unsigned char)ch))
                        break;
                    token_end++;
                }
                size_t token_len = token_end - ctx->position;

                int is_plo = arp_is_plo_pattern(&ctx->input[ctx->position], token_len);
                if (is_plo)
                {
                    size_t pattern_end = ctx->position;
                    while (pattern_end < ctx->length &&
                           (isalnum((unsigned char)ctx->input[pattern_end]) || ctx->input[pattern_end] == 'x'))
                    {
                        pattern_end++;
                    }

                    if (pattern_end + 1 < ctx->length)
                    {
                        if ((ctx->input[pattern_end] == 'd' && ctx->input[pattern_end + 1] == 's') ||
                            (ctx->input[pattern_end] == 's' && ctx->input[pattern_end + 1] == 's'))
                        {
                            pattern_end += 2;
                        }
                        else if (ctx->input[pattern_end] == 'r')
                        {
                            pattern_end += 1;
                        }
                    }

                    size_t pattern_len = pattern_end - ctx->position;
                    if (pattern_len < sizeof(token->data.plo.pattern))
                    {
                        token->type = ARP_TOKEN_PLO_PATTERN;
                        strncpy(token->data.plo.pattern, &ctx->input[ctx->position], pattern_len);
                        token->data.plo.pattern[pattern_len] = '\0';
                        allow_weight = true;
                        ctx->position = pattern_end;
                    }
                }
                else
                {
                    int rank1 = arp_char_to_rank(ctx->input[ctx->position]);
                    if (rank1 < 0)
                    {
                        snprintf(ctx->error_message, sizeof(ctx->error_message),
                                 "Invalid rank '%c' at position %zu", ctx->input[ctx->position], ctx->position);
                        return 0;
                    }
                    ctx->position++;

                    if (ctx->position >= ctx->length)
                    {
                        snprintf(ctx->error_message, sizeof(ctx->error_message),
                                 "Incomplete hand at position %zu", start_pos);
                        return 0;
                    }

                    if (arp_char_to_suit(ctx->input[ctx->position]) >= 0)
                    {
                        int suit1 = arp_char_to_suit(ctx->input[ctx->position]);
                        ctx->position++;

                        if (ctx->position >= ctx->length)
                        {
                            snprintf(ctx->error_message, sizeof(ctx->error_message),
                                     "Incomplete specific hand at position %zu", start_pos);
                            return 0;
                        }

                        int rank2 = arp_char_to_rank(ctx->input[ctx->position]);
                        if (rank2 < 0)
                        {
                            snprintf(ctx->error_message, sizeof(ctx->error_message),
                                     "Invalid second rank '%c' at position %zu", ctx->input[ctx->position], ctx->position);
                            return 0;
                        }
                        ctx->position++;

                        if (ctx->position >= ctx->length)
                        {
                            snprintf(ctx->error_message, sizeof(ctx->error_message),
                                     "Incomplete specific hand at position %zu", start_pos);
                            return 0;
                        }

                        int suit2 = arp_char_to_suit(ctx->input[ctx->position]);
                        if (suit2 < 0)
                        {
                            snprintf(ctx->error_message, sizeof(ctx->error_message),
                                     "Invalid second suit '%c' at position %zu", ctx->input[ctx->position], ctx->position);
                            return 0;
                        }
                        ctx->position++;

                        token->type = ARP_TOKEN_SPECIFIC_HAND;
                        token->data.specific.rank1 = rank1;
                        token->data.specific.rank2 = rank2;
                        token->data.specific.suit1 = suit1;
                        token->data.specific.suit2 = suit2;
                        allow_weight = true;
                    }
                    else
                    {
                        int rank2 = arp_char_to_rank(ctx->input[ctx->position]);
                        if (rank2 < 0)
                        {
                            snprintf(ctx->error_message, sizeof(ctx->error_message),
                                     "Invalid rank '%c' at position %zu", ctx->input[ctx->position], ctx->position);
                            return 0;
                        }
                        ctx->position++;

                        /* Determine suit type from start suffix */
                        char suffix_start = '\0';
                        if (ctx->position < ctx->length)
                        {
                            char c = ctx->input[ctx->position];
                            if (c == 's' || c == 'o')
                            {
                                suffix_start = c;
                                ctx->position++;
                            }
                        }

                        /* Handle ranges like AA-TT or AK-AJ */
                        if (ctx->position < ctx->length && ctx->input[ctx->position] == '-')
                        {
                            ctx->position++; /* Skip '-' */

                            if (ctx->position + 1 >= ctx->length)
                            {
                                snprintf(ctx->error_message, sizeof(ctx->error_message),
                                         "Incomplete range at position %zu", start_pos);
                                return 0;
                            }

                            int end_rank1 = arp_char_to_rank(ctx->input[ctx->position]);
                            int end_rank2 = arp_char_to_rank(ctx->input[ctx->position + 1]);

                            if (end_rank1 < 0 || end_rank2 < 0)
                            {
                                snprintf(ctx->error_message, sizeof(ctx->error_message),
                                         "Invalid range end at position %zu", ctx->position);
                                return 0;
                            }
                            ctx->position += 2; /* Skip ranks */

                            char suffix_end = '\0';
                            if (ctx->position < ctx->length)
                            {
                                char c = ctx->input[ctx->position];
                                if (c == 's' || c == 'o')
                                {
                                    suffix_end = c;
                                    ctx->position++;
                                }
                            }

                            if (rank1 == rank2)
                            {
                                /* Pair Range AA-KK */
                                if (end_rank1 != end_rank2)
                                {
                                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                                             "Range end must be a pocket pair at position %zu", ctx->position);
                                    return 0;
                                }

                                /* Pairs shouldn't have suffixes usually, but if they do (As-Ks?? no) */
                                /* Usually pairs are just AA-KK. */

                                token->type = ARP_TOKEN_PAIR_RANGE;
                                token->data.pair_range.high_rank = rank1;
                                token->data.pair_range.low_rank = end_rank1;
                                allow_weight = true;
                            }
                            else
                            {
                                /* Hand Range AK-AJ */
                                if (end_rank1 != rank1)
                                {
                                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                                             "Invalid hand range: first ranks don't match at position %zu", ctx->position);
                                    return 0;
                                }
                                if (end_rank2 >= rank2)
                                {
                                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                                             "Invalid hand range: end rank must be lower at position %zu (start_rank2=%d, end_rank2=%d)", ctx->position, rank2, end_rank2);
                                    return 0;
                                }
                                if (suffix_start != suffix_end)
                                {
                                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                                             "Invalid hand range: suit designators don't match at position %zu", ctx->position);
                                    return 0;
                                }

                                token->type = ARP_TOKEN_HAND_RANGE;
                                token->data.hand_range.rank1_high = rank1;
                                token->data.hand_range.rank2_high = rank2;
                                token->data.hand_range.rank2_low = end_rank2;
                                token->data.hand_range.suited = (suffix_start == 's');
                                token->data.hand_range.offsuit = (suffix_start == 'o');
                                token->data.hand_range.both = (suffix_start == '\0');
                                allow_weight = true;
                            }
                        }
                        else
                        {
                            /* Single Hand (AK, AKs, AKo) or Single Pair (AA) */
                            bool suited = (suffix_start == 's');
                            /* offsuit logic handled by token type setting below */
                            bool both = (suffix_start == '\0');

                            if (rank1 == rank2)
                            {
                                token->type = ARP_TOKEN_PAIR_RANGE;
                                token->data.pair_range.high_rank = rank1;
                                token->data.pair_range.low_rank = rank1;
                                allow_weight = true;
                            }
                            else
                            {
                                token->type = both ? ARP_TOKEN_BOTH : (suited ? ARP_TOKEN_SUITED : ARP_TOKEN_OFFSUIT);
                                token->data.hand.rank1 = rank1;
                                token->data.hand.rank2 = rank2;
                                token->data.hand.suited = suited || both;
                                token->data.hand.both = both;
                                allow_weight = true;
                            }
                        }
                    }
                }
            }
        }
        else
        {
            snprintf(ctx->error_message, sizeof(ctx->error_message),
                     "Unexpected character '%c' at position %zu", c, ctx->position);
            return 0;
        }

token_complete:
        if (allow_weight && ctx->position < ctx->length &&
            (ctx->input[ctx->position] == '{' || ctx->input[ctx->position] == '@'))
        {
            bool is_brace = (ctx->input[ctx->position] == '{');
            size_t weight_start = ctx->position + 1;
            char *endptr = NULL;
            double value = strtod(&ctx->input[weight_start], &endptr);
            if (endptr == &ctx->input[weight_start])
            {
                snprintf(ctx->error_message, sizeof(ctx->error_message),
                         "Invalid weight at position %zu", ctx->position);
                return 0;
            }
            while (isspace((unsigned char)*endptr))
            {
                endptr++;
            }
            if (*endptr == '%')
            {
                value /= 100.0;
                endptr++;
                while (isspace((unsigned char)*endptr))
                {
                    endptr++;
                }
            }
            if (is_brace)
            {
                if (*endptr != '}')
                {
                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                             "Weight must end with '}' at position %zu", ctx->position);
                    return 0;
                }
            }
            else
            {
                /* @ form: value is terminated by a delimiter; do not consume it. */
                char next = *endptr;
                if (next != '\0' && next != ',' && next != ' ' && next != '\t' &&
                    next != '+' && next != '-' && next != '!' &&
                    next != '(' && next != ')' && next != '{' && next != '}' &&
                    next != '@')
                {
                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                             "Weight must be followed by a delimiter at position %zu",
                             ctx->position);
                    return 0;
                }
            }
            if (value < 0.0)
            {
                snprintf(ctx->error_message, sizeof(ctx->error_message),
                         "Weight cannot be negative at position %zu", ctx->position);
                return 0;
            }
            token->weight = value;
            token->has_weight = true;
            if (is_brace)
                ctx->position = (size_t)(endptr - ctx->input) + 1;
            else
                ctx->position = (size_t)(endptr - ctx->input);
        }

        ctx->token_count++;
    }


    /* Validate that we do not end with an operation */
    if (ctx->token_count > 0) {
        arp_token_t* last_token = &ctx->tokens[ctx->token_count - 1];
        if (last_token->type == ARP_TOKEN_OPERATION) {
            snprintf(ctx->error_message, sizeof(ctx->error_message),
                    "Range string cannot end with an operator");
            return 0;
        }
    }
    return 1;
}

/* Parse tokens into range */
static int arp_parse_tokens(arp_context_t *ctx, StdDeck_CardMask dead_cards, enum_game_t game_type, arp_range_t *result)
{
    if (!ctx || !result)
        return 0;

    arp_operation_t current_op = ARP_OP_ADD; /* Default operation is add */
    arp_range_t accumulator;
    memset(&accumulator, 0, sizeof(accumulator));
    bool has_accumulator = false;

    for (size_t i = 0; i < ctx->token_count; i++)
    {
        arp_token_t *token = &ctx->tokens[i];

        switch (token->type)
        {
        case ARP_TOKEN_OPERATION:
            current_op = token->operation;
            break;

        case ARP_TOKEN_COMMA:
            current_op = ARP_OP_ADD; /* Reset to add after comma */
            break;

        case ARP_TOKEN_PAIR_RANGE:
        case ARP_TOKEN_HAND_RANGE:
        case ARP_TOKEN_STUD_PATTERN:
        case ARP_TOKEN_SUITED:
        case ARP_TOKEN_OFFSUIT:
        case ARP_TOKEN_BOTH:
        case ARP_TOKEN_SPECIFIC_HAND:
        case ARP_TOKEN_PERCENTAGE:
        case ARP_TOKEN_PLO_PATTERN:
        case ARP_TOKEN_PLO_CATEGORY:
        {
            arp_range_t token_range;
            memset(&token_range, 0, sizeof(token_range));

            size_t estimated_size = arp_estimate_range_size(token);
            if (!arp_init_range_with_game_and_capacity(&token_range, game_type, estimated_size))
            {
                return 0;
            }

            int expand_success = 1;

            if (token->type == ARP_TOKEN_PAIR_RANGE)
            {
                expand_success = arp_expand_pair_range(token->data.pair_range.high_rank,
                                                       token->data.pair_range.low_rank,
                                                       dead_cards, &token_range);
            }
            else if (token->type == ARP_TOKEN_HAND_RANGE)
            {
                expand_success = arp_expand_nonpropair_range(token->data.hand_range.rank1_high,
                                                             token->data.hand_range.rank2_high,
                                                             token->data.hand_range.rank2_low,
                                                             token->data.hand_range.suited,
                                                             token->data.hand_range.offsuit,
                                                             dead_cards, &token_range);
            }
            else if (token->type == ARP_TOKEN_STUD_PATTERN)
            {
                expand_success = ARP_ParseStudPattern(token->data.stud.pattern, dead_cards, &token_range);
                if (!expand_success)
                {
                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                             "Failed to parse Stud pattern: %s", token->data.stud.pattern);
                }
            }
            else if (token->type == ARP_TOKEN_SUITED)
            {
                expand_success = arp_expand_hand_range(token->data.hand.rank1, token->data.hand.rank2,
                                                       true, false, dead_cards, &token_range);
            }
            else if (token->type == ARP_TOKEN_OFFSUIT)
            {
                expand_success = arp_expand_hand_range(token->data.hand.rank1, token->data.hand.rank2,
                                                       false, true, dead_cards, &token_range);
            }
            else if (token->type == ARP_TOKEN_BOTH)
            {
                expand_success = arp_expand_hand_range(token->data.hand.rank1, token->data.hand.rank2,
                                                       true, true, dead_cards, &token_range);
            }
            else if (token->type == ARP_TOKEN_SPECIFIC_HAND)
            {
                int card1 = StdDeck_MAKE_CARD(token->data.specific.rank1, token->data.specific.suit1);
                int card2 = StdDeck_MAKE_CARD(token->data.specific.rank2, token->data.specific.suit2);

                StdDeck_CardMask hand;
                StdDeck_CardMask_RESET(hand);
                StdDeck_CardMask_SET(hand, card1);
                StdDeck_CardMask_SET(hand, card2);

                if (!StdDeck_CardMask_ANY_SET(dead_cards, hand))
                {
                    expand_success = arp_add_hand_to_range(&token_range, hand, 1.0);
                }
            }
            else if (token->type == ARP_TOKEN_PERCENTAGE)
            {
                token_range.is_percentage = true;
                token_range.percentage_used = token->data.percentage;
                if (arp_is_omaha_game(game_type))
                {
                    expand_success = arp_expand_omaha_percentage(token->data.percentage, game_type, dead_cards, &token_range);
                }
                else
                {
                    expand_success = arp_expand_percentage(token->data.percentage, game_type, dead_cards, &token_range);
                }
                if (!expand_success)
                {
                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                             "Failed to expand percentage range");
                }
            }
            else if (token->type == ARP_TOKEN_PLO_PATTERN)
            {
                expand_success = arp_parse_plo_pattern(token->data.plo.pattern, dead_cards, &token_range);
                if (!expand_success)
                {
                    snprintf(ctx->error_message, sizeof(ctx->error_message),
                             "Failed to parse PLO pattern: %s", token->data.plo.pattern);
                }
            }
            else if (token->type == ARP_TOKEN_PLO_CATEGORY)
            {
                if (!arp_is_omaha_game(game_type) || token->data.plo.category == PLO_CAT_INVALID)
                {
                    expand_success = 0;
                }
                else
                {
                    OmahaHandList list;
                    if (!OmahaHandList_Init(&list, OMAHA_HANDLIST_INITIAL_CAPACITY))
                    {
                        expand_success = 0;
                    }
                    else
                    {
                        int generated = PLO_GetCategoryHands(token->data.plo.category, dead_cards, &list);
                        if (generated < 0)
                        {
                            OmahaHandList_Free(&list);
                            expand_success = 0;
                        }
                        else
                        {
                            for (int idx = 0; idx < list.count && expand_success; ++idx)
                            {
                                if (!arp_add_hand_to_range(&token_range, list.hands[idx], 1.0))
                                {
                                    expand_success = 0;
                                }
                            }

                            OmahaHandList_Free(&list);
                        }
                    }
                }
            }

            if (!expand_success)
            {
                ARP_FreeRange(&token_range);
                ARP_FreeRange(&accumulator);
                return 0;
            }

            if (token->has_weight)
            {
                arp_apply_weight_to_range(&token_range, token->weight);
            }

            if (!arp_apply_token_operation(&accumulator, &has_accumulator, &token_range, current_op, game_type))
            {
                ARP_FreeRange(&token_range);
                ARP_FreeRange(&accumulator);
                return 0;
            }

            ARP_FreeRange(&token_range);
            break;
        }

        case ARP_TOKEN_LPAREN:
        case ARP_TOKEN_RPAREN:
            /* Parentheses are handled in expression parsing */
            break;

        case ARP_TOKEN_UNKNOWN:
        case ARP_TOKEN_END:
        default:
            break;
        }
    }

    if (has_accumulator)
    {
        *result = accumulator;
    }
    else
    {
        memset(result, 0, sizeof(*result));
        result->game_type = game_type;
        result->is_omaha = arp_is_omaha_game(game_type);
    }

    return 1;
}

/* Main parsing function */
int ARP_ParseRange(const char *range_string, StdDeck_CardMask dead_cards, enum_game_t game_type, arp_range_t *result)
{

    if (!range_string || !result)
    {
        return 0;
    }

    /* Use the complex expression parser for all range strings */
    int ret = ARP_ParseComplexExpression(range_string, dead_cards, game_type, result);
    if (ret)
    {
        arp_normalize_range_weights(result);
    }
    return ret;
}

/* Convert to PlayerRange */
int ARP_ToPlayerRange(const arp_range_t *arp_range, PlayerRange *player_range)
{
    if (!arp_range || !player_range)
        return 0;

    player_range->hand_masks = arp_range->hands;
    player_range->weights = arp_range->has_weights ? arp_range->weights : NULL;
    player_range->count = (int)arp_range->count;
    player_range->total_weight = arp_range->has_weights ? arp_range->total_weight : (double)arp_range->count;

    return 1;
}

/* Free range */
void ARP_FreeRange(arp_range_t *range)
{
    if (!range)
        return;

    if (range->hands)
    {
        free(range->hands);
        range->hands = NULL;
    }
    if (range->weights)
    {
        free(range->weights);
        range->weights = NULL;
    }

    if (range->internal_data) {
        arp_hash_table_t *ht = (arp_hash_table_t *)range->internal_data;
        arp_hash_free(ht);
        free(ht);
        range->internal_data = NULL;
    }

    memset(range, 0, sizeof(arp_range_t));
}

/* Get range percentage */
float ARP_GetRangePercentage(const arp_range_t *range, enum_game_t game_type)
{
    if (!range)
        return 0.0f;

    /* For Hold'em, there are C(52,2) = 1326 possible starting hands */
    int total_hands = 1326; /* This is specific to Hold'em */

    return (float)range->count / (float)total_hands;
}

/* Range to string */
int ARP_RangeToString(const arp_range_t *range, char *buffer, size_t buffer_size)
{
    if (!range || !buffer || buffer_size == 0)
        return 0;

    if (range->is_percentage)
    {
        return snprintf(buffer, buffer_size, "%.1f%% (%zu hands)",
                        range->percentage_used * 100.0f, range->count);
    }
    else
    {
        return snprintf(buffer, buffer_size, "%zu hands", range->count);
    }
}

/* Validate range string */
int ARP_ValidateRangeString(const char *range_string, char *error_buffer, size_t error_buffer_size)
{
    if (!range_string)
        return 0;

    arp_context_t ctx;
    if (!ARP_InitContext(&ctx, range_string))
    {
        if (error_buffer && error_buffer_size > 0)
        {
            snprintf(error_buffer, error_buffer_size, "Failed to initialize parser");
        }
        return 0;
    }

    int result = arp_tokenize(&ctx);

    if (!result && error_buffer && error_buffer_size > 0)
    {
        strncpy(error_buffer, ctx.error_message, error_buffer_size - 1);
        error_buffer[error_buffer_size - 1] = '\0';
    }

    ARP_FreeContext(&ctx);
    return result;
}
