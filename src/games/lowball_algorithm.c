#include <poker_eval/deck/deck_std.h>
#include <string.h>
#include <assert.h>
#include <poker_eval/games/lowball_algorithm.h>

void count_ranks(const StdDeck_CardMask *hand, int rank_counts[StdDeck_Rank_COUNT]) {
    memset(rank_counts, 0, sizeof(int) * StdDeck_Rank_COUNT);
    for (int r = 0; r < StdDeck_Rank_COUNT; ++r) {
        for (int s = 0; s < StdDeck_Suit_COUNT; ++s) {
            int card = StdDeck_MAKE_CARD(r, s);
            if (StdDeck_CardMask_CARD_IS_SET(*hand, card)) {
                rank_counts[r]++;
            }
        }
    }
}

int select_no_pair_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[5]) {
    int found = 0;
    // Un rang en double n'interdit pas une main sans paire : on ne retient qu'une
    // carte par rang et on ignore les exemplaires surnumeraires. Une main de 7
    // cartes 4-5-5-6-6-7-8 vaut bien 8-7-6-5-4, pas une paire.
    for (int r = 0; r < StdDeck_Rank_COUNT; ++r) {
        if (rank_counts[r] > 0) {
            out_ranks[found++] = r;
            if (found == 5) return 1;
        }
    }
    return 0;
}

int select_one_pair_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[4]) {
    for (int pair = 0; pair < StdDeck_Rank_COUNT; ++pair) {
        if (rank_counts[pair] >= 2) {
            int kickers[3];
            int found = 0;
            for (int r = 0; r < StdDeck_Rank_COUNT; ++r) {
                if (r != pair && rank_counts[r] > 0) {
                    kickers[found++] = r;
                    if (found == 3) break;
                }
            }
            if (found == 3) {
                out_ranks[0] = pair;
                out_ranks[1] = kickers[0];
                out_ranks[2] = kickers[1];
                out_ranks[3] = kickers[2];
                return 1;
            }
        }
    }
    return 0;
}

int select_two_pair_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[3]) {
    int pairs[2];
    int found_pairs = 0;
    for (int r = 0; r < StdDeck_Rank_COUNT; ++r) {
        if (rank_counts[r] >= 2) {
            pairs[found_pairs++] = r;
            if (found_pairs == 2) break;
        }
    }
    if (found_pairs < 2) return 0;
    for (int r = 0; r < StdDeck_Rank_COUNT; ++r) {
        if (r != pairs[0] && r != pairs[1] && rank_counts[r] > 0) {
            out_ranks[0] = pairs[0];
            out_ranks[1] = pairs[1];
            out_ranks[2] = r;
            return 1;
        }
    }
    return 0;
}

int select_trips_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[3]) {
    for (int trips = 0; trips < StdDeck_Rank_COUNT; ++trips) {
        if (rank_counts[trips] >= 3) {
            int kickers[2];
            int found = 0;
            for (int r = 0; r < StdDeck_Rank_COUNT; ++r) {
                if (r != trips && rank_counts[r] > 0) {
                    kickers[found++] = r;
                    if (found == 2) break;
                }
            }
            if (found == 2) {
                out_ranks[0] = trips;
                out_ranks[1] = kickers[0];
                out_ranks[2] = kickers[1];
                return 1;
            }
        }
    }
    return 0;
}

int select_full_house_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[2]) {
    for (int trips = 0; trips < StdDeck_Rank_COUNT; ++trips) {
        if (rank_counts[trips] >= 3) {
            for (int pair = 0; pair < StdDeck_Rank_COUNT; ++pair) {
                if (pair != trips && rank_counts[pair] >= 2) {
                    out_ranks[0] = trips;
                    out_ranks[1] = pair;
                    return 1;
                }
            }
        }
    }
    return 0;
}

int select_quads_ranks(const int rank_counts[StdDeck_Rank_COUNT], int out_ranks[4]) {
    for (int quads = 0; quads < StdDeck_Rank_COUNT; ++quads) {
        if (rank_counts[quads] >= 4) {
            int kickers[3];
            int found = 0;
            for (int r = 0; r < StdDeck_Rank_COUNT; ++r) {
                if (r != quads && rank_counts[r] > 0) {
                    kickers[found++] = r;
                    if (found == 3) break;
                }
            }
            if (found == 3) {
                out_ranks[0] = quads;
                out_ranks[1] = kickers[0];
                out_ranks[2] = kickers[1];
                out_ranks[3] = kickers[2];
                return 1;
            }
        }
    }
    return 0;
}
