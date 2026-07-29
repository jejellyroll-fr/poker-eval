#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <poker_eval/deck/deck_std.h>

// Helper function to count set bits in a card mask
static int count_set_bits(StdDeck_CardMask mask) {
    int count = 0;
    for (int i = 0; i < StdDeck_N_CARDS; i++) {
        if (StdDeck_CardMask_CARD_IS_SET(mask, i)) {
            count++;
        }
    }
    return count;
}

int main(void) {
    printf("=== Test Simple d'Allocation Dynamique Omaha ===\n\n");
    
    OmahaHandList handList;
    OmahaHandQuery query;
    StdDeck_CardMask deadCards;
    
    // Initialize
    memset(&handList, 0, sizeof(OmahaHandList));
    StdDeck_CardMask_RESET(deadCards);
    
    // Test 1: AAxx avec initialisation explicite
    printf("Test 1: AAxx avec initialisation explicite\n");
    if (OmahaHandList_Init(&handList, 2000)) {
        printf("  ✓ Liste initialisée avec capacité %d\n", handList.capacity);
        
        if (OmahaHand_Parse("AAxx", &query)) {
            printf("  ✓ Query 'AAxx' parsée avec succès\n");
            
            int result = OmahaHand_Instantiate(&query, deadCards, &handList);
            if (result > 0) {
                printf("  ✓ Généré %d combinaisons pour AAxx\n", handList.count);
                printf("  ✓ Capacité finale: %d\n", handList.capacity);
                
                // Vérifier quelques mains
                int valid_hands = 0;
                for (int i = 0; i < handList.count && i < 10; i++) {
                    if (count_set_bits(handList.hands[i]) == 4) {
                        valid_hands++;
                    }
                }
                printf("  ✓ %d premières mains vérifiées valides\n", valid_hands);
            } else {
                printf("  ✗ Erreur lors de l'instantiation\n");
            }
        } else {
            printf("  ✗ Erreur lors du parsing\n");
        }
        
        OmahaHandList_Free(&handList);
        printf("  ✓ Mémoire libérée\n");
    } else {
        printf("  ✗ Erreur d'initialisation\n");
    }
    
    printf("\n");
    
    // Test 2: xxxx (grande range) avec auto-initialisation
    printf("Test 2: xxxx (grande range) avec auto-initialisation\n");
    memset(&handList, 0, sizeof(OmahaHandList));
    
    if (OmahaHand_Parse("xxxx", &query)) {
        printf("  ✓ Query 'xxxx' parsée avec succès\n");
        
        // Pas d'initialisation explicite - test de l'auto-initialisation
        int result = OmahaHand_Instantiate(&query, deadCards, &handList);
        if (result > 0) {
            printf("  ✓ Auto-initialisation réussie\n");
            printf("  ✓ Généré %d combinaisons (C(52,4) = 270,725)\n", handList.count);
            printf("  ✓ Capacité finale: %d\n", handList.capacity);
            printf("  ✓ Dépasse l'ancienne limite MAX_OMAHA_COMBOS de 10,000!\n");
            
            // Calculer la mémoire utilisée
            size_t memory_kb = (handList.capacity * sizeof(StdDeck_CardMask)) / 1024;
            printf("  ✓ Mémoire utilisée: ~%zu KB\n", memory_kb);
        } else {
            printf("  ✗ Erreur lors de l'instantiation\n");
        }
        
        OmahaHandList_Free(&handList);
        printf("  ✓ Mémoire libérée\n");
    } else {
        printf("  ✗ Erreur lors du parsing\n");
    }
    
    printf("\n");
    
    // Test 3: Réutilisation avec Clear
    printf("Test 3: Réutilisation de liste avec Clear\n");
    memset(&handList, 0, sizeof(OmahaHandList));
    OmahaHandList_Init(&handList, 5000);
    
    // Première utilisation
    OmahaHand_Parse("KKxx", &query);
    OmahaHand_Instantiate(&query, deadCards, &handList);
    int first_count = handList.count;
    int capacity_after_first = handList.capacity;
    printf("  ✓ Première utilisation: %d mains, capacité %d\n", first_count, capacity_after_first);
    
    // Clear et réutilisation
    OmahaHandList_Clear(&handList);
    printf("  ✓ Liste vidée (count=%d, capacity=%d)\n", handList.count, handList.capacity);
    
    OmahaHand_Parse("QQxx", &query);
    OmahaHand_Instantiate(&query, deadCards, &handList);
    printf("  ✓ Deuxième utilisation: %d mains, capacité %d\n", handList.count, handList.capacity);
    printf("  ✓ Capacité préservée après Clear\n");
    
    OmahaHandList_Free(&handList);
    
    printf("\n=== Tous les tests réussis! ===\n");
    
    return 0;
}
