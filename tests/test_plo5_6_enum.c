/*
 * test_plo5_6_enum.c - Integration test for PLO5/PLO6 enumeration
 *
 * Verifies that enumExhaustive() correctly handles game_omaha5 and game_omaha6
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>

static void parse_cards(const char *str, StdDeck_CardMask *mask) {
  StdDeck_CardMask_RESET(*mask);
  char rank, suit;
  int i = 0;
  while (str[i] != '\0') {
    if (str[i] == ' ' || str[i] == ',') {
      i++;
      continue;
    }
    rank = str[i++];
    suit = str[i++];
    int r = StdDeck_Rank_2, s = StdDeck_Suit_HEARTS;
    switch (rank) {
    case 'A':
    case 'a':
      r = StdDeck_Rank_ACE;
      break;
    case 'K':
    case 'k':
      r = StdDeck_Rank_KING;
      break;
    case 'Q':
    case 'q':
      r = StdDeck_Rank_QUEEN;
      break;
    case 'J':
    case 'j':
      r = StdDeck_Rank_JACK;
      break;
    case 'T':
    case 't':
      r = StdDeck_Rank_TEN;
      break;
    case '9':
      r = StdDeck_Rank_9;
      break;
    case '8':
      r = StdDeck_Rank_8;
      break;
    case '7':
      r = StdDeck_Rank_7;
      break;
    case '6':
      r = StdDeck_Rank_6;
      break;
    case '5':
      r = StdDeck_Rank_5;
      break;
    case '4':
      r = StdDeck_Rank_4;
      break;
    case '3':
      r = StdDeck_Rank_3;
      break;
    case '2':
      r = StdDeck_Rank_2;
      break;
    }
    switch (suit) {
    case 'h':
    case 'H':
      s = StdDeck_Suit_HEARTS;
      break;
    case 'd':
    case 'D':
      s = StdDeck_Suit_DIAMONDS;
      break;
    case 'c':
    case 'C':
      s = StdDeck_Suit_CLUBS;
      break;
    case 's':
    case 'S':
      s = StdDeck_Suit_SPADES;
      break;
    }
    StdDeck_CardMask_SET(*mask, StdDeck_MAKE_CARD(r, s));
  }
}

static void test_plo5_enum(void) {
  printf("Testing PLO5 Enumeration...\n");
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;

  /* Player 1: As Ah Ks Kh Qh (Nut flush draw) */
  parse_cards("AsAhKsKhQh", &pockets[0]);
  /* Player 2: 2c 2d 3c 3d 4c (Low pair, straight draw) */
  parse_cards("2c2d3c3d4c", &pockets[1]);

  /* Board: Js 9s 8s (Flush for P1) */
  parse_cards("Js9s8s", &board);

  StdDeck_CardMask_RESET(dead);

  /* Run exhaustive enumeration for remaining 2 cards */
  int err = enumExhaustive(game_omaha5, pockets, board, dead, 2, 3, 0, &result);

  if (err != 0) {
    printf("enumExhaustive failed with error %d\n", err);
    exit(1);
  }

  printf("Samples: %d\n", result.nsamples);
  /* C(40, 2) = 780 samples expected (52 - 10 - 3 = 39 cards? No, 52 - 5 - 5 - 3
   * = 39 cards remaining) */
  /* Wait: 52 total. P1(5) + P2(5) + Board(3) = 13 cards known.
     Remaining = 39 cards.
     We need 2 more board cards.
     C(39, 2) = (39 * 38) / 2 = 741 samples. */

  printf("Expected samples: 741\n");
  assert(result.nsamples == 741);

  /* P1 should win significantly more due to flush */
  printf("P1 Wins: %d, P2 Wins: %d\n", result.nwinhi[0], result.nwinhi[1]);
  assert(result.nwinhi[0] > result.nwinhi[1]);

  printf("✓ PLO5 Enumeration Passed\n");
}

static void test_plo6_enum(void) {
  printf("Testing PLO6 Enumeration...\n");
  StdDeck_CardMask pockets[2], board, dead;
  enum_result_t result;

  /* Player 1: As Ah Ks Kh Qh Jh */
  parse_cards("AsAhKsKhQhJh", &pockets[0]);
  /* Player 2: 2c 2d 3c 3d 4c 4d */
  parse_cards("2c2d3c3d4c4d", &pockets[1]);

  /* Board: Js 9s 8s */
  parse_cards("Js9s8s", &board);

  StdDeck_CardMask_RESET(dead);

  /* Run exhaustive enumeration */
  int err = enumExhaustive(game_omaha6, pockets, board, dead, 2, 3, 0, &result);

  if (err != 0) {
    printf("enumExhaustive failed with error %d\n", err);
    exit(1);
  }

  /* 52 - 6 - 6 - 3 = 37 cards remaining.
     C(37, 2) = (37 * 36) / 2 = 666 samples. */
  printf("Samples: %d\n", result.nsamples);
  printf("Expected samples: 666\n");
  assert(result.nsamples == 666);

  printf("✓ PLO6 Enumeration Passed\n");
}

int main(void) {
  test_plo5_enum();
  test_plo6_enum();
  printf("\n✅ All PLO5/6 Enumeration tests PASSED\n");
  return 0;
}
