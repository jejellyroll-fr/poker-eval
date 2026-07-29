#include <poker_eval/game_engine.h>
#include <poker_eval/enumerate.h>
#include <poker_eval/enumdefs.h>

int main(void) {
  /* Simple compile-only sanity: declare a few types/symbols */
  enum_game_t g = game_holdem;
  (void)g;
  return 0;
}

