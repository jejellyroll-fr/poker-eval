# Guide d'utilisation du module PLO Nomenclature

## Vue d'ensemble

Le module PLONomenclature fournit des fonctionnalités pour parser, catégoriser et manipuler les mains de Pot-Limit Omaha (PLO) selon la nomenclature standard utilisée par les coachs et les solveurs.

## Fonctionnalités principales

### 1. Parsing de mains PLO

Le module peut parser deux types de notations :

#### Mains spécifiques
```c
PLOHand hand;
PLO_ParseHand("AsKdQhJc", &hand);  // Main avec cartes spécifiques
```

#### Patterns avec placeholders
```c
PLOHand hand;
PLO_ParseHand("AAxxds", &hand);    // Paire d'As double-suited
PLO_ParseHand("JT98r", &hand);     // Rundown rainbow
PLO_ParseHand("KKxxss", &hand);    // Paire de Rois single-suited
```

### 2. Catégorisation automatique

Les mains sont automatiquement catégorisées en 21 catégories :
- **Unpaired** : Double-suited (DS), Single-suited (SS), Rainbow (RB)
- **One-Pair** : Pair DS, Pair SS, Pair RB
- **Two-Pair** : 2-Pair DS, 2-Pair SS, 2-Pair RB
- **Trips** : Trips DS, Trips SS, Trips RB
- **Aces** : AA DS, AA SS, AA RB
- **Broadway-heavy** : 3+ Broadway DS, SS, RB
- **Ragged/Low** : Ragged DS, SS, RB

### 3. Vérification de pattern

```c
PLOHand hand;
PLO_ParseHand("AsAdKhQd", &hand);
if (PLO_MatchesPattern(&hand, "AAxxds")) {
    // La main correspond au pattern AA double-suited
}
```

## Exemple d'utilisation complet

```c
#include <poker_eval/distributions/plo_nomenclature.h>
#include <stdio.h>

int main() {
    PLOHand hand;
    
    // Parser une main
    if (PLO_ParseHand("AAKQds", &hand)) {
        printf("Main parsée : %s\n", hand.notation);
        printf("Catégorie : %s\n", PLO_CategoryName(hand.category));
        printf("Suitedness : %s\n", PLO_SuitednessSuffix(hand.suitedness));
        printf("Contient un As : %s\n", hand.has_ace ? "Oui" : "Non");
        printf("Cartes Broadway : %d\n", hand.broadway_count);
        
        // Vérifier si elle correspond à un pattern
        if (PLO_MatchesPattern(&hand, "AAxxds")) {
            printf("Cette main est AA double-suited\n");
        }
        
        // Obtenir le pourcentage de cette catégorie
        float pct = PLO_CategoryPercentage(hand.category);
        printf("Cette catégorie représente %.2f%% des mains\n", pct);
    }
    
    return 0;
}
```

## Utilisation avec l'Advanced Range Parser

Les catégories peuvent être insérées directement dans l'`AdvancedRangeParser` à l'aide du préfixe `cat:` ou `category:` (casse, tirets et underscores ignorés). Cela évite d'écrire des expressions PLO longues à la main.

```c
StdDeck_CardMask dead;
StdDeck_CardMask_RESET(dead);

arp_range_t omaha_range;
if (ARP_ParseRange("cat:aa_ds + cat:unpaired_ss", dead, game_omaha, &omaha_range)) {
    printf("Nombre de combos : %zu\n", omaha_range.count);
    ARP_FreeRange(&omaha_range);
}
```

Exemples pratiques :

- `cat:aa_ds{50%}` pondère les AA double-suited à 50 %
- `category:broadway-rb - cat:pair_rb` retire les mains rainbow contenant une paire
- `cat:unpaired_ds, !cat:ragged_ds` exclut les jeux bas double-suited

Les alias (`AA-DS`, `aces_ds`, `UNPAIRED-SS`, etc.) sont acceptés et les cartes mortes sont filtrées automatiquement pendant l'expansion.

## Structures de données

### PLOHand
```c
typedef struct {
    StdDeck_CardMask cards;       // Les 4 cartes
    PLOHandCategory category;     // Catégorie (1-21)
    PLOSuitedness suitedness;     // Type de suitedness
    PLOConnectivity connectivity; // Type de connectivité
    int has_ace;                  // Contient au moins un As
    int broadway_count;           // Nombre de cartes Broadway (T-A)
    int pair_count;               // Nombre de paires (0-2)
    int trips;                    // A un brelan (1) ou non (0)
    char notation[32];            // Notation string
} PLOHand;
```

### Types de suitedness
- `PLO_SUIT_RAINBOW` : 4 couleurs différentes
- `PLO_SUIT_SINGLE` : Une seule couleur représentée 2 fois
- `PLO_SUIT_DOUBLE` : Deux couleurs représentées 2 fois chacune
- `PLO_SUIT_TRIPLE` : Une couleur représentée 3 fois
- `PLO_SUIT_QUAD` : Toutes les cartes de la même couleur (monotone)

### Types de connectivité
- `PLO_CONN_NONE` : Aucune connectivité
- `PLO_CONN_RUNDOWN` : 4 cartes consécutives (ex: JT98)
- `PLO_CONN_1GAP` : Un gap dans la séquence (ex: JT86)
- `PLO_CONN_2GAP` : Deux gaps dans la séquence
- `PLO_CONN_PARTIAL` : Connectivité partielle

## Compilation

Le module est automatiquement inclus dans la bibliothèque poker_eval. Pour compiler un programme l'utilisant :

```bash
gcc -o mon_programme mon_programme.c -lpoker_eval -lpoker_distributions
```

## Notes importantes

1. Les placeholders 'x' dans les patterns représentent n'importe quelle carte différente des cartes fixes spécifiées.
2. La fonction `PLO_ParseHand` génère un masque de cartes valide pour les patterns avec placeholders.
3. Les pourcentages de catégories sont approximatifs et basés sur la nomenclature standard PLO.
