/* preflop_equity.h
 * API publique — Équité préflop (Hold'em, Omaha, etc.)
 *
 * Copyright (C) 2025
 */

#ifndef POKER_EVAL_PREFLOP_EQUITY_H
#define POKER_EVAL_PREFLOP_EQUITY_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint32_t, uint8_t */
#include <stdbool.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/flop_equity.h>

/* Alias to enum_game_t from core/enumdefs.h */
typedef enum_game_t game_t;

/* Nombre de mains canoniques préflop :
 * 13 paires + 78 suited + 78 offsuit = 169
 */
#define PREFLOP_NUM_CANONICAL_HANDS 169
#define PREFLOP_LOOKUP_TABLE_SIZE (PREFLOP_NUM_CANONICAL_HANDS * PREFLOP_NUM_CANONICAL_HANDS)

/* Type identifiant une main canonique (0..168) */
typedef int preflop_hand_t;

/* Classes de mains */
typedef enum
{
    PREFLOP_PAIR = 0,
    PREFLOP_SUITED,
    PREFLOP_OFFSUIT
} preflop_hand_class_t;

/* Strength categories */
typedef enum {
    PREFLOP_STRENGTH_PREMIUM = 0,   // AA, KK, QQ, AKs
    PREFLOP_STRENGTH_STRONG,        // JJ, TT, AQs, AKo
    PREFLOP_STRENGTH_MEDIUM,        // 99-66, AJs, KQs
    PREFLOP_STRENGTH_WEAK,          // Small pairs, suited connectors
    PREFLOP_STRENGTH_TRASH          // Junk
} preflop_strength_t;

preflop_strength_t preflop_classify(preflop_hand_t hand);

/* Conversion de cartes ↔ mains canoniques */
preflop_hand_t preflop_cards_to_hand(int card1, int card2);
void preflop_hand_to_string(preflop_hand_t hand, char *out_str);
int preflop_string_to_hand(const char *str);
preflop_hand_class_t preflop_hand_class(preflop_hand_t hand);

/* Range de mains */
typedef struct preflop_range_s
{
    uint32_t bitmap[(PREFLOP_NUM_CANONICAL_HANDS + 31) / 32];
    int num_hands;
} preflop_range_t;

void preflop_range_init(preflop_range_t *range);
void preflop_range_add(preflop_range_t *range, preflop_hand_t hand);
int preflop_range_contains(const preflop_range_t *range, preflop_hand_t hand);
int preflop_range_parse(const char *range_str, preflop_range_t *out_range);
void preflop_range_to_string(const preflop_range_t *range, char *out_str, size_t max_len);

int preflop_range_count_combinations(const preflop_range_t *range);
int preflop_hand_enumerate_combinations(preflop_hand_t hand, int out_combos[][2], int max_combos);

/* Lookup table forward declaration */
typedef struct preflop_lookup_table preflop_lookup_table_t;

/* Structures pour le calcul d'équité */
typedef struct preflop_equity_input_s
{
    preflop_range_t range1;
    preflop_range_t range2;
    size_t num_samples; /* 0 = exhaustif */
    preflop_lookup_table_t *lookup_table; /* Optional: use for instant lookups */
} preflop_equity_input_t;

typedef struct preflop_equity_result_s
{
    double equity1;
    double equity2;
    double tie_equity;
    size_t num_matchups;
    double **equity_matrix; /* [169][169], peut être NULL */
} preflop_equity_result_t;

/* Calcul d’équité */
int preflop_equity_calculate(const preflop_equity_input_t *input,
                             preflop_equity_result_t *result);
double **preflop_equity_matrix_alloc(void);
void preflop_equity_matrix_free(double **matrix);
void preflop_equity_result_free(preflop_equity_result_t *result);

/* Lookup table functions */

preflop_lookup_table_t *preflop_lookup_table_load(const char *filename, game_t game);
preflop_lookup_table_t *preflop_lookup_table_load_default(void);
preflop_lookup_table_t *preflop_lookup_table_create(game_t game);
int preflop_lookup_table_save(const preflop_lookup_table_t *table, const char *filename);
void preflop_lookup_table_free(preflop_lookup_table_t *table);
double preflop_lookup_table_get(const preflop_lookup_table_t *table,
                                preflop_hand_t hand1, preflop_hand_t hand2);
void preflop_lookup_table_set(preflop_lookup_table_t *table,
                              preflop_hand_t hand1, preflop_hand_t hand2,
                              double equity);
game_t preflop_lookup_table_game(const preflop_lookup_table_t *table);

#endif /* POKER_EVAL_PREFLOP_EQUITY_H */
