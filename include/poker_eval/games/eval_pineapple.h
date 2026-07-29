/*
 * Copyright (C) 2024
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

#ifndef __EVAL_PINEAPPLE_H__
#define __EVAL_PINEAPPLE_H__

#include <poker_eval/core/poker_defs.h>
#include <poker_eval/games/rules_pineapple.h>

/*
 * Pineapple Hold'em evaluation:
 * 1. Player starts with 3 hole cards
 * 2. After the flop, player must discard one card
 * 3. The remaining 2 cards are used like regular Hold'em
 *
 * For evaluation purposes, we simulate the optimal discard by trying
 * all 3 possible 2-card combinations and choosing the best one.
 *
 * The actual implementation is in rules_pineapple.c to avoid inline conflicts.
 */

#endif /* __EVAL_PINEAPPLE_H__ */
