/*
 * Copyright (C) 2024
 *           Poker-eval contributors
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

#ifndef __RULES_BADUGI_H__
#define __RULES_BADUGI_H__

#include <poker_eval/core/pokereval_export.h>
#include <stddef.h>
#include <poker_eval/core/poker_defs.h>

/* Badugi hand types - for enumeration compatibility, higher is better */
#define BadugiRules_HandType_BADUGI 3 /* 4 cards, all different suits and ranks (best) */
#define BadugiRules_HandType_THREE 2  /* 3 cards, different suits and ranks */
#define BadugiRules_HandType_TWO 1    /* 2 cards, different suits and ranks */
#define BadugiRules_HandType_ONE 0    /* 1 card (worst possible hand) */

#define BadugiRules_HandType_FIRST BadugiRules_HandType_BADUGI
#define BadugiRules_HandType_LAST BadugiRules_HandType_ONE
#define BadugiRules_HandType_COUNT 4

/* Badacey hand types - combines Badugi with A-5 lowball */
#define BadaceyRules_HandType_BADUGI 3 /* 4-card Badugi */
#define BadaceyRules_HandType_THREE 2  /* 3-card Badugi */
#define BadaceyRules_HandType_TWO 1    /* 2-card Badugi */
#define BadaceyRules_HandType_ONE 0    /* 1-card Badugi */
#define BadaceyRules_HandType_A5_LOW 4 /* A-5 lowball (no Badugi) */

#define BadaceyRules_HandType_FIRST BadaceyRules_HandType_BADUGI
#define BadaceyRules_HandType_LAST BadaceyRules_HandType_A5_LOW
#define BadaceyRules_HandType_COUNT 5

/* Badeucy hand types - combines Badugi with 2-7 lowball */
#define BadeucyRules_HandType_BADUGI 3 /* 4-card Badugi */
#define BadeucyRules_HandType_THREE 2  /* 3-card Badugi */
#define BadeucyRules_HandType_TWO 1    /* 2-card Badugi */
#define BadeucyRules_HandType_ONE 0    /* 1-card Badugi */
#define BadeucyRules_HandType_27_LOW 4 /* 2-7 lowball (no Badugi) */

#define BadeucyRules_HandType_FIRST BadeucyRules_HandType_BADUGI
#define BadeucyRules_HandType_LAST BadeucyRules_HandType_27_LOW
#define BadeucyRules_HandType_COUNT 5

extern POKEREVAL_EXPORT const char *BadugiRules_handTypeNames[BadugiRules_HandType_COUNT];
extern POKEREVAL_EXPORT const char *BadugiRules_handTypeNamesPadded[BadugiRules_HandType_COUNT];
extern POKEREVAL_EXPORT const char *BadaceyRules_handTypeNames[BadaceyRules_HandType_COUNT];
extern POKEREVAL_EXPORT const char *BadaceyRules_handTypeNamesPadded[BadaceyRules_HandType_COUNT];
extern POKEREVAL_EXPORT const char *BadeucyRules_handTypeNames[BadeucyRules_HandType_COUNT];
extern POKEREVAL_EXPORT const char *BadeucyRules_handTypeNamesPadded[BadeucyRules_HandType_COUNT];

extern POKEREVAL_EXPORT int BadugiRules_nSigCards[BadugiRules_HandType_COUNT];
extern POKEREVAL_EXPORT int BadaceyRules_nSigCards[BadaceyRules_HandType_COUNT];
extern POKEREVAL_EXPORT int BadeucyRules_nSigCards[BadeucyRules_HandType_COUNT];

/* Bounded variant; prefer it. size includes the terminator. */
extern POKEREVAL_EXPORT int BadugiRules_HandVal_toString_n(HandVal handval, char *outString, size_t size);
/* Requires a buffer of at least POKER_EVAL_HANDVAL_STRING_MAX bytes. */
extern POKEREVAL_EXPORT int BadugiRules_HandVal_toString(HandVal handval, char *outString);
extern POKEREVAL_EXPORT int BadugiRules_HandVal_print(HandVal handval);
/* Bounded variant; prefer it. size includes the terminator. */
extern POKEREVAL_EXPORT int BadaceyRules_HandVal_toString_n(HandVal handval, char *outString, size_t size);
/* Requires a buffer of at least POKER_EVAL_HANDVAL_STRING_MAX bytes. */
extern POKEREVAL_EXPORT int BadaceyRules_HandVal_toString(HandVal handval, char *outString);
extern POKEREVAL_EXPORT int BadaceyRules_HandVal_print(HandVal handval);
/* Bounded variant; prefer it. size includes the terminator. */
extern POKEREVAL_EXPORT int BadeucyRules_HandVal_toString_n(HandVal handval, char *outString, size_t size);
/* Requires a buffer of at least POKER_EVAL_HANDVAL_STRING_MAX bytes. */
extern POKEREVAL_EXPORT int BadeucyRules_HandVal_toString(HandVal handval, char *outString);
extern POKEREVAL_EXPORT int BadeucyRules_HandVal_print(HandVal handval);

#endif /* __RULES_BADUGI_H__ */
