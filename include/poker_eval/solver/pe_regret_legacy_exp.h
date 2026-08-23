/*
 * pe_regret_legacy_exp.h - Legacy ECFR exponential policy (ALG-05)
 *
 * This is deliberately separate from canonical regret matching. It preserves
 * the v2 ECFR policy so the migration can select it explicitly without
 * changing the meaning of CFR, CFR+ or DCFR.
 */

#ifndef POKER_EVAL_PE_REGRET_LEGACY_EXP_H
#define POKER_EVAL_PE_REGRET_LEGACY_EXP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compute the v2 ECFR strategy for a dense action/combo regret span.
 *
 * Positive regrets receive exp(lambda * (regret - max_positive)); non-positive
 * regrets receive zero. The maximum is subtracted before exponentiation to
 * preserve the historical policy while avoiding avoidable overflow. When no
 * action has positive regret, the result is uniform, as in cfr_storage_t.
 *
 * Arrays use the layout [action][combo]. Lambda must be finite and positive.
 *
 * @return 0 on success, -1 for invalid arguments or non-finite regrets.
 */
int pe_regret_match_legacy_exp_vector(const double *regrets, double *strategy,
                                      uint16_t action_count,
                                      uint16_t combo_count,
                                      double lambda);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_REGRET_LEGACY_EXP_H */
