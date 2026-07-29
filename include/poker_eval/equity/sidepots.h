#ifndef POKER_EVAL_SIDEPOTS_H
#define POKER_EVAL_SIDEPOTS_H

#include <poker_eval/core/enumdefs.h>

typedef struct {
    double amount;
    int eligible_players[ENUM_MAXPLAYERS];
    int num_eligible;
} sidepot_t;

int pe_calculate_sidepots(
    const double invested[],
    int num_players,
    sidepot_t* out_pots,
    int max_pots,
    double* out_total_pot
);

#endif /* POKER_EVAL_SIDEPOTS_H */
