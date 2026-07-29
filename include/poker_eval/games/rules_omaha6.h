#ifndef __RULES_OMAHA6_H__
#define __RULES_OMAHA6_H__

#include <poker_eval/games/rules_std.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Omaha6Rules_MIN_POCKET 6
#define Omaha6Rules_MAX_POCKET 6

/* The evaluation logic for Omaha 6 is handled by StdDeck_OmahaHiLow8_EVAL
   in eval_omaha.h which supports 4-6 cards generically.
   This header is mainly for game definition completeness. */

#ifdef __cplusplus
}
#endif

#endif
