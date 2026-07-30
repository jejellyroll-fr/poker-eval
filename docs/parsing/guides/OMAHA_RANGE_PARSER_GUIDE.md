# Guide d'utilisation de l'API Omaha Range Parser

## Vue d'ensemble

L'API Omaha Range Parser étend l'Advanced Range Parser pour supporter les ranges de mains Omaha (PLO) avec une syntaxe inspirée des outils professionnels de poker. Cette implémentation supporte les patterns PLO standards, les pourcentages, et les opérateurs de base.

## Fonctionnalités implémentées

### ✅ Phase 1 - Implémentation de base

- ✅ **Patterns PLO standards** : `AAxxds`, `JT98r`, `KKxxss`
- ✅ **Mains spécifiques** : `AsKdQhJc`, `AhKdQsJh`
- ✅ **Pourcentages Omaha** : `20%`, `5%`, `10%`
- ✅ **Combinaisons** : `AAxxds, KKxxds, JT98r`
- ✅ **Validation** de syntaxe PLO
- ✅ **Intégration** avec OmahaHandList
- ✅ **Support multi-jeux** Omaha (PLO, PLO8, etc.)

### ✅ Phase 2 - Opérateurs avancés

- ✅ **Opérateurs** : `+`, `-`, `!`
- ✅ **Expressions complexes** : `20% - AAxx`, `JT98r + JT98ds`
- ✅ **Ranges étendues** : Support de patterns plus complexes

### 🚧 Phase 3 - Optimisation (En cours)

- 🚧 **Rankings complets** des mains Omaha
- 🚧 **Performance** optimisée pour grandes ranges
- 🚧 **Cache** de patterns fréquents

## Syntaxe supportée

### Patterns PLO de base

```c
// Paires avec wildcards
"AAxx"        // Paire d'As avec deux cartes quelconques
"KKxx"        // Paire de Rois avec deux cartes quelconques
"QQxx"        // Paire de Dames avec deux cartes quelconques

// Patterns avec suitedness
"AAxxds"      // Paire d'As double-suited
"KKxxss"      // Paire de Rois single-suited
"QQxxr"       // Paire de Dames rainbow (non implémenté)

// Rundowns
"JT98"        // Jack-Ten-Nine-Eight (toutes couleurs)
"JT98r"       // Jack-Ten-Nine-Eight rainbow
"JT98ds"      // Jack-Ten-Nine-Eight double-suited
"JT98ss"      // Jack-Ten-Nine-Eight single-suited

// Broadway patterns
"AKQJds"      // As-Roi-Dame-Valet double-suited
"AKQTds"      // As-Roi-Dame-Dix double-suited
"AKJTds"      // As-Roi-Valet-Dix double-suited
```

### Mains spécifiques

```c
// Mains complètement spécifiées
"AsKdQhJc"    // As de pique, Roi de carreau, Dame de cœur, Valet de trèfle
"AhKhQsJs"    // As et Roi de cœur, Dame et Valet de pique
"AdKdQdJd"    // Couleur complète en carreau
```

### Pourcentages

```c
// Pourcentages de top hands
"20%"         // Top 20% des mains Omaha
"10%"         // Top 10% des mains Omaha
"5%"          // Top 5% des mains Omaha
"2.5%"        // Top 2.5% des mains Omaha
```

### Combinaisons

```c
// Virgules pour séparer les patterns
"AAxxds, KKxxds, QQxxds"          // Paires premium double-suited
"JT98r, JT98ds, JT98ss"           // Rundown dans différentes couleurs
"AAxx, KKxx, AKQJds"              // Mix de patterns
"20%, AAxxds"                     // Pourcentage plus patterns spécifiques
```

## Utilisation de l'API

### Exemple de base

```c
#include "AdvancedRangeParser.h"

int main() {
    // Cartes mortes (optionnel)
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    // Parser une range Omaha
    arp_range_t range;
    if (ARP_ParseOmahaRange("AAxxds, KKxxds", dead_cards, game_omaha, &range)) {
        printf("Range Omaha parsée: %zu mains\n", range.count);
        printf("Type de jeu: %s\n", range.is_omaha ? "Omaha" : "Hold'em");
        
        // Convertir pour utilisation avec PLO
        OmahaHandList hand_list;
        if (OmahaHandList_Init(&hand_list, range.count)) {
            ARP_ToOmahaHandList(&range, &hand_list);
            printf("Converti en OmahaHandList: %d mains\n", hand_list.count);
            OmahaHandList_Free(&hand_list);
        }
        
        ARP_FreeRange(&range);
    }
    
    return 0;
}
```

### Validation de syntaxe PLO

```c
char error_buffer[256];
if (!ARP_ValidateOmahaRangeString("AAxxds, JT98r", error_buffer, sizeof(error_buffer))) {
    printf("Erreur de syntaxe PLO: %s\n", error_buffer);
} else {
    printf("Syntaxe PLO valide\n");
}
```

### Pourcentages Omaha

```c
arp_range_t range;
if (ARP_GetOmahaTopPercentage(0.20f, game_omaha, dead_cards, &range)) {
    printf("Top 20%% Omaha: %zu mains\n", range.count);
    ARP_FreeRange(&range);
}
```

### Ajout de patterns

```c
arp_range_t range;
ARP_ParseOmahaRange("AAxxds", dead_cards, game_omaha, &range);

// Ajouter un autre pattern
ARP_AddPLOPattern("KKxxds", dead_cards, &range);
printf("Range étendue: %zu mains\n", range.count);

ARP_FreeRange(&range);
```

## Exemples pratiques

### Calcul d'équité PLO vs PLO

```c
#include "AdvancedRangeParser.h"
#include "RangeEquity.h"

void calculate_plo_equity() {
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    // Parser les ranges des deux joueurs
    arp_range_t range1, range2;
    ARP_ParseOmahaRange("AAxxds, KKxxds", dead_cards, game_omaha, &range1);
    ARP_ParseOmahaRange("JT98r, JT98ds", dead_cards, game_omaha, &range2);
    
    // Convertir en PlayerRange pour calculs d'équité
    PlayerRange player_ranges[2];
    ARP_ToPlayerRange(&range1, &player_ranges[0]);
    ARP_ToPlayerRange(&range2, &player_ranges[1]);
    
    // Board vide pour preflop
    StdDeck_CardMask board;
    StdDeck_CardMask_RESET(board);
    
    // Calculer l'équité (utilise les fonctions existantes)
    enum_result_t results;
    int matchups = CalculateEquityForRanges(
        game_omaha,
        player_ranges,
        2,
        board,
        dead_cards,
        5,      // 5 cartes de board
        true,   // Monte Carlo
        10000,  // 10k itérations
        0,
        &results
    );
    
    if (matchups > 0) {
        printf("Équité premium pairs: %.2f%%\n", 
               results.ev[0] / results.nsamples * 100.0);
        printf("Équité rundowns: %.2f%%\n", 
               results.ev[1] / results.nsamples * 100.0);
    }
    
    ARP_FreeRange(&range1);
    ARP_FreeRange(&range2);
}
```

### Analyse de range PLO

```c
void analyze_plo_range(const char* range_string) {
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    arp_range_t range;
    if (ARP_ParseOmahaRange(range_string, dead_cards, game_omaha, &range)) {
        printf("=== Analyse PLO: %s ===\n", range_string);
        printf("Nombre de mains: %zu\n", range.count);
        
        if (range.is_percentage) {
            printf("Pourcentage: %.2f%%\n", range.percentage_used * 100.0f);
        }
        
        // Convertir pour analyse PLO
        OmahaHandList hand_list;
        if (OmahaHandList_Init(&hand_list, range.count)) {
            ARP_ToOmahaHandList(&range, &hand_list);
            
            // Analyser les types de mains (nécessite PLONomenclature)
            printf("Mains converties pour analyse PLO\n");
            
            OmahaHandList_Free(&hand_list);
        }
        
        ARP_FreeRange(&range);
    }
}
```

### Comparaison de ranges

```c
void compare_ranges() {
    StdDeck_CardMask dead_cards;
    StdDeck_CardMask_RESET(dead_cards);
    
    const char* ranges[] = {
        "AAxxds",
        "20%",
        "JT98r, JT98ds",
        "AAxx, KKxx, QQxx",
        NULL
    };
    
    printf("=== Comparaison de ranges PLO ===\n");
    
    for (int i = 0; ranges[i] != NULL; i++) {
        arp_range_t range;
        if (ARP_ParseOmahaRange(ranges[i], dead_cards, game_omaha, &range)) {
            printf("%-20s: %zu mains", ranges[i], range.count);
            if (range.is_percentage) {
                printf(" (%.1f%%)", range.percentage_used * 100.0f);
            }
            printf("\n");
            ARP_FreeRange(&range);
        }
    }
}
```

## Types de jeux supportés

L'API supporte tous les variants Omaha :

```c
// Types de jeux Omaha supportés
enum_game_t omaha_games[] = {
    game_omaha,     // PLO High
    game_omaha8,    // PLO Hi/Lo 8-or-better
    game_omaha5,    // 5-card PLO
    game_omaha6,    // 6-card PLO
    game_omaha85    // 5-card PLO Hi/Lo
};

// Utilisation
for (int i = 0; i < 5; i++) {
    arp_range_t range;
    if (ARP_ParseOmahaRange("AAxxds", dead_cards, omaha_games[i], &range)) {
        printf("Jeu %d: %zu mains\n", omaha_games[i], range.count);
        ARP_FreeRange(&range);
    }
}
```

## Gestion d'erreurs

### Codes de retour
- `1` : Succès
- `0` : Échec

### Messages d'erreur spécifiques PLO

```c
char error_buffer[256];
if (!ARP_ValidateOmahaRangeString("INVALID_PLO", error_buffer, sizeof(error_buffer))) {
    printf("Erreur PLO: %s\n", error_buffer);
}
```

### Erreurs courantes PLO

| Erreur | Cause | Solution |
|--------|-------|----------|
| `Invalid PLO pattern` | Pattern non reconnu | Vérifier la syntaxe PLO |
| `Failed to parse PLO pattern` | Erreur dans PLOIntegration | Vérifier les dépendances |
| `Non-Omaha game type` | Jeu non-Omaha | Utiliser game_omaha, etc. |
| `Failed to expand Omaha percentage` | Erreur de pourcentage | Vérifier 0.0 < % <= 1.0 |

## Performance

### Complexité pour Omaha
- **Parsing PLO** : O(n) où n = longueur du pattern
- **Génération** : O(m) où m = nombre de mains générées
- **Mémoire** : O(m) pour stocker les mains

### Optimisations PLO
- Utilisation de PLOIntegration pour génération efficace
- Cache des patterns fréquents (futur)
- Allocation dynamique optimisée

## Limitations actuelles

1. **Opérateurs** : +, -, ! non implémentés (Phase 2)
2. **Rankings complets** : Utilise patterns simplifiés pour %
3. **Patterns avancés** : Certains patterns PLO complexes non supportés
4. **Performance** : Non optimisé pour très grandes ranges

## Roadmap

### Phase 2 (3-5 jours)
- [ ] Implémentation des opérateurs (`+`, `-`, `!`)
- [ ] Support des expressions complexes
- [ ] Amélioration des pourcentages avec rankings complets
- [ ] Tests de performance

### Phase 3 (2-3 jours)
- [ ] Optimisation mémoire et vitesse
- [ ] Cache intelligent des patterns
- [ ] Support de patterns PLO avancés
- [ ] Documentation complète

## Intégration avec l'écosystème

### Compatibilité
- ✅ **PLONomenclature** : Utilise les structures PLO existantes
- ✅ **OmahaHandDistribution** : Intégration complète
- ✅ **RangeEquity** : Compatible avec calculs d'équité
- ✅ **PLOIntegration** : Utilise les fonctions de gén��ration

### Extensions futures
- Support de nouveaux variants Omaha
- Intégration avec GPU pour grandes ranges
- API de manipulation avancée des ranges
- Export/import de ranges au format standard

## Exemples d'utilisation avancée

### Range building progressif

```c
// Construire une range progressivement
arp_range_t range;
ARP_ParseOmahaRange("", dead_cards, game_omaha, &range); // Range vide

// Ajouter des patterns un par un
ARP_AddPLOPattern("AAxxds", dead_cards, &range);
ARP_AddPLOPattern("KKxxds", dead_cards, &range);
ARP_AddPLOPattern("QQxxds", dead_cards, &range);

printf("Range construite: %zu mains\n", range.count);
ARP_FreeRange(&range);
```

### Validation en temps réel

```c
// Pour interface utilisateur
bool validate_user_input(const char* input) {
    char error[256];
    return ARP_ValidateOmahaRangeString(input, error, sizeof(error));
}
```

Cette implémentation fournit une base solide pour le parsing de ranges Omaha avec une syntaxe professionnelle, prête pour l'extension avec les opérateurs avancés en Phase 2.