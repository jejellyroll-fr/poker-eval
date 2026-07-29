/*
 * Démonstration du StudRangeParser
 * 
 * Ce programme montre comment utiliser le parser de ranges pour le stud
 * avec différents types de syntaxes et fonctionnalités.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/range/StudRangeParser.h>

static void demo_basic_parsing(void) {
    printf("=== Démonstration du parsing de base ===\n\n");
    
    const char* test_ranges[] = {
        "AA, KK, QQ",                    // Paires spécifiques
        "AAA, KKK",                      // Trips
        "A23, KQJ, T98",                 // Mains à 3 cartes
        "AA-TT",                         // Intervalle de paires
        "AAA-KKK",                       // Intervalle de trips
        "A23xxx, KQJxxy, T98xyz",        // Contraintes de couleur
        "20%",                           // Pourcentage
        "AA-TT, AAA, A23xyz"             // Range complexe
    };
    
    for (size_t i = 0; i < sizeof(test_ranges) / sizeof(test_ranges[0]); i++) {
        printf("Range: '%s'\n", test_ranges[i]);
        
        srp_range_t range;
        int result = SRP_ParseRange(test_ranges[i], SRP_VARIANT_STUD, NULL, &range);
        
        if (result) {
            printf("  ✓ Parsing réussi - %zu main(s) trouvée(s)\n", range.count);
            
            // Affiche la représentation string
            char buffer[512];
            SRP_RangeToString(&range, buffer, sizeof(buffer));
            printf("  → Résultat: %s\n", buffer);
            
            SRP_FreeRange(&range);
        } else {
            printf("  ✗ Échec du parsing\n");
        }
        printf("\n");
    }
}

static void demo_color_constraints(void) {
    printf("=== Démonstration des contraintes de couleur ===\n\n");
    
    const char* color_examples[] = {
        "A23xxx",      // Monotone (même couleur)
        "A23xxy",      // Bicolor (deux couleurs)
        "A23xyz",      // Rainbow (trois couleurs)
        "AAAhhh",      // Trips en cœur
        "AAhh",        // Paire en cœur
        "KQJhcd"       // Rainbow avec couleurs spécifiques
    };
    
    for (size_t i = 0; i < sizeof(color_examples) / sizeof(color_examples[0]); i++) {
        printf("Range avec couleur: '%s'\n", color_examples[i]);
        
        srp_range_t range;
        int result = SRP_ParseRange(color_examples[i], SRP_VARIANT_STUD, NULL, &range);
        
        if (result && range.count > 0) {
            printf("  ✓ Parsing réussi\n");
            printf("  → Contrainte de couleur: ");
            
            switch (range.hands[0].color_constraint) {
                case SRP_COLOR_NONE:
                    printf("Aucune\n");
                    break;
                case SRP_COLOR_MONOTONE:
                    printf("Monotone (xxx)\n");
                    break;
                case SRP_COLOR_BICOLOR:
                    printf("Bicolor (xxy)\n");
                    break;
                case SRP_COLOR_RAINBOW:
                    printf("Rainbow (xyz)\n");
                    break;
                default:
                    printf("Unknown\n");
                    break;
            }
            
            SRP_FreeRange(&range);
        } else {
            printf("  ✗ Échec du parsing\n");
        }
        printf("\n");
    }
}

static void demo_percentages(void) {
    printf("=== Démonstration des pourcentages ===\n\n");
    
    float percentages[] = {0.05f, 0.10f, 0.20f, 0.50f};
    
    for (size_t i = 0; i < sizeof(percentages) / sizeof(percentages[0]); i++) {
        printf("Top %.0f%% des mains:\n", percentages[i] * 100);
        
        srp_range_t range;
        int result = SRP_GetTopPercentage(percentages[i], SRP_VARIANT_STUD, NULL, &range);
        
        if (result) {
            printf("  ✓ %zu main(s) sélectionnée(s)\n", range.count);
            
            // Affiche quelques exemples
            printf("  → Exemples: ");
            for (size_t j = 0; j < range.count && j < 5; j++) {
                const srp_hand_t *hand = &range.hands[j];
                for (int k = 0; k < hand->card_count; k++) {
                    char rank_char;
                    switch (hand->cards[k]) {
                        case 12: rank_char = 'A'; break;
                        case 11: rank_char = 'K'; break;
                        case 10: rank_char = 'Q'; break;
                        case 9: rank_char = 'J'; break;
                        case 8: rank_char = 'T'; break;
                        default: rank_char = (char)('0' + hand->cards[k]); break;
                    }
                    printf("%c", rank_char);
                }
                if (j < range.count - 1 && j < 4) printf(", ");
            }
            if (range.count > 5) printf("...");
            printf("\n");
            
            SRP_FreeRange(&range);
        } else {
            printf("  ✗ Échec de la génération\n");
        }
        printf("\n");
    }
}

static void demo_validation(void) {
    printf("=== Démonstration de la validation ===\n\n");
    
    const char* test_strings[] = {
        "AA, KK",           // Valide
        "A23xyz",           // Valide
        "AA-TT",            // Valide
        "20%",              // Valide
        "",                 // Invalide (vide)
        "XYZ",              // Invalide (rangs inexistants)
        "A2",               // Valide (paire incomplète mais acceptable)
        "AAAA"              // Invalide (trop de cartes)
    };
    
    for (size_t i = 0; i < sizeof(test_strings) / sizeof(test_strings[0]); i++) {
        printf("Validation de: '%s'\n", test_strings[i]);
        
        char error_buffer[256];
        int is_valid = SRP_ValidateRangeString(test_strings[i], SRP_VARIANT_STUD, 
                                               error_buffer, sizeof(error_buffer));
        
        if (is_valid) {
            printf("  ✓ Syntaxe valide\n");
        } else {
            printf("  ✗ Syntaxe invalide\n");
            if (strlen(error_buffer) > 0) {
                printf("  → Erreur: %s\n", error_buffer);
            }
        }
        printf("\n");
    }
}

static void demo_context_usage(void) {
    printf("=== Démonstration de l'utilisation des contextes ===\n\n");
    
    const char* range_string = "AA-TT, AAA, A23xyz";
    
    printf("Initialisation du contexte pour: '%s'\n", range_string);
    
    srp_context_t ctx;
    int result = SRP_InitContext(&ctx, range_string, SRP_VARIANT_STUD);
    
    if (result) {
        printf("  ✓ Contexte initialisé\n");
        printf("  → Longueur de l'input: %zu\n", ctx.length);
        printf("  → Variante: %d\n", ctx.variant);
        printf("  → Position initiale: %zu\n", ctx.position);
        
        SRP_FreeContext(&ctx);
        printf("  ✓ Contexte libéré\n");
    } else {
        printf("  ✗ Échec de l'initialisation du contexte\n");
    }
    printf("\n");
}

int main(void) {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                  StudRangeParser - Démonstration            ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    demo_basic_parsing();
    demo_color_constraints();
    demo_percentages();
    demo_validation();
    demo_context_usage();
    
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                     Démonstration terminée                  ║\n");
    printf("║                                                              ║\n");
    printf("║  Le StudRangeParser supporte toutes les syntaxes de base    ║\n");
    printf("║  du stud avec contraintes de couleur et pourcentages.       ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    return 0;
}
