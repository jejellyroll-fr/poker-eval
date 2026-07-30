/* compressed_tables.c - Compressed and aligned lookup tables */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/utils/compressed_tables.h>

#if defined(__GNUC__) || defined(__clang__)
  #define CACHE_ALIGNED __attribute__((aligned(64)))
#elif defined(_MSC_VER)
  #define CACHE_ALIGNED __declspec(align(64))
#else
  #define CACHE_ALIGNED
#endif

typedef struct { uint64_t bits[128]; } BitVector; /* 8192 / 64 */
typedef struct {
    BitVector non_zero_mask;
    uint16_t num_non_zero;
    uint8_t values[];
} CompressedStraightTable;

static CACHE_ALIGNED CompressedStraightTable* compressed_straight_table = NULL;

static inline int popcount64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(x);
#else
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
    x = x + (x >> 8); x = x + (x >> 16); x = x + (x >> 32);
    return x & 0x7f;
#endif
}

static void build_compressed_straight_table(void) {
    int num_non_zero = 0; for (int i = 0; i < 8192; i++) if (straightTable[i] != 0) num_non_zero++;
    size_t size = sizeof(CompressedStraightTable) + num_non_zero * sizeof(uint8_t); size = (size + 63) & ~63;
#if defined(_WIN32)
    compressed_straight_table = (CompressedStraightTable*)_aligned_malloc(size, 64);
#else
    if (posix_memalign((void**)&compressed_straight_table, 64, size) != 0) { compressed_straight_table = NULL; return; }
#endif
    if (!compressed_straight_table) return;
    memset(compressed_straight_table, 0, size); compressed_straight_table->num_non_zero = (uint16_t)num_non_zero;
    int value_idx = 0; for (int i = 0; i < 8192; i++) if (straightTable[i] != 0) {
        int word_idx = i / 64; int bit_idx = i % 64;
        compressed_straight_table->non_zero_mask.bits[word_idx] |= (1ULL << bit_idx);
        compressed_straight_table->values[value_idx++] = straightTable[i];
    }
}

static inline uint8_t lookup_compressed_straight(uint16_t index) {
    if (!compressed_straight_table) return straightTable[index];
    int word_idx = index / 64; int bit_idx = index % 64;
    if (!(compressed_straight_table->non_zero_mask.bits[word_idx] & (1ULL << bit_idx))) return 0;
    int value_idx = 0; for (int i = 0; i < word_idx; i++) value_idx += popcount64(compressed_straight_table->non_zero_mask.bits[i]);
    uint64_t mask = (1ULL << bit_idx) - 1; value_idx += popcount64(compressed_straight_table->non_zero_mask.bits[word_idx] & mask);
    return compressed_straight_table->values[value_idx];
}

static void build_compressed_topfive_table(void) { /* not compressed */ }
static inline uint32_t lookup_compressed_topfive(uint16_t index) { return topFiveCardsTable[index]; }

void init_compressed_tables(void) { build_compressed_straight_table(); build_compressed_topfive_table(); }
void cleanup_compressed_tables(void) {
    if (compressed_straight_table) {
#if defined(_WIN32)
        _aligned_free(compressed_straight_table);
#else
        free(compressed_straight_table);
#endif
        compressed_straight_table = NULL;
    }
}

uint8_t get_straight_value(uint16_t index) { return compressed_straight_table ? lookup_compressed_straight(index) : straightTable[index]; }
uint32_t get_topfive_value(uint16_t index) { return lookup_compressed_topfive(index); }
void get_compression_stats(size_t* original_size, size_t* compressed_size) {
    *original_size = 8192 * sizeof(uint8_t) + 8192 * sizeof(uint32_t);
    *compressed_size = 0;
    if (compressed_straight_table)
        *compressed_size += sizeof(CompressedStraightTable) + compressed_straight_table->num_non_zero * sizeof(uint8_t);
    else *compressed_size += 8192 * sizeof(uint8_t);
    *compressed_size += 8192 * sizeof(uint32_t);
}
