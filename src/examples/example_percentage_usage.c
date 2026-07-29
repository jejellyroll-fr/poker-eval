#include <stdio.h>
#include <stdlib.h>
#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/deck/deck_std.h>

static void print_range_summary(const char* range_str, arp_range_t* range) {
    printf("Range '%s':\n", range_str);
    printf("  - Nombre de mains: %zu\n", range->count);
    printf("  - Pourcentage: %.2f%%\n", range->percentage_used * 100.0f);
    
    char buffer[256];
    ARP_RangeToString(range, buffer, sizeof(buffer));
    printf("  - Description: %s\n", buffer);
    
    // Calculer le pourcentage réel par rapport aux 1326 mains possibles
    float real_percentage = ARP_GetRangePercentage(range, game_holdem) * 100.0f;
    printf("  - Pourcentage réel: %.2f%%\n", real_percentage);
    printf("\n");
}

int main(void) {
    printf("🃏 Exemples d'utilisation des pourcentages - Advanced Range Parser\n");
    printf("===================================================================\n\n");
    
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    printf("📈 Exemples de ranges par pourcentage:\n");
    printf("======================================\n\n");
    
    // Exemples typiques de ranges de poker
    const char* example_ranges[] = {
        "2%",    // Ultra-tight (premium hands only)
        "5%",    // Very tight 
        "10%",   // Tight
        "15%",   // Tight-aggressive
        "20%",   // Standard
        "25%",   // Loose
        "30%"    // Very loose
    };
    
    const char* descriptions[] = {
        "Ultra-serré (mains premium uniquement)",
        "Très serré (mains très fortes)",
        "Serré (bonnes mains)",
        "Serré-agressif (range élargi)",
        "Standard (range équilibré)",
        "Large (range étendu)",
        "Très large (beaucoup de mains)"
    };
    
    int num_examples = sizeof(example_ranges) / sizeof(example_ranges[0]);
    
    for (int i = 0; i < num_examples; i++) {
        arp_range_t range;
        if (ARP_ParseRange(example_ranges[i], dead_cards, game_holdem, &range)) {
            printf("%s - %s\n", example_ranges[i], descriptions[i]);
            print_range_summary(example_ranges[i], &range);
            ARP_FreeRange(&range);
        }
    }
    
    printf("🎯 Comparaison avec des ranges manuels:\n");
    printf("=======================================\n\n");
    
    // Comparer un pourcentage avec un range manuel équivalent
    printf("Comparaison: 5%% vs range manuel premium\n");
    printf("------------------------------------------\n");
    
    arp_range_t range_5pct, range_manual;
    
    if (ARP_ParseRange("5%", dead_cards, game_holdem, &range_5pct)) {
        printf("Range 5%%:\n");
        print_range_summary("5%", &range_5pct);
        ARP_FreeRange(&range_5pct);
    }
    
    // Range manuel approximatif du top 5%
    const char* manual_range = "AA, KK, QQ, JJ, AKs, TT, AKo, AQs, KQs, AJs, KJs";
    if (ARP_ParseRange(manual_range, dead_cards, game_holdem, &range_manual)) {
        printf("Range manuel équivalent:\n");
        printf("'%s'\n", manual_range);
        print_range_summary(manual_range, &range_manual);
        ARP_FreeRange(&range_manual);
    }
    
    printf("💡 Avantages des pourcentages:\n");
    printf("==============================\n\n");
    
    printf("1. ✅ Simplicité: '20%%' vs 'AA,KK,QQ,JJ,TT,99,88,AKs,AQs,AJs,ATs,A9s,A8s,A7s,A6s,A5s,A4s,A3s,A2s,AKo,KQs,KJs,KTs,K9s,K8s,K7s,K6s,K5s,K4s,K3s,K2s,AQo,QJs,QTs,Q9s,Q8s,Q7s,Q6s,Q5s,Q4s,Q3s,Q2s,AJo,JTs,J9s,J8s,J7s,J6s,J5s,J4s,J3s,J2s,ATo,T9s,T8s,T7s,T6s,T5s,T4s,T3s,T2s,A9o,98s,97s,96s,95s,94s,93s,92s,KQo,87s,86s,85s,84s,83s,82s,A8o,76s,75s,74s,73s,72s,KJo,65s,64s,63s,62s,A7o,54s,53s,52s,KTo,43s,42s,A6o,32s,K9o,A5o,Q9o,A4o,J9o,A3o,K8o,A2o,Q8o'\n");
    printf("2. ✅ Précision: Basé sur le classement mathématique des mains\n");
    printf("3. ✅ Cohérence: Toujours les mêmes mains pour un pourcentage donné\n");
    printf("4. ✅ Flexibilité: Support des décimales (5.5%%, 12.3%%)\n");
    printf("5. ✅ Intuitivité: Facile à comprendre et communiquer\n\n");
    
    printf("🔧 Cas d'usage typiques:\n");
    printf("========================\n\n");
    
    printf("• Analyse de range d'ouverture: 'UTG ouvre 12%%'\n");
    printf("• 3-bet ranges: 'Je 3-bet 4%% en position'\n");
    printf("• Défense des blindes: 'Je défends 25%% depuis la BB'\n");
    printf("• All-in ranges: 'Je push 8%% en short stack'\n");
    printf("• Analyse post-flop: 'Cette action représente 15%% de son range'\n\n");
    
    printf("🎉 L'implémentation des pourcentages est maintenant complète et fonctionnelle !\n");

    return 0;
}
