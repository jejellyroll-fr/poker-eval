#ifndef POKER_EVAL_CORE_LOW_QUALIFIER_H
#define POKER_EVAL_CORE_LOW_QUALIFIER_H

#include <stdbool.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/handval_low.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOW_QUALIFIER_NONE = 0,
    LOW_QUALIFIER_8,
    LOW_QUALIFIER_7
} low_qualifier_t;

bool pe_low_qualify5(LowHandVal hand_low, low_qualifier_t qualifier);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_CORE_LOW_QUALIFIER_H */
