/*
 * pe_regret.h - Vector regret matching (architecture v3, VEC-03)
 */

#ifndef POKER_EVAL_PE_REGRET_H
#define POKER_EVAL_PE_REGRET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compute vanilla regret-matching strategy probabilities.
 *
 * Both arrays use the storage layout [action][combo]. Positive regrets are
 * normalized independently for every combo. If a combo has no positive
 * regret, all actions receive 1/action_count.
 *
 * @return 0 on success, -1 for NULL arrays or a zero dimension.
 */
int pe_regret_match_vector(const double *regrets, double *strategy,
                           uint16_t action_count, uint16_t combo_count);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_REGRET_H */
