/*
 * Démonstration des fonctionnalités avancées du StudRangeParser
 *
 * Ce programme montre les nouvelles capacités avancées :
 * - Combinaison de ranges avec opérateurs
 * - Expansion complète des contraintes de couleur
 * - Système de ranking amélioré pour les pourcentages
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/range/StudRangeParser.h>

static void demo_range_combinations(void)
{
    printf("=== Démonstration des combinaisons de ranges ===\n\n");

    srp_range_t premium_pairs, trips, combined, final_range;

    // Crée une range de paires premium
    printf("1. Création d'une range de paires premium (AA-QQ):\n");
    SRP_ParseRange("AA-QQ", SRP_VARIANT_STUD, NULL, &premium_pairs);
    char buffer1[256];
    SRP_RangeToString(&premium_pairs, buffer1, sizeof(buffer1));
    printf("   Premium pairs: %s\n", buffer1);

    // Crée une range de trips
    printf("\n2. Création d'une range de trips (AAA, KKK):\n");
    SRP_ParseRange("AAA, KKK", SRP_VARIANT_STUD, NULL, &trips);
    char buffer2[256];
    SRP_RangeToString(&trips, buffer2, sizeof(buffer2));
    printf("   Trips: %s\n", buffer2);

    // Union des deux ranges
    printf("\n3. Union des deux ranges:\n");
    SRP_CombineRanges(&premium_pairs, &trips, SRP_OP_UNION, &combined);
    char buffer3[256];
    SRP_RangeToString(&combined, buffer3, sizeof(buffer3));
    printf("   Combined: %s\n", buffer3);
    printf("   Total hands: %zu\n", combined.count);

    // Soustraction d'une main spécifique
    printf("\n4. Soustraction de KK de la range combinée:\n");
    srp_range_t to_remove;
    SRP_ParseRange("KK", SRP_VARIANT_STUD, NULL, &to_remove);
    SRP_CombineRanges(&combined, &to_remove, SRP_OP_SUBTRACT, &final_range);
    char buffer4[256];
    SRP_RangeToString(&final_range, buffer4, sizeof(buffer4));
    printf("   Final range (without KK): %s\n", buffer4);
    printf("   Final count: %zu\n", final_range.count);

    // Nettoyage
    SRP_FreeRange(&premium_pairs);
    SRP_FreeRange(&trips);
    SRP_FreeRange(&combined);
    SRP_FreeRange(&to_remove);
    SRP_FreeRange(&final_range);

    printf("\n");
}

static void demo_color_expansion(void)
{
    printf("=== Démonstration de l'expansion des couleurs ===\n\n");

    // Expansion monotone pour une paire
    printf("1. Expansion monotone pour AA:\n");
    srp_range_t monotone_result;
    monotone_result.hands = NULL;
    monotone_result.count = 0;
    monotone_result.capacity = 0;

    int aa_ranks[2] = {12, 12}; // AA
    SRP_ExpandHandWithColor(aa_ranks, 2, SRP_COLOR_MONOTONE, NULL, &monotone_result);
    printf("   AA monotone generates %zu specific hands\n", monotone_result.count);
    printf("   (AAhh, AAcc, AAdd, AAss)\n");

    // Expansion rainbow pour une main à 3 cartes
    printf("\n2. Expansion rainbow pour AKQ:\n");
    srp_range_t rainbow_result;
    rainbow_result.hands = NULL;
    rainbow_result.count = 0;
    rainbow_result.capacity = 0;

    int akq_ranks[3] = {12, 11, 10}; // AKQ
    SRP_ExpandHandWithColor(akq_ranks, 3, SRP_COLOR_RAINBOW, NULL, &rainbow_result);
    printf("   AKQ rainbow generates %zu specific hands\n", rainbow_result.count);
    printf("   (All combinations with 3 different suits)\n");

    // Expansion bicolor pour une main à 3 cartes
    printf("\n3. Expansion bicolor pour A23:\n");
    srp_range_t bicolor_result;
    bicolor_result.hands = NULL;
    bicolor_result.count = 0;
    bicolor_result.capacity = 0;

    int a23_ranks[3] = {12, 0, 1}; // A23
    SRP_ExpandHandWithColor(a23_ranks, 3, SRP_COLOR_BICOLOR, NULL, &bicolor_result);
    printf("   A23 bicolor generates %zu specific hands\n", bicolor_result.count);
    printf("   (2 cards of one suit, 1 of another)\n");

    // Nettoyage
    SRP_FreeRange(&monotone_result);
    SRP_FreeRange(&rainbow_result);
    SRP_FreeRange(&bicolor_result);

    printf("\n");
}

static void demo_advanced_percentages(void)
{
    printf("=== Démonstration du système de pourcentages amélioré ===\n\n");

    float percentages[] = {0.02f, 0.05f, 0.10f, 0.20f};
    const char *labels[] = {"Top 2%", "Top 5%", "Top 10%", "Top 20%"};

    for (size_t i = 0; i < 4; i++)
    {
        printf("%s des mains de stud:\n", labels[i]);

        srp_range_t result;
        SRP_GetTopPercentage(percentages[i], SRP_VARIANT_STUD, NULL, &result);

        printf("   Nombre de mains: %zu\n", result.count);

        // Analyse de la composition
        int pair_count = 0, trips_count = 0, other_count = 0;
        for (size_t j = 0; j < result.count; j++)
        {
            if (result.hands[j].card_count == 2)
            {
                pair_count++;
            }
            else if (result.hands[j].card_count == 3 &&
                     result.hands[j].cards[0] == result.hands[j].cards[1] &&
                     result.hands[j].cards[1] == result.hands[j].cards[2])
            {
                trips_count++;
            }
            else
            {
                other_count++;
            }
        }

        printf("   Composition: %d paires, %d trips, %d autres\n",
               pair_count, trips_count, other_count);

        // Affiche quelques exemples
        printf("   Exemples: ");
        for (size_t j = 0; j < result.count && j < 5; j++)
        {
            const srp_hand_t *hand = &result.hands[j];
            for (int k = 0; k < hand->card_count; k++)
            {
                char rank_char;
                switch (hand->cards[k])
                {
                case 12:
                    rank_char = 'A';
                    break;
                case 11:
                    rank_char = 'K';
                    break;
                case 10:
                    rank_char = 'Q';
                    break;
                case 9:
                    rank_char = 'J';
                    break;
                case 8:
                    rank_char = 'T';
                    break;
                default:
                    rank_char = (char)('2' + hand->cards[k]);
                    break;
                }
                printf("%c", rank_char);
            }
            if (j < result.count - 1 && j < 4)
                printf(", ");
        }
        if (result.count > 5)
            printf("...");
        printf("\n\n");

        SRP_FreeRange(&result);
    }
}

static void demo_complex_scenarios(void)
{
    printf("=== Démonstration de scénarios complexes ===\n\n");

    // Scénario 1: Range de tournoi tight
    printf("1. Range de tournoi tight (early position):\n");
    srp_range_t premium, strong_trips, tournament_range;

    SRP_ParseRange("AA-JJ", SRP_VARIANT_STUD, NULL, &premium);
    SRP_ParseRange("AAA-QQQ", SRP_VARIANT_STUD, NULL, &strong_trips);
    SRP_CombineRanges(&premium, &strong_trips, SRP_OP_UNION, &tournament_range);

    char buffer1[256];
    SRP_RangeToString(&tournament_range, buffer1, sizeof(buffer1));
    printf("   Tournament range: %s\n", buffer1);
    printf("   Total hands: %zu\n", tournament_range.count);

    // Scénario 2: Range agressive moins les nuts
    printf("\n2. Range agressive moins les nuts:\n");
    srp_range_t aggressive, nuts, adjusted_range;

    SRP_ParseRange("AA-99, AAA-999, A23xyz, KQJxxy", SRP_VARIANT_STUD, NULL, &aggressive);
    SRP_ParseRange("AAA, AA", SRP_VARIANT_STUD, NULL, &nuts);
    SRP_CombineRanges(&aggressive, &nuts, SRP_OP_SUBTRACT, &adjusted_range);

    char buffer2[512];
    SRP_RangeToString(&adjusted_range, buffer2, sizeof(buffer2));
    printf("   Adjusted range: %s\n", buffer2);
    printf("   Total hands: %zu\n", adjusted_range.count);

    // Scénario 3: Combinaison de pourcentages et de mains spécifiques
    printf("\n3. Top 15%% plus quelques mains spéciales:\n");
    srp_range_t top_percent, specials, final_combo;

    SRP_GetTopPercentage(0.15f, SRP_VARIANT_STUD, NULL, &top_percent);
    SRP_ParseRange("A23xxx, KQJxyz", SRP_VARIANT_STUD, NULL, &specials);
    SRP_CombineRanges(&top_percent, &specials, SRP_OP_UNION, &final_combo);

    printf("   Top 15%% had %zu hands\n", top_percent.count);
    printf("   Added %zu special hands\n", specials.count);
    printf("   Final combination: %zu hands\n", final_combo.count);

    // Nettoyage
    SRP_FreeRange(&premium);
    SRP_FreeRange(&strong_trips);
    SRP_FreeRange(&tournament_range);
    SRP_FreeRange(&aggressive);
    SRP_FreeRange(&nuts);
    SRP_FreeRange(&adjusted_range);
    SRP_FreeRange(&top_percent);
    SRP_FreeRange(&specials);
    SRP_FreeRange(&final_combo);

    printf("\n");
}

static void demo_performance_comparison(void)
{
    printf("=== Démonstration de performance ===\n\n");

    printf("Comparaison des méthodes de création de ranges:\n\n");

    // Méthode 1: Parsing direct
    printf("1. Parsing direct d'une range complexe:\n");
    srp_range_t direct_range;
    SRP_ParseRange("AA-TT, AAA-TTT, A23xyz, KQJxxy, T98xxx", SRP_VARIANT_STUD, NULL, &direct_range);
    printf("   Range directe: %zu mains\n", direct_range.count);

    // Méthode 2: Combinaison de sous-ranges
    printf("\n2. Combinaison de sous-ranges:\n");
    srp_range_t pairs, trips, hands, temp1, combined_range;

    SRP_ParseRange("AA-TT", SRP_VARIANT_STUD, NULL, &pairs);
    SRP_ParseRange("AAA-TTT", SRP_VARIANT_STUD, NULL, &trips);
    SRP_ParseRange("A23xyz, KQJxxy, T98xxx", SRP_VARIANT_STUD, NULL, &hands);

    SRP_CombineRanges(&pairs, &trips, SRP_OP_UNION, &temp1);
    SRP_CombineRanges(&temp1, &hands, SRP_OP_UNION, &combined_range);

    printf("   Paires: %zu mains\n", pairs.count);
    printf("   Trips: %zu mains\n", trips.count);
    printf("   Mains spéciales: %zu mains\n", hands.count);
    printf("   Range combinée: %zu mains\n", combined_range.count);

    // Vérification de l'équivalence
    printf("\n3. Vérification de l'équivalence:\n");
    printf("   Méthode directe: %zu mains\n", direct_range.count);
    printf("   Méthode combinée: %zu mains\n", combined_range.count);
    printf("   %s\n", (direct_range.count == combined_range.count) ? "✓ Les deux méthodes donnent le même résultat" : "⚠ Différence détectée");

    // Nettoyage
    SRP_FreeRange(&direct_range);
    SRP_FreeRange(&pairs);
    SRP_FreeRange(&trips);
    SRP_FreeRange(&hands);
    SRP_FreeRange(&temp1);
    SRP_FreeRange(&combined_range);

    printf("\n");
}

int main(void)
{
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           StudRangeParser - Fonctionnalités Avancées        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    demo_range_combinations();
    demo_color_expansion();
    demo_advanced_percentages();
    demo_complex_scenarios();
    demo_performance_comparison();

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    Démonstration terminée                   ║\n");
    printf("║                                                              ║\n");
    printf("║  Le StudRangeParser supporte maintenant toutes les          ║\n");
    printf("║  fonctionnalités avancées pour une utilisation              ║\n");
    printf("║  professionnelle en analyse de poker stud.                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    return 0;
}
