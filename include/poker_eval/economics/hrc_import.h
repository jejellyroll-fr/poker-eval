/* Portable JSON import and pot accounting for HRC/PKO-style trees. */
#ifndef POKER_EVAL_ECONOMICS_HRC_IMPORT_H
#define POKER_EVAL_ECONOMICS_HRC_IMPORT_H

#include <stddef.h>
#include <stdint.h>

#include <poker_eval/economics/hrc.h>
#include <poker_eval/economics/pko.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t offset;
    char message[128];
} pe_hrc_import_error_t;

typedef struct {
    int player_count;
    double initial_pot;
    double stacks[PE_HRC_MAX_PLAYERS];
    double antes[PE_HRC_MAX_PLAYERS];
} pe_hrc_pot_model_t;

typedef struct {
    double committed[PE_HRC_MAX_PLAYERS];
    int folded[PE_HRC_MAX_PLAYERS];
    pe_hrc_pot_slice_t slices[PE_HRC_MAX_PLAYERS];
    unsigned slice_count;
    double total_pot;
} pe_hrc_pot_trace_t;

typedef struct {
    pe_hrc_config_t config;
    pe_range_t *owned_ranges[PE_HRC_MAX_PLAYERS];
    pe_hrc_node_t *owned_nodes;
    char *range_text[PE_HRC_MAX_PLAYERS];
    char *action_labels[PE_HRC_MAX_NODES][PE_HRC_MAX_ACTIONS];
    pe_hrc_pot_model_t pot_model;
    double bounties[ICM_MAX_PLAYERS];
    double payouts[ICM_MAX_PLAYERS];
    int num_payouts;
    double bounty_multiplier;
    int has_bounty_multiplier;
} pe_hrc_import_t;

/* Supported document shape: {schema:"pe-hrc/v1"|"pe-pko/v1",
 * variant:"holdem"|"omaha"|"plo5"|"plo6", root:0,
 * players:[{stack,ante,range,bounty}], nodes:[{terminal,player,actions:[
 * {label,amount,child}]}], payouts:[...], bounty_multiplier}. Action amounts
 * are incremental chips; antes are posted before the root action. Unknown JSON
 * fields are ignored for forward-compatible room adapters. */
int pe_hrc_import_json(const char *json, size_t length,
                       pe_hrc_terminal_fn terminal_value, void *user_data,
                       pe_hrc_import_t *out, pe_hrc_import_error_t *error);

int pe_hrc_import_json_file(const char *path,
                            pe_hrc_terminal_fn terminal_value, void *user_data,
                            pe_hrc_import_t *out, pe_hrc_import_error_t *error);

void pe_hrc_import_free(pe_hrc_import_t *imported);

/* Replay an action path from an imported tree and build main/side-pot slices.
 * `path` contains action indexes exactly as received by pe_hrc_terminal_fn. */
int pe_hrc_trace_pot(const pe_hrc_tree_t *tree,
                     const pe_hrc_pot_model_t *model,
                     const uint16_t *path, size_t path_length,
                     pe_hrc_pot_trace_t *out);

/* Convert imported player stacks, ranges, payouts and bounties into the PKO
 * range enumerator input. The caller supplies the outcome evaluator. */
int pe_hrc_import_make_pko_input(const pe_hrc_import_t *imported,
                                 size_t max_profiles,
                                 pe_pko_range_outcome_fn outcome,
                                 void *user_data,
                                 pe_pko_range_input_t *out);

#ifdef __cplusplus
}
#endif

#endif
