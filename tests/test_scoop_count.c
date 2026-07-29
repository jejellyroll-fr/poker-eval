/* Test scoop counting for Omaha Hi/Lo 8 */
#include <stdio.h>
#include <assert.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/deck/deck_std.h>
#include <string.h>

static int strToCard(const char *s) {
  char buf[3];
  strncpy(buf, s, 2);
  buf[2] = '\0';
  int c = -1;
  StdDeck_stringToCard(buf, &c);
  return c;
}

int main(void)
{
  enum_result_t res;
  enumResultClear(&res);
  StdDeck_CardMask p1, p2, board, dead;
  StdDeck_CardMask_RESET(dead);
  StdDeck_CardMask_RESET(p1);
  StdDeck_CardMask_RESET(p2);
  StdDeck_CardMask_RESET(board);
  /* Player1 A2 34 (wheel potential) */
  StdDeck_CardMask_SET(p1, strToCard("As"));
  StdDeck_CardMask_SET(p1, strToCard("2s"));
  StdDeck_CardMask_SET(p1, strToCard("3s"));
  StdDeck_CardMask_SET(p1, strToCard("4s"));
  /* Player2 high cards KQJT */
  StdDeck_CardMask_SET(p2, strToCard("Ks"));
  StdDeck_CardMask_SET(p2, strToCard("Qs"));
  StdDeck_CardMask_SET(p2, strToCard("Js"));
  StdDeck_CardMask_SET(p2, strToCard("Ts"));
  /* Board: 5h 7d 9c Th Jh */
  StdDeck_CardMask_SET(board, strToCard("5h"));
  StdDeck_CardMask_SET(board, strToCard("7d"));
  StdDeck_CardMask_SET(board, strToCard("9c"));
  StdDeck_CardMask_SET(board, strToCard("Th"));
  StdDeck_CardMask_SET(board, strToCard("Jh"));
  StdDeck_CardMask pockets[2] = {p1, p2};
  int err = enumExhaustive_dispatch(game_omaha8, pockets, board, dead, 2, 5, 0, &res);
  (void)err; /* Suppress unused variable warning */
  assert(err == 0);
  /* Expect player2 (K♠Q♠J♠T♠) to scoop: makes 9-to-K straight using K♠Q♠(hole)+9cJhTh(board).
   * Player1 (A♠2♠3♠4♠) only has Ace-high — Omaha requires exactly 2 hole cards, preventing the wheel.
   * No low qualifies on board 5h7d9cThJh (only 2 cards ≤ 8). */
  assert(res.nscoop[1] > 0);
  assert(res.nscoop[0] == 0);
  printf("Scoop test passed.\n");
  return 0;
}
