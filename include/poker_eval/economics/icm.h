#ifndef POKER_EVAL_ECONOMICS_ICM_H
#define POKER_EVAL_ECONOMICS_ICM_H

#ifdef __cplusplus
extern "C" {
#endif

#define ICM_MAX_PLAYERS 23

typedef struct {
    double stacks[ICM_MAX_PLAYERS];
    int num_players;
    double payouts[ICM_MAX_PLAYERS];
    int num_payouts;
} icm_input_t;

typedef struct {
    double icm_ev[ICM_MAX_PLAYERS];   /* EV in currency units */
    double equity[ICM_MAX_PLAYERS];   /* Equity share (0.0-1.0) */
} icm_result_t;

/* Calculate ICM (Malmuth-Harville model) */
int pe_icm_calculate(const icm_input_t *input, icm_result_t *result);

/* Calculate ICM chop for a tournament deal */
/* Returns 0 on success */
int pe_icm_chop(const icm_input_t *input, double *out_payouts);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_ECONOMICS_ICM_H */
