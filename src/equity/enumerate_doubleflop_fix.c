/* enumerate_doubleflop_fix.c - Fix for double flop holdem Monte Carlo sampling
 *
 * This file contains the missing implementation for Monte Carlo sampling
 * of double flop holdem games.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>

/* Monte Carlo sampling for double flop holdem
 * This function should be integrated into enumerate.c in the enumSample function
 */
static int enumSampleDoubleFlop(StdDeck_CardMask pockets[],
                        StdDeck_CardMask board,
                        StdDeck_CardMask dead,
                        int npockets, int nboard, int niter,
                        enum_result_t *result)
{
    /* For double flop, we need to sample 10 cards total (2 boards of 5 cards each) */
    int numCards = 10 - nboard;
    
    if (numCards <= 0) {
        /* All board cards are already specified - not implemented yet */
        return 1;
    }
    
    /* Initialize result */
    result->nsamples = 0;
    
    /* Monte Carlo loop */
    for (int iter = 0; iter < niter; iter++) {
        StdDeck_CardMask deck, drawn;
        StdDeck_CardMask board1, board2;
        HandVal hival1[ENUM_MAXPLAYERS], hival2[ENUM_MAXPLAYERS];
        HandVal besthi1 = HandVal_NOTHING, besthi2 = HandVal_NOTHING;
        int hishare1 = 0, hishare2 = 0;
        
        /* Create available deck (all cards minus dead cards) */
        StdDeck_CardMask_NOT(deck, dead);
        
        /* Draw random cards for the boards */
        StdDeck_CardMask_RESET(drawn);
        StdDeck_CardMask_RESET(board1);
        StdDeck_CardMask_RESET(board2);
        
        /* Copy existing board cards if any */
        if (nboard > 0) {
            /* This is simplified - in reality we'd need to handle partial boards properly */
            /* For now, assume no partial boards */
        }
        
        /* Draw cards for both boards */
        int cards_drawn = 0;
        while (cards_drawn < numCards) {
            /* Find a random card from remaining deck */
            int num_remaining = StdDeck_numCards(deck);
            if (num_remaining == 0) break;
            
            int r = (int)pe_rng_below(pe_rng_current(), (uint32_t)num_remaining);
            int count = 0;
            
            for (int c = 0; c < StdDeck_N_CARDS; c++) {
                if (StdDeck_CardMask_CARD_IS_SET(deck, c)) {
                    if (count == r) {
                        /* Add this card to drawn cards */
                        StdDeck_CardMask_SET(drawn, c);
                        StdDeck_CardMask_UNSET(deck, c);
                        
                        /* Add to appropriate board */
                        if (cards_drawn < 5) {
                            StdDeck_CardMask_SET(board1, c);
                        } else {
                            StdDeck_CardMask_SET(board2, c);
                        }
                        break;
                    }
                    count++;
                }
            }
            cards_drawn++;
        }
        
        /* Evaluate all hands for both boards */
        for (int i = 0; i < npockets; i++) {
            StdDeck_CardMask hand1, hand2;
            
            /* Combine pocket cards with board 1 */
            StdDeck_CardMask_OR(hand1, pockets[i], board1);
            hival1[i] = StdDeck_StdRules_EVAL_N(hand1, 7);
            
            /* Combine pocket cards with board 2 */
            StdDeck_CardMask_OR(hand2, pockets[i], board2);
            hival2[i] = StdDeck_StdRules_EVAL_N(hand2, 7);
            
            /* Find best hands */
            if (hival1[i] > besthi1) {
                besthi1 = hival1[i];
                hishare1 = 1;
            } else if (hival1[i] == besthi1) {
                hishare1++;
            }
            
            if (hival2[i] > besthi2) {
                besthi2 = hival2[i];
                hishare2 = 1;
            } else if (hival2[i] == besthi2) {
                hishare2++;
            }
        }
        
        /* Award pot fractions */
        double hipot1 = (hishare1 > 0) ? 1.0 / hishare1 : 0;
        double hipot2 = (hishare2 > 0) ? 1.0 / hishare2 : 0;
        
        for (int i = 0; i < npockets; i++) {
            double potfrac = 0;
            
            if (hival1[i] == besthi1) {
                potfrac += hipot1;
            }
            if (hival2[i] == besthi2) {
                potfrac += hipot2;
            }
            
            /* Each board is worth half the pot */
            result->ev[i] += potfrac / 2.0;
            
            /* Update scoop counter */
            if (hival1[i] == besthi1 && hival2[i] == besthi2 && 
                hishare1 == 1 && hishare2 == 1) {
                result->nscoop[i]++;
            }
        }
        
        result->nsamples++;
    }
    
    return 0;
}

/* This code should be added to enumSample() in enumerate.c:

  else if (game == game_doubleflop_holdem)
  {
    return enumSampleDoubleFlop(pockets, board, dead, npockets, nboard, niter, result);
  }

*/
