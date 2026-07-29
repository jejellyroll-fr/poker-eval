/*
 * benchmark_plo_preflop.c - Performance benchmark for PLO4/5/6 preflop equity
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poker_eval/core/time_compat.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/enumerate.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>

/* Function prototypes */
static void parse_cards(const char *str, StdDeck_CardMask *mask);
static double get_time_sec(void);
static void run_benchmark(const char *name, enum_game_t game,
                          const char *p1_str, const char *p2_str,
                          int use_sample);

/* Helper to parse cards */
static void parse_cards(const char *str, StdDeck_CardMask *mask) {
  StdDeck_CardMask_RESET(*mask);
  char *copy = strdup(str);
  char *token = strtok(copy, " ");
  while (token) {
    char buf[8];
    int card;
    strcpy(buf, token); /* StdDeck_stringToCard modifies the buffer */
    if (StdDeck_stringToCard(buf, &card) > 0) { /* Returns chars consumed */
      StdDeck_CardMask_SET(*mask, card);
    }
    token = strtok(NULL, " ");
  }
  free(copy);
}

static double get_time_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void run_benchmark(const char *name, enum_game_t game,
                          const char *p1_str, const char *p2_str,
                          int use_sample) {
  StdDeck_CardMask p1, p2, board, dead;
  StdDeck_CardMask pockets[2];
  enum_result_t result;

  parse_cards(p1_str, &p1);
  parse_cards(p2_str, &p2);
  StdDeck_CardMask_RESET(board);
  StdDeck_CardMask_RESET(dead);

  pockets[0] = p1;
  pockets[1] = p2;

  printf("Benchmarking %s (%s)...\n", name,
         use_sample ? "Oracle/MonteCarlo" : "Exact/OpenMP");

  double start = get_time_sec();
  int ret;

  if (use_sample) {
    /* Oracle Mode: 100,000 samples */
    ret = enumSample(game, pockets, board, dead, 2, 0, 100000, 0, &result);
  } else {
    /* Exact Mode: Exhaustive */
    ret = enumExhaustive(game, pockets, board, dead, 2, 0, 0, &result);
  }

  double end = get_time_sec();
  double duration = end - start;

  if (ret != 0) {
    printf("Error in enumeration: %d\n", ret);
    return;
  }

  double total_samples = (double)result.nsamples;
  double speed = total_samples / duration / 1000000.0;

  printf("  Time: %.4f seconds\n", duration);
  printf("  Samples: %.0f\n", total_samples);
  printf("  Speed: %.2f M/s\n", speed);
  printf("  Equity P1: %.2f%%\n", (result.ev[0] / total_samples) * 100.0);
  printf("  Equity P2: %.2f%%\n", (result.ev[1] / total_samples) * 100.0);
  printf("----------------------------------------\n");
}

int main(int argc, char **argv) {
  (void)argc; /* Unused */
  (void)argv; /* Unused */

  printf("=== PLO Preflop Performance Benchmark ===\n");

  /* PLO4: As Ks Ah Kh vs Qd Jd Tc 9c */
  run_benchmark("PLO4", game_omaha, "As Ks Ah Kh", "Qd Jd Tc 9c", 0);
  run_benchmark("PLO4", game_omaha, "As Ks Ah Kh", "Qd Jd Tc 9c", 1);

  /* PLO5: As Ks Ah Kh Qh vs Qd Jd Tc 9c 8c */
  run_benchmark("PLO5", game_omaha5, "As Ks Ah Kh Qh", "Qd Jd Tc 9c 8c", 0);
  run_benchmark("PLO5", game_omaha5, "As Ks Ah Kh Qh", "Qd Jd Tc 9c 8c", 1);

  /* PLO6: As Ks Ah Kh Qh Jh vs Qd Jd Tc 9c 8c 7c */
  run_benchmark("PLO6", game_omaha6, "As Ks Ah Kh Qh Jh", "Qd Jd Tc 9c 8c 7c",
                0);
  run_benchmark("PLO6", game_omaha6, "As Ks Ah Kh Qh Jh", "Qd Jd Tc 9c 8c 7c",
                1);

  return 0;
}
