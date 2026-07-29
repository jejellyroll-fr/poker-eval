#ifndef LOWBALL_ALGORITHM_H
#define LOWBALL_ALGORITHM_H

#include <poker_eval/deck/deck_std.h>

void count_ranks(const StdDeck_CardMask *hand, int rank_counts[StdDeck_Rank_COUNT]);
int select_no_pair_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[5]);
int select_one_pair_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[4]);
int select_two_pair_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[3]);
int select_trips_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[3]);
int select_full_house_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[2]);
int select_quads_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[4]);

#endif // LOWBALL_ALGORITHM_H
