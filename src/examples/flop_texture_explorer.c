/*
 * flop_texture_explorer.c - Explore different flop textures
 *
 * Generates random flops and analyzes their textures
 */

#include <poker_eval/equity/flop_equity.h>
#include <poker_eval/deck/deck_std.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Helper random */
static int rand_range(int n) {
    return rand() % n;
}

/* Generate random flop */
static StdDeck_CardMask generate_random_flop(void)
{
    StdDeck_CardMask flop;
    StdDeck_CardMask_RESET(flop);

    int cards[52];
    for(int i=0; i<52; i++) cards[i] = i;

    /* Shuffle */
    for(int i=0; i<51; i++) {
        int j = i + rand_range(52-i);
        int t = cards[i]; cards[i] = cards[j]; cards[j] = t;
    }

    /* Take first 3 */
    for(int i=0; i<3; i++) {
        StdDeck_CardMask_SET(flop, cards[i]);
    }

    return flop;
}

int main(int argc, char **argv)
{
    int n_flops = (argc > 1) ? atoi(argv[1]) : 10;

    printf("=== Flop Texture Explorer ===\n\n");
    printf("Analyzing %d random flops...\n\n", n_flops);

    srand((unsigned int)time(NULL));

    /* Count texture categories */
    int texture_counts[5] = {0};

    for (int i = 0; i < n_flops; i++) {
        StdDeck_CardMask flop = generate_random_flop();

        flop_analysis_t analysis;
        analyze_flop_texture(flop, &analysis);

        texture_counts[analysis.texture]++;

        /* Print first 10 */
        if (i < 10) {
            char texture_str[32];
            flop_texture_to_string(analysis.texture, texture_str);

            printf("%d. Texture: %s (score: %d)\n", i+1, texture_str, analysis.texture_score);
        }
    }

    printf("\n=== Distribution ===\n");
    printf("Dry: %d (%.1f%%)\n", texture_counts[0], 100.0 * texture_counts[0] / n_flops);
    printf("Wet: %d (%.1f%%)\n", texture_counts[1], 100.0 * texture_counts[1] / n_flops);
    printf("Coordinated: %d (%.1f%%)\n", texture_counts[2], 100.0 * texture_counts[2] / n_flops);
    printf("Paired: %d (%.1f%%)\n", texture_counts[3], 100.0 * texture_counts[3] / n_flops);
    printf("Trips: %d (%.1f%%)\n", texture_counts[4], 100.0 * texture_counts[4] / n_flops);

    return 0;
}
