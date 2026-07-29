/*
 *  Copyright 2006 Michael Maurer <mjmaurer@yahoo.com>, 
 *                 Brian Goetz <brian@quiotix.com>, 
 *                 Loic Dachary <loic@dachary.org>, 
 *                 Tim Showalter <tjs@psaux.com>
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

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/deck/deck_astud.h>
#include <poker_eval/games/rules_astud.h>

const char *AStudRules_handTypeNames[AStudRules_HandType_LAST+1] = {
  "NoPair",
  "OnePair",
  "TwoPair",
  "Trips",
  "Straight",
  "FlHouse",
  "Flush",
  "Quads",
  "StFlush"
};

const char *AStudRules_handTypeNamesPadded[AStudRules_HandType_LAST+1] = {
  "NoPair  ",
  "OnePair ",
  "TwoPair ",
  "Trips   ",
  "Straight",
  "FlHouse ",
  "Flush   ",
  "Quads   ",
  "StFlush "
};

int AStudRules_nSigCards[AStudRules_HandType_LAST+1] = {
  5, 
  4, 
  3, 
  3, 
  1, 
  2,
  5, 
  2, 
  1
};


static inline void advance_cursor(char **cursor, size_t *remaining, int written) {
  if (*remaining == 0)
    return;

  if (written < 0) {
    *remaining = 0;
    return;
  }

  if ((size_t)written >= *remaining) {
    *cursor += *remaining;
    *remaining = 0;
    return;
  }

  *cursor += written;
  *remaining -= (size_t)written;
}

int 
AStudRules_HandVal_toString(HandVal handval, char *outString) {
  if (!outString)
    return 0;

  char *p = outString;
  const size_t BUFFER_SIZE = 80;
  size_t remaining = BUFFER_SIZE;
  int htype = HandVal_HANDTYPE(handval);

  int written = snprintf(p, remaining, "%s (", AStudRules_handTypeNames[htype]);
  advance_cursor(&p, &remaining, written);

  if (remaining > 0 && AStudRules_nSigCards[htype] >= 1) {
    written = snprintf(p, remaining, "%c",
                       AStudDeck_rankChars[HandVal_TOP_CARD(handval)]);
    advance_cursor(&p, &remaining, written);
  }

  if (remaining > 0 && AStudRules_nSigCards[htype] >= 2) {
    written = snprintf(p, remaining, " %c",
                       AStudDeck_rankChars[HandVal_SECOND_CARD(handval)]);
    advance_cursor(&p, &remaining, written);
  }

  if (remaining > 0 && AStudRules_nSigCards[htype] >= 3) {
    written = snprintf(p, remaining, " %c",
                       AStudDeck_rankChars[HandVal_THIRD_CARD(handval)]);
    advance_cursor(&p, &remaining, written);
  }

  if (remaining > 0 && AStudRules_nSigCards[htype] >= 4) {
    written = snprintf(p, remaining, " %c",
                       AStudDeck_rankChars[HandVal_FOURTH_CARD(handval)]);
    advance_cursor(&p, &remaining, written);
  }

  if (remaining > 0 && AStudRules_nSigCards[htype] >= 5) {
    written = snprintf(p, remaining, " %c",
                       AStudDeck_rankChars[HandVal_FIFTH_CARD(handval)]);
    advance_cursor(&p, &remaining, written);
  }

  if (remaining > 0) {
    written = snprintf(p, remaining, ")");
    advance_cursor(&p, &remaining, written);
  }

  return (int)(p - outString);
}

int 
AStudRules_HandVal_print(HandVal handval) {
  char buf[80];
  int n;

  n = AStudRules_HandVal_toString(handval, buf);
  printf("%s", buf);
  return n;
}
