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

/*
 * Example showing how to use Pineapple Hold'em evaluation
 */

#include <stdio.h>
#include <stdlib.h>

#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/games/rules_pineapple.h>

int main(void)
{
    StdDeck_CardMask pockets[2];
    StdDeck_CardMask board, dead;
    enum_result_t result;
    int err;

    printf("=== Pineapple Hold'em Example ===\n");
    printf("Each player starts with 3 hole cards and must discard 1 after the flop.\n\n");

    /* Initialize card masks */
    StdDeck_CardMask_RESET(pockets[0]);
    StdDeck_CardMask_RESET(pockets[1]);
    StdDeck_CardMask_RESET(board);
    StdDeck_CardMask_RESET(dead);

    /* Player 1: As Ah Kh (pocket aces with king kicker) */
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_ACE, StdDeck_Suit_HEARTS));
    StdDeck_CardMask_SET(pockets[0], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_HEARTS));

    /* Player 2: Ks Kd Qh (pocket kings with queen kicker) */
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_SPADES));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_KING, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(pockets[1], StdDeck_MAKE_CARD(StdDeck_Rank_QUEEN, StdDeck_Suit_HEARTS));

    /* Add pocket cards to dead cards */
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    printf("Player 1: As Ah Kh (pocket aces)\n");
    printf("Player 2: Ks Kd Qh (pocket kings)\n");
    printf("\nRunning Monte Carlo simulation (10,000 iterations)...\n");

    /* Run Monte Carlo simulation */
    err = enumSample(game_pineapple, pockets, board, dead, 2, 0, 10000, 0, &result);

    if (err != 0)
    {
        printf("ERROR: enumSample failed with error %d\n", err);
        return 1;
    }

    printf("\nResults:\n");
    printf("Player 1 (AsAhKh): %.1f%% equity\n", 100.0 * result.ev[0] / result.nsamples);
    printf("Player 2 (KsKdQh): %.1f%% equity\n", 100.0 * result.ev[1] / result.nsamples);
    printf("Total samples: %d\n", result.nsamples);

    /* Test with a flop */
    printf("\n=== With Flop ===\n");

    /* Reset dead cards to just the pocket cards */
    StdDeck_CardMask_RESET(dead);
    StdDeck_CardMask_OR(dead, dead, pockets[0]);
    StdDeck_CardMask_OR(dead, dead, pockets[1]);

    /* Set flop: 2c 7d Jh */
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_2, StdDeck_Suit_CLUBS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_7, StdDeck_Suit_DIAMONDS));
    StdDeck_CardMask_SET(board, StdDeck_MAKE_CARD(StdDeck_Rank_JACK, StdDeck_Suit_HEARTS));

    /* Add flop to dead cards */
    StdDeck_CardMask_OR(dead, dead, board);

    printf("Flop: 2c 7d Jh\n");
    printf("Running Monte Carlo simulation (10,000 iterations)...\n");

    /* Run simulation with flop */
    err = enumSample(game_pineapple, pockets, board, dead, 2, 3, 10000, 0, &result);

    if (err != 0)
    {
        printf("ERROR: enumSample with flop failed with error %d\n", err);
        return 1;
    }

    printf("\nResults with flop:\n");
    printf("Player 1 (AsAhKh): %.1f%% equity\n", 100.0 * result.ev[0] / result.nsamples);
    printf("Player 2 (KsKdQh): %.1f%% equity\n", 100.0 * result.ev[1] / result.nsamples);
    printf("Total samples: %d\n", result.nsamples);

    printf("\nNote: In Pineapple Hold'em, each player would discard their worst card after seeing the flop.\n");
    printf("The simulation automatically finds the optimal discard for each scenario.\n");

    return 0;
}
