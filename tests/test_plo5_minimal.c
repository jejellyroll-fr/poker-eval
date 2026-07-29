/*
 * test_plo5_minimal.c - Minimal test to check if eval_plo5.h compiles
 */

#include <stdio.h>
#include <poker_eval/games/eval_plo5.h>

int main(void)
{
    printf("PLO5 header compiled successfully!\n");
    printf("PLO5_TOTAL_COMBOS = %d\n", PLO5_TOTAL_COMBOS);
    return 0;
}
