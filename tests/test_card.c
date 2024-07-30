#include <stdio.h>
#include "Card.h"

int main() {
    Card card;
    Card_init(&card);

    char rank = RankToChar(12); // Should return 'A'
    char suit = SuitToChar(0); // Should return 'h'

    printf("Rank 12 is %c\n", rank);
    printf("Suit 0 is %c\n", suit);

    int rank_val = CharToRank('A'); // Should return 12
    int suit_val = CharToSuit('h'); // Should return 0

    printf("Char 'A' is rank %d\n", rank_val);
    printf("Char 'h' is suit %d\n", suit_val);

    Card_destroy(&card);
    return 0;
}
