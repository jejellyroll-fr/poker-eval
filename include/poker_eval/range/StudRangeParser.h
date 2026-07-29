/*
 * Copyright (C) 2025
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

#ifndef __STUD_RANGE_PARSER_H__
#define __STUD_RANGE_PARSER_H__

#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/deck/deck_std.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Parse a Stud pattern and add hands to range */
/* Pattern examples: "(AA)K", "(AK)Q", "(ss)Ks" */
int ARP_ParseStudPattern(const char *pattern, StdDeck_CardMask dead_cards, arp_range_t *range);

/* Check if string looks like a Stud pattern */
int ARP_IsStudPattern(const char *str, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* __STUD_RANGE_PARSER_H__ */
