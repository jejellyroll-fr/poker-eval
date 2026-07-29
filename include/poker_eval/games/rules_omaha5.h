#ifndef __RULES_OMAHA5_H__
#define __RULES_OMAHA5_H__

#include <poker_eval/games/rules_std.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Omaha5Rules_MIN_POCKET 5
#define Omaha5Rules_MAX_POCKET 5

/* The evaluation logic for Omaha 5 is handled by StdDeck_OmahaHiLow8_EVAL
   in eval_omaha.h which supports 4-6 cards generically.
   This header is mainly for game definition completeness. */

#ifdef __cplusplus
}
#endif

#endif
