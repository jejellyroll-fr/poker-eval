#include <stdio.h>
#include <stdint.h>
#include <poker_eval/deck/deck_std.h>

int main(void)
{
    StdDeck_CardMask mask;
    mask.cards_n = 0x180108000010408ULL;

    printf("Original cards_n: 0x%llx\n", (unsigned long long)mask.cards_n);
    printf("Spades: 0x%x\n", mask.cards.spades);
    printf("Clubs: 0x%x\n", mask.cards.clubs);
    printf("Diamonds: 0x%x\n", mask.cards.diamonds);
    printf("Hearts: 0x%x\n", mask.cards.hearts);

    // Reconstruct
    StdDeck_CardMask reconstructed;
    reconstructed.cards_n = 0;
    reconstructed.cards.spades = mask.cards.spades;
    reconstructed.cards.clubs = mask.cards.clubs;
    reconstructed.cards.diamonds = mask.cards.diamonds;
    reconstructed.cards.hearts = mask.cards.hearts;

    printf("Reconstructed cards_n: 0x%llx\n", (unsigned long long)reconstructed.cards_n);

    // Check bit positions
    printf("\nBit layout analysis:\n");
    printf("Spades at bits 0-12 (with padding at 13-15)\n");
    printf("Clubs at bits 16-28 (with padding at 29-31)\n");
    printf("Diamonds at bits 32-44 (with padding at 45-47)\n");
    printf("Hearts at bits 48-60 (with padding at 61-63)\n");

    return 0;
}
