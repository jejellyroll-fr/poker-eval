#include <poker_eval/games/mixed_game.h>
#include <poker_eval/engine/game_engine.h>
#include <stdio.h>
#include <string.h>

static const char *game_name(game_type_t game)
{
    static const char *const names[] = {
        "Hold'em",
        "Omaha",
        "Omaha Hi/Lo",
        "PLO5",
        "PLO6",
        "Seven Card Stud",
        "Stud Hi/Lo",
        "Razz",
        "Five Card Draw",
        "Deuce to Seven",
        "Badugi",
        "Open Face Chinese",
        "Mixed HORSE"
    };
    if (game >= 0 && game < (int)(sizeof(names) / sizeof(names[0])))
        return names[game];
    return "Unknown";
}

static void demo_horse_rotation(void)
{
    mixed_variant_t horse[] = {
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 1},
        {.game = GAME_OMAHA_HILO, .hands_per_round = 1},
        {.game = GAME_RAZZ, .hands_per_round = 1},
        {.game = GAME_SEVEN_STUD, .hands_per_round = 1},
        {.game = GAME_SEVEN_STUD_HILO, .hands_per_round = 1},
    };

    mixed_game_t *mixed = mixed_game_create(horse, (int)(sizeof(horse) / sizeof(horse[0])),
                                            MIXED_ROTATION_SEQUENTIAL);
    if (!mixed)
    {
        printf("Failed to create HORSE rotation\n");
        return;
    }

    printf("HORSE sequential rotation (5 hands):\n");
    for (int hand = 0; hand < 5; hand++)
    {
        const mixed_variant_t *variant = mixed_game_current(mixed);
        printf("  Hand %d -> %s\n", hand + 1, game_name(variant->game));
        mixed_game_advance(mixed, NULL, -1);
    }

    mixed_game_destroy(mixed);
}

static void demo_8game_rotation(void)
{
    /* 8-game rotation:
     * 1. 2-7 Triple Draw (Limit)
     * 2. Hold'em (Limit)
     * 3. Omaha Hi/Lo (Limit)
     * 4. Razz
     * 5. Seven Card Stud
     * 6. Stud Hi/Lo
     * 7. No Limit Hold'em
     * 8. Pot Limit Omaha
     */
    mixed_variant_t eight_game[] = {
        {.game = GAME_DEUCE_TO_SEVEN, .hands_per_round = 6},    /* 2-7 Triple Draw */
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 6},      /* Limit Hold'em */
        {.game = GAME_OMAHA_HILO, .hands_per_round = 6},        /* Omaha Hi/Lo */
        {.game = GAME_RAZZ, .hands_per_round = 6},              /* Razz */
        {.game = GAME_SEVEN_STUD, .hands_per_round = 6},        /* Seven Card Stud */
        {.game = GAME_SEVEN_STUD_HILO, .hands_per_round = 6},   /* Stud Hi/Lo */
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 6},      /* No Limit Hold'em (same type, different rules) */
        {.game = GAME_OMAHA, .hands_per_round = 6},             /* Pot Limit Omaha */
    };

    mixed_game_t *mixed = mixed_game_create(eight_game, (int)(sizeof(eight_game) / sizeof(eight_game[0])),
                                            MIXED_ROTATION_SEQUENTIAL);
    if (!mixed)
    {
        printf("Failed to create 8-game rotation\n");
        return;
    }

    printf("8-Game sequential rotation (8 orbits, showing first hand of each):\n");
    int hands_in_current = 0;
    int orbit = 0;
    
    for (int hand = 0; hand < 48 && orbit < 8; hand++)  /* 8 games × 6 hands = 48 */
    {
        const mixed_variant_t *variant = mixed_game_current(mixed);
        if (hands_in_current == 0)
        {
            orbit++;
            printf("  Orbit %d: %s (6 hands)\n", orbit, game_name(variant->game));
        }
        hands_in_current++;
        if (hands_in_current >= variant->hands_per_round)
        {
            hands_in_current = 0;
        }
        mixed_game_advance(mixed, NULL, -1);
    }

    mixed_game_destroy(mixed);
}

static void demo_dealer_choice(void)
{
    mixed_variant_t choices[] = {
        {.game = GAME_TEXAS_HOLDEM, .hands_per_round = 1},
        {.game = GAME_OMAHA_HILO, .hands_per_round = 1},
        {.game = GAME_RAZZ, .hands_per_round = 1},
    };

    mixed_game_t *mixed = mixed_game_create(choices, (int)(sizeof(choices) / sizeof(choices[0])),
                                            MIXED_ROTATION_DEALER_CHOICE);
    if (!mixed)
    {
        printf("Failed to create Dealer's Choice rotation\n");
        return;
    }

    printf("Dealer's choice rotation (players choose in order):\n");
    for (int hand = 0; hand < 6; hand++)
    {
        int chooser = hand % 3;
        mixed_game_set_current_variant(mixed, chooser);
        const mixed_variant_t *variant = mixed_game_current(mixed);
        printf("  Hand %d (player %d) -> %s\n", hand + 1, chooser + 1, game_name(variant->game));
        mixed_game_record_result(mixed, (HandResult[]){
                                         {.player_id = chooser, .is_winner = true, .winnings = 100}},
                                 1);
        mixed_game_advance(mixed, NULL, chooser);
    }

    mixed_game_report_t report;
    mixed_game_get_report(mixed, &report);
    printf("\nDealer's Choice stats (wins per variant):\n");
    for (int i = 0; i < report.variant_count; i++)
    {
        printf("  %-12s -> wins: %d, total winnings: %.0f\n",
               game_name(report.stats[i].game), report.stats[i].wins, report.stats[i].total_winnings);
    }

    mixed_game_destroy(mixed);
}

int main(void)
{
    printf("=== Mixed Game Demo ===\n\n");
    demo_horse_rotation();
    printf("\n");
    demo_8game_rotation();
    printf("\n");
    demo_dealer_choice();
    printf("\n=== End of Demo ===\n");
    return 0;
}
