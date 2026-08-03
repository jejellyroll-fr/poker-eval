#ifdef POKER_EVAL_EXPERIMENTAL

#include <string.h>

#include <poker_eval/core/poker_eval_modern.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>

#include "../utils/poker_eval_modern_internal.h"

/*
 * Convert a modern API hand (which stores its cards in a deck-native mask) into
 * a StdDeck_CardMask so it can be fed to the classical enumerate layer.
 *
 * Only the standard 52-card deck supports classic equity enumeration with the
 * standard encoder; the other deck types are surfaced as unsupported.
 */
static int
modern_hand_to_std_mask(const poker_eval_hand_t *hand,
                        StdDeck_CardMask *out_mask,
                        int *out_card_count)
{
  if (hand == NULL || out_mask == NULL || out_card_count == NULL)
    return -1;

  if (hand->deck_type != POKER_DECK_STANDARD)
    return -1; /* equity is only defined for the standard deck */

  *out_mask = hand->cards.std_mask;
  *out_card_count = (int)hand->card_count;
  return 0;
}

/*
 * Determine the enum game variant that matches a given number of hole cards on
 * the standard deck. 2-cards -> hold'em, 4 -> omaha, 3 -> pineapple, etc.
 */
static enum_game_t
equity_game_for_pocket_size(int pocket_size)
{
  switch (pocket_size)
  {
  case 2:
    return game_holdem;
  case 3:
    return game_pineapple;
  case 4:
    return game_omaha;
  case 5:
    return game_omaha5;
  case 6:
    return game_omaha6;
  default:
    return (enum_game_t)-1;
  }
}

poker_eval_error_t
poker_eval_calculate_equity(poker_eval_context_t *context,
                            const poker_eval_hand_t *const *hands,
                            size_t num_hands,
                            const poker_eval_hand_t *board_cards,
                            const poker_eval_hand_t *dead_cards,
                            poker_equity_result_t *results)
{
  StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
  StdDeck_CardMask board;
  StdDeck_CardMask dead;
  StdDeck_CardMask m;
  enum_result_t eres;
  enum_game_t game;
  int pocket_size;
  int nboard;
  int i;
  int ret;

  if (context == NULL || hands == NULL || results == NULL)
    return POKER_EVAL_ERROR_NULL_POINTER;

  if (num_hands < 1 || num_hands > ENUM_MAXPLAYERS)
    return POKER_EVAL_ERROR_INVALID_ARGUMENT;

  for (i = 0; i < (int)num_hands; i++)
  {
    int cc;
    if (modern_hand_to_std_mask(hands[i], &pockets[i], &cc) != 0)
      return POKER_EVAL_ERROR_UNSUPPORTED_OPERATION;
    if (i == 0)
      pocket_size = cc;
    else if (cc != pocket_size)
      return POKER_EVAL_ERROR_INVALID_ARGUMENT;
  }

  game = equity_game_for_pocket_size(pocket_size);
  if (game == (enum_game_t)-1)
    return POKER_EVAL_ERROR_UNSUPPORTED_OPERATION;

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);
  nboard = 0;

  if (board_cards != NULL)
  {
    int cc;
    if (modern_hand_to_std_mask(board_cards, &m, &cc) != 0)
      return POKER_EVAL_ERROR_UNSUPPORTED_OPERATION;
    board = m;
    nboard = cc;
  }

  if (dead_cards != NULL)
  {
    int cc;
    if (modern_hand_to_std_mask(dead_cards, &m, &cc) != 0)
      return POKER_EVAL_ERROR_UNSUPPORTED_OPERATION;
    dead = m;
  }

  /* enumExhaustive_dispatch zeroes the result and allocates the ordering when
   * orderflag is set, so only a zeroed struct is required up front. */
  memset(&eres, 0, sizeof(eres));

  ret = enumExhaustive_dispatch(game, pockets, board, dead,
                                (int)num_hands, nboard, 1, &eres);
  if (ret != 0)
  {
    enumResultFree(&eres);
    return POKER_EVAL_ERROR_INVALID_ARGUMENT;
  }

  for (i = 0; i < (int)num_hands; i++)
  {
    double total = (double)eres.nsamples;
    if (total <= 0.0)
    {
      results[i].win_probability = 0.0;
      results[i].tie_probability = 0.0;
      results[i].lose_probability = 0.0;
      results[i].total_outcomes = 0;
    }
    else
    {
      results[i].win_probability = (double)eres.nwinhi[i] / total;
      results[i].tie_probability = (double)eres.ntiehi[i] / total;
      results[i].lose_probability = (double)eres.nlosehi[i] / total;
      results[i].total_outcomes = (uint64_t)eres.nsamples;
    }
  }

  enumResultFree(&eres);
  return POKER_EVAL_SUCCESS;
}

poker_eval_error_t
poker_eval_calculate_equity_monte_carlo(poker_eval_context_t *context,
                                        const poker_eval_hand_t *const *hands,
                                        size_t num_hands,
                                        const poker_eval_hand_t *board_cards,
                                        const poker_eval_hand_t *dead_cards,
                                        uint64_t num_iterations,
                                        poker_equity_result_t *results)
{
  StdDeck_CardMask pockets[ENUM_MAXPLAYERS];
  StdDeck_CardMask board;
  StdDeck_CardMask dead;
  StdDeck_CardMask m;
  enum_result_t eres;
  enum_game_t game;
  int pocket_size;
  int nboard;
  int i;

  if (context == NULL || hands == NULL || results == NULL)
    return POKER_EVAL_ERROR_NULL_POINTER;

  if (num_hands < 1 || num_hands > ENUM_MAXPLAYERS)
    return POKER_EVAL_ERROR_INVALID_ARGUMENT;

  if (num_iterations == 0)
    return POKER_EVAL_ERROR_INVALID_ARGUMENT;

  for (i = 0; i < (int)num_hands; i++)
  {
    int cc;
    if (modern_hand_to_std_mask(hands[i], &pockets[i], &cc) != 0)
      return POKER_EVAL_ERROR_UNSUPPORTED_OPERATION;
    if (i == 0)
      pocket_size = cc;
    else if (cc != pocket_size)
      return POKER_EVAL_ERROR_INVALID_ARGUMENT;
  }

  game = equity_game_for_pocket_size(pocket_size);
  if (game == (enum_game_t)-1)
    return POKER_EVAL_ERROR_UNSUPPORTED_OPERATION;

  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);
  nboard = 0;

  if (board_cards != NULL)
  {
    int cc;
    if (modern_hand_to_std_mask(board_cards, &m, &cc) != 0)
      return POKER_EVAL_ERROR_UNSUPPORTED_OPERATION;
    board = m;
    nboard = cc;
  }

  if (dead_cards != NULL)
  {
    int cc;
    if (modern_hand_to_std_mask(dead_cards, &m, &cc) != 0)
      return POKER_EVAL_ERROR_UNSUPPORTED_OPERATION;
    dead = m;
  }

  /* enumSample zeroes the result and allocates the ordering when orderflag is
   * set, so only a zeroed struct is required up front. */
  memset(&eres, 0, sizeof(eres));

  (void)enumSample(game, pockets, board, dead,
                   (int)num_hands, nboard, (int)num_iterations, 1, &eres);

  for (i = 0; i < (int)num_hands; i++)
  {
    double total = (double)eres.nsamples;
    if (total <= 0.0)
    {
      results[i].win_probability = 0.0;
      results[i].tie_probability = 0.0;
      results[i].lose_probability = 0.0;
      results[i].total_outcomes = 0;
    }
    else
    {
      results[i].win_probability = (double)eres.nwinhi[i] / total;
      results[i].tie_probability = (double)eres.ntiehi[i] / total;
      results[i].lose_probability = (double)eres.nlosehi[i] / total;
      results[i].total_outcomes = (uint64_t)eres.nsamples;
    }
  }

  enumResultFree(&eres);
  return POKER_EVAL_SUCCESS;
}
#endif /* POKER_EVAL_EXPERIMENTAL */
