/*
 * generate_omaha_table.c - Tool to generate Omaha Preflop Equity Table
 *
 * Copyright (C) 2025 poker-eval contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/equity/omaha_preflop.h>
#include <poker_eval/equity/batched_montecarlo.h>
#include <poker_eval/deck/deck_std.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <output_file.dat> [samples]\n", argv[0]);
        return 1;
    }
    const char* filename = argv[1];
    int samples = (argc > 2) ? atoi(argv[2]) : 1000; /* Default low for quick test */

    omaha_preflop_table_t* table = omaha_preflop_table_create();
    if (!table) { fprintf(stderr, "Alloc failed\n"); return 1; }

    printf("Generating Omaha Preflop Table (%d samples/hand)...\n", samples);
    int count = 0;
    int ranks[4];

    /* Iterate canonical hands */
    /* Ranks: A..2 */
    for (ranks[0]=12; ranks[0]>=0; ranks[0]--) {
      for (ranks[1]=ranks[0]; ranks[1]>=0; ranks[1]--) {
        for (ranks[2]=ranks[1]; ranks[2]>=0; ranks[2]--) {
          for (ranks[3]=ranks[2]; ranks[3]>=0; ranks[3]--) {

            /* Suits: s1 is implicitly 0. Iterate others. */
            for (int s2=0; s2<4; ++s2) {
              for (int s3=0; s3<4; ++s3) {
                for (int s4=0; s4<4; ++s4) {
                    int suits[4] = {0, s2, s3, s4};

                    /* Check duplicates */
                    int valid = 1;
                    for(int i=0; i<4; ++i) {
                        for(int j=i+1; j<4; ++j) {
                            if(ranks[i]==ranks[j] && suits[i]==suits[j]) {
                                valid = 0; break;
                            }
                        }
                        if(!valid) break;
                    }
                    if(!valid) continue;

                    omaha_hand_key_t key = omaha_key_from_ranks_suits(ranks, suits);

                    /* Check if already computed */
                    if (table->data[key] > 0.0001f) continue;

                    /* Calculate equity vs Random */
                    StdDeck_CardMask hand;
                    StdDeck_CardMask_RESET(hand);
                    for(int i=0; i<4; ++i) {
                        StdDeck_CardMask_SET(hand, StdDeck_MAKE_CARD(ranks[i], suits[i]));
                    }

                    /* Batched MC: Hand vs Empty (Random) */
                    /* Note: BatchedMonteCarlo_CalculateEquity uses enumSampleBatched which handles distribution */
                    /* Passing empty mask as pockets[1] means player 2 gets random cards from stub */
                    StdDeck_CardMask empty;
                    StdDeck_CardMask_RESET(empty);

                    BatchedMonteCarloResult res = BatchedMonteCarlo_CalculateEquity(
                        hand, empty, empty, empty,
                        samples, 0
                    );

                    omaha_preflop_table_set(table, key, (float)res.equity[0]);
                    count++;
                    if (count % 100 == 0) {
                        printf("\rComputed %d hands... Last equity: %.2f%%", count, res.equity[0]*100.0);
                        fflush(stdout);
                    }
                }
              }
            }
          }
        }
      }
    }
    printf("\nDone. Computed %d canonical hands.\n", count);

    if (omaha_preflop_table_save(table, filename) != 0) {
        fprintf(stderr, "Error saving to %s\n", filename);
        return 1;
    }

    printf("Saved to %s\n", filename);
    omaha_preflop_table_free(table);
    return 0;
}
