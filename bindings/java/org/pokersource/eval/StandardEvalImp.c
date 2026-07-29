/* $Id: StandardEvalImp.c 353 2002-06-28 05:56:19Z mjmaurer $ */

#include <stdio.h>
#include <jni.h>
#include "pokerjni.h"	/* javah output for us to implement */
#include "jniutil.h"	/* JNI help like exception throwing */
#include <poker_eval/core/poker_defs.h>	/* poker-eval basics */
#include "pokutil.h"	/* poker-eval help like card parsing */

#include <poker_eval/deck/deck_std.h>
#include <poker_eval/games/rules_std.h>
#include <poker_eval/core/inlines/eval.h>
#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>

JNIEXPORT jlong JNICALL Java_org_pokersource_eval_StandardEval_EvalHigh
   (JNIEnv *env, jclass class, jintArray ranks, jintArray suits)
{
  int ncards;
  StdDeck_CardMask mcards;
  HandVal hival;
  
  if (parseStandardRanksSuits(env, ranks, suits, &mcards, &ncards)) {
    jniThrow(env, "unable to parse input cards");
    return (jlong)0;
  }
  hival = StdDeck_StdRules_EVAL_N(mcards, ncards);
#ifdef DEBUG
  printf("In C: Hand [%s] => Eval: %d ",
         DmaskString(StdDeck, mcards), hival);
  StdRules_HandVal_print(hival);
  printf("\n");
#endif
  return (jlong)hival;
}

JNIEXPORT jlong JNICALL Java_org_pokersource_eval_StandardEval_EvalLow
   (JNIEnv *env, jclass class, jintArray ranks, jintArray suits)
{
  int ncards;
  StdDeck_CardMask mcards;
  LowHandVal loval;
  
  if (parseStandardRanksSuits(env, ranks, suits, &mcards, &ncards)) {
    jniThrow(env, "unable to parse input cards");
    return (jlong)0;
  }
  loval = pe_eval_low_a5(mcards);
#ifdef DEBUG
  printf("In C: Hand [%s] => Eval: %d ",
         DmaskString(StdDeck, mcards), loval);
  LowHandVal_print(loval);
  printf("\n");
#endif
  return (jlong)loval;
}

JNIEXPORT jlong JNICALL Java_org_pokersource_eval_StandardEval_EvalLow8
   (JNIEnv *env, jclass class, jintArray ranks, jintArray suits)
{
  int ncards;
  StdDeck_CardMask mcards;
  LowHandVal lo8val;
  
  if (parseStandardRanksSuits(env, ranks, suits, &mcards, &ncards)) {
    jniThrow(env, "unable to parse input cards");
    return (jlong)0;
  }
  lo8val = pe_eval_low_a5(mcards);
  if (!pe_low_qualify5(lo8val, LOW_QUALIFIER_8))
    lo8val = LowHandVal_NOTHING;
#ifdef DEBUG
  printf("In C: Hand [%s] => Eval: %d ",
         DmaskString(StdDeck, mcards), lo8val);
  LowHandVal_print(lo8val);
  printf("\n");
#endif
  return (jlong)lo8val;
}
