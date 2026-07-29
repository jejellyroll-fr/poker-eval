/* 
 * eval.c
 * Copyright 1999 Brian Goetz
 * 
 * An example program for the poker hand evaluation library.
 * All it does is evaluate the hand given on the command line
 * and print out the value. 
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/poker_eval.h>

int gNCards;
StdDeck_CardMask gCards;
int gLow=0;
int gHighLow=0;

static void
parseArgs(int argc, char **argv) {
  int i, c;

  for (i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-low"))
      gLow = 1;
    else if (!strcmp(argv[i], "-hl")) 
      gHighLow = 1;
    else {
      if (StdDeck_stringToCard(argv[i], &c) == 0)
        goto error;
      if (!StdDeck_CardMask_CARD_IS_SET(gCards, c)) {
        StdDeck_CardMask_SET(gCards, c);
        ++gNCards;
      };
    };
  }
  
  return;

 error:
  fprintf(stderr, "Usage: eval [ -low ] [ -hl ] cards \n");
  exit(0);
}


int
main(int argc, char **argv) {
  HandVal handval;
#if defined(Hand_EVAL_LOW)
  LowHandVal low;
#endif

  gNCards = 0;
  StdDeck_CardMask_RESET(gCards);
  parseArgs(argc, argv);

  if (!gLow) {
    char cardstr[100];
    handval = Hand_EVAL_N(gCards, gNCards);
    StdDeck_maskString(gCards, cardstr);
    printf("%s: ", cardstr);
    HandVal_print(handval);
    printf("\n");
  };

  if (gLow || gHighLow) {
#if defined(Hand_EVAL_LOW)
    if (gNCards < 5)
      printf("Not enough cards to evaluate low hand\n");
    else {
      char cardstr[100];
      low = Hand_EVAL_LOW(gCards, gNCards);
      StdDeck_maskString(gCards, cardstr);
      printf("%s (low): ", cardstr);
      LowHandVal_print(low);
      printf("\n");
    };
#else
    printf("Low evaluator not available \n");
#endif
  };

  return 0;
}
