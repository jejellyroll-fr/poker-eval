/*
 * Copyright (C) 2024
 *           Poker-eval contributors
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/distributions/plo_integration.h>
#include <poker_eval/equity/RangeEquity.h>

/* Helper function to print expression evaluation results */
static void print_expression_result(const char* expression, const arp_range_t* range, int success)
{
    printf("=== Expression: %s ===\n", expression);
    
    if (success)
    {
        printf("✓ Parsing réussi\n");
        printf("  Nombre de mains: %zu\n", range->count);
        printf("  Type de jeu: %s\n", range->is_omaha ? "Omaha" : "Hold'em");
        
        if (range->is_percentage)
        {
            printf("  Pourcentage utilisé: %.2f%%\n", range->percentage_used * 100.0f);
        }
    }
    else
    {
        printf("✗ Échec du parsing\n");
    }
    
    printf("\n");
}

/* Test basic operators */
static void test_basic_operators(void)
{
    printf("=== Test des opérateurs de base ===\n\n");
    
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    const char* basic_expressions[] = {
        "AAxxds + KKxxds",           // Union simple
        "20% - AAxx",                // Soustraction simple
        "AAxxds, KKxxds, QQxxds",    // Virgule (union)
        NULL
    };
    
    for (int i = 0; basic_expressions[i] != NULL; i++)
    {
        arp_range_t range;
        int success = ARP_ParseComplexExpression(basic_expressions[i], dead_cards, game_omaha, &range);
        print_expression_result(basic_expressions[i], &range, success);
        
        if (success)
        {
            ARP_FreeRange(&range);
        }
    }
}

/* Test parentheses and precedence */
static void test_parentheses_precedence(void)
{
    printf("=== Test des parenthèses et priorité ===\n\n");
    
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    const char* precedence_expressions[] = {
        "(AAxxds + KKxxds) - QQxx",      // Parenthèses modifient la priorité
        "AAxxds + KKxxds - QQxx",        // Évaluation gauche à droite
        "!(AAxx + KKxx)",                // Négation d'une expression
        "20% - (AAxx + KKxx + QQxx)",    // Expression complexe avec parenthèses
        NULL
    };
    
    for (int i = 0; precedence_expressions[i] != NULL; i++)
    {
        arp_range_t range;
        int success = ARP_ParseComplexExpression(precedence_expressions[i], dead_cards, game_omaha, &range);
        print_expression_result(precedence_expressions[i], &range, success);
        
        if (success)
        {
            ARP_FreeRange(&range);
        }
    }
}

/* Test complex real-world scenarios */
static void test_real_world_scenarios(void)
{
    printf("=== Test de scénarios réalistes ===\n\n");
    
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    struct {
        const char* expression;
        const char* description;
    } scenarios[] = {
        {
            "AAxxds + KKxxds + QQxxds + JJxxds",
            "Range de paires premium double-suited"
        },
        {
            "20% - (AAxx + KKxx)",
            "Top 20% moins les paires d'As et de Rois"
        },
        {
            "JT98r + JT98ds + JT98ss",
            "Rundown Jack-Ten-Nine-Eight dans toutes les couleurs"
        },
        {
            "(AAxxds + KKxxds) - (AAxxr + KKxxr)",
            "Paires premium DS moins les mêmes en rainbow"
        },
        {
            "!(20% - AAxxds)",
            "Complément du top 20% moins les As double-suited"
        },
        {
            "AKQJds + AKQTds + AKJTds + AQJTds",
            "Broadway hands double-suited"
        }
    };
    
    for (int i = 0; i < 6; i++)
    {
        printf("Scénario: %s\n", scenarios[i].description);
        
        arp_range_t range;
        int success = ARP_ParseComplexExpression(scenarios[i].expression, dead_cards, game_omaha, &range);
        print_expression_result(scenarios[i].expression, &range, success);
        
        if (success)
        {
            ARP_FreeRange(&range);
        }
    }
}

/* Test validation and error handling */
static void test_validation_errors(void)
{
    printf("=== Test de validation et gestion d'erreurs ===\n\n");
    
    const char* invalid_expressions[] = {
        "AAxxds +",                      // Expression incomplète
        "+ KKxxds",                      // Commence par opérateur
        "AAxxds KKxxds",                 // Opérateur manquant
        "(AAxxds + KKxxds",              // Parenthèse manquante
        "AAxxds + KKxxds)",              // Parenthèse en trop
        "AAxxds + + KKxxds",             // Opérateurs consécutifs
        "",                              // Expression vide
        "AAxxds - - KKxxds",             // Double soustraction
        "!()",                           // Négation d'expression vide
        NULL
    };
    
    for (int i = 0; invalid_expressions[i] != NULL; i++)
    {
        char error_buffer[256];
        int valid = ARP_ValidateComplexExpression(invalid_expressions[i], error_buffer, sizeof(error_buffer));
        
        printf("Expression: '%s'\n", invalid_expressions[i]);
        printf("  Résultat: %s\n", valid ? "VALIDE (inattendu)" : "INVALIDE (attendu)");
        
        if (!valid)
        {
            printf("  Erreur: %s\n", error_buffer);
        }
        
        printf("\n");
    }
}

/* Test integration with equity calculations */
static void test_equity_integration(void)
{
    printf("=== Test d'intégration avec calculs d'équité ===\n\n");
    
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    /* Parse two complex ranges */
    arp_range_t range1, range2;
    
    const char* expr1 = "AAxxds + KKxxds";
    const char* expr2 = "JT98r + JT98ds";
    
    printf("Range 1: %s\n", expr1);
    if (ARP_ParseComplexExpression(expr1, dead_cards, game_omaha, &range1))
    {
        printf("  ✓ Parsing réussi: %zu mains\n", range1.count);
        
        printf("Range 2: %s\n", expr2);
        if (ARP_ParseComplexExpression(expr2, dead_cards, game_omaha, &range2))
        {
            printf("  ✓ Parsing réussi: %zu mains\n", range2.count);
            
            /* Convert to OmahaHandList for further processing */
            OmahaHandList list1, list2;

            if (OmahaHandList_Init(&list1, (int)range1.count) &&
                OmahaHandList_Init(&list2, (int)range2.count))
            {
                if (ARP_ToOmahaHandList(&range1, &list1) &&
                    ARP_ToOmahaHandList(&range2, &list2))
                {
                    printf("  ✓ Conversion vers OmahaHandList réussie\n");
                    printf("    Liste 1: %d mains\n", list1.count);
                    printf("    Liste 2: %d mains\n", list2.count);
                    printf("  → Prêt pour calculs d'équité\n");
                }
                
                OmahaHandList_Free(&list1);
                OmahaHandList_Free(&list2);
            }
            
            ARP_FreeRange(&range2);
        }
        else
        {
            printf("  ✗ Échec du parsing de la range 2\n");
        }
        
        ARP_FreeRange(&range1);
    }
    else
    {
        printf("  ✗ Échec du parsing de la range 1\n");
    }
    
    printf("\n");
}

/* Demonstrate operator precedence */
static void demonstrate_precedence(void)
{
    printf("=== Démonstration de la priorité des opérateurs ===\n\n");
    
    printf("Priorité des opérateurs (du plus élevé au plus faible):\n");
    printf("  1. Parenthèses ()\n");
    printf("  2. Négation (!)\n");
    printf("  3. Addition (+) et Soustraction (-) [associativité gauche]\n");
    printf("  4. Virgule (,) [priorité la plus faible]\n\n");
    
    struct {
        const char* expression;
        const char* interpretation;
    } precedence_examples[] = {
        {
            "AAxx + KKxx - QQxx",
            "((AAxx + KKxx) - QQxx)"
        },
        {
            "!AAxx + KKxx",
            "((!AAxx) + KKxx)"
        },
        {
            "AAxx, KKxx + QQxx",
            "(AAxx, (KKxx + QQxx))"
        },
        {
            "!(AAxx + KKxx)",
            "(!(AAxx + KKxx))"
        }
    };
    
    for (int i = 0; i < 4; i++)
    {
        printf("Expression: %s\n", precedence_examples[i].expression);
        printf("Interprétée comme: %s\n\n", precedence_examples[i].interpretation);
    }
}

int main(void)
{
    printf("Exemple d'utilisation des expressions complexes - Phase 2\n");
    printf("=========================================================\n\n");
    
    /* Run all test sections */
    test_basic_operators();
    test_parentheses_precedence();
    test_real_world_scenarios();
    test_validation_errors();
    test_equity_integration();
    demonstrate_precedence();
    
    printf("=== RÉSUMÉ DES FONCTIONNALITÉS PHASE 2 ===\n\n");
    
    printf("✅ Opérateurs implémentés:\n");
    printf("  + : Union de ranges\n");
    printf("  - : Soustraction de ranges\n");
    printf("  ! : Négation/complément de range\n");
    printf("  , : Virgule (union, priorité faible)\n");
    printf("  () : Parenthèses pour grouper\n\n");
    
    printf("✅ Fonctionnalités avancées:\n");
    printf("  • Parsing d'expressions complexes avec priorité\n");
    printf("  • Gestion des parenthèses imbriquées\n");
    printf("  • Validation complète avec messages d'erreur\n");
    printf("  • Intégration avec l'écosystème PLO existant\n");
    printf("  • Support des patterns PLO et pourcentages\n\n");
    
    printf("✅ API Phase 2:\n");
    printf("  • ARP_ParseComplexExpression()\n");
    printf("  • ARP_ValidateComplexExpression()\n");
    printf("  • ARP_EvaluateExpressionTree()\n");
    printf("  • ARP_FreeExpressionTree()\n\n");
    
    printf("🚀 Prêt pour Phase 3:\n");
    printf("  • Optimisation des performances\n");
    printf("  • Rankings complets des mains Omaha\n");
    printf("  • Cache intelligent des expressions\n");
    printf("  • API avancée de manipulation\n\n");
    
    printf("Exemple terminé avec succès!\n");

    return 0;
}
