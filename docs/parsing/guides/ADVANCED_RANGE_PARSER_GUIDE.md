# Guide d'utilisation de l'API Advanced Range Parser

## Vue d'ensemble

L'API Advanced Range Parser permet de parser et manipuler des ranges de mains de poker en utilisant une syntaxe standard et intuitive. Cette API supporte les notations couramment utilisées dans les logiciels de poker modernes.

## Fonctionnalités

### ✅ Implémenté (Phase 1 & 2)
- ✅ **Paires de poche** : `AA`, `KK`, `AA-TT`
- ✅ **Mains suited/offsuit** : `AKs`, `AKo`, `AK`
- ✅ **Mains spécifiques** : `AsKh`, `AdQd`
- ✅ **Combinaisons** : `AA, KK, AKs`
- ✅ **Pourcentages** : `20%`, `5.5%`
- ✅ **Validation** de syntaxe
- ✅ **Conversion** vers PlayerRange
- ✅ **Gestion des cartes mortes**

### ✅ Implémenté (Phase 3 - Optimisations)
- ✅ **Cache de pourcentages** : Accélération 40x+ pour les requêtes répétées
- ✅ **Optimisation mémoire** : Estimation intelligente de la capacité
- ✅ **Hash table** : O(1) pour la détection de doublons (O(n) total vs O(n²))

### ✅ Implémenté (Phase 4 - API Avancée)
- ✅ **Ranges étendues** : `AK-AJ`, `AKs-AJs`
- ✅ **Opérateurs arithmétiques** : `+` (union), `-` (différence), `!` (exclusion)
- ✅ **Support Stud** : `(AA)K`, `(ss)Ks`
- ✅ **Messages d'erreurs détaillés** avec `ARP_ParseRangeWithError`

### ✅ Implémenté (Phase 5 - Final Polish)
- ✅ **API utilitaire complète** : `CountCombinations`, `CloneRange`, `RangesEqual`, `IntersectRanges`, `ContainsHand`
- ✅ **Import/Export** : Format texte simple pour sauvegarder/charger des ranges
- ✅ **Tests exhaustifs** : 19+ tests couvrant cache, utils, et parsing
- ✅ **Benchmarks** : Suite complète de mesure de performance

## Syntaxe supportée

Voir le document détaillé [RANGE_SYNTAX.md](../RANGE_SYNTAX.md) pour la spécification complète.

### Mains de base

```c
// Paires de poche
"AA"          // Paire d'As (6 combinaisons)
"KK"          // Paire de Rois (6 combinaisons)

// Mains suited
"AKs"         // As-Roi suited (4 combinaisons)

// Mains offsuit
"AKo"         // As-Roi offsuit (12 combinaisons)

// Mains mixtes (suited + offsuit)
"AK"          // As-Roi (16 combinaisons)
```

### Ranges étendues

```c
// Ranges de paires
"AA-TT"       // Paires d'As à Dix (30 combinaisons)

// Ranges de mains non-paires
"AK-AJ"       // AK, AQ, AJ (48 combinaisons)
"AKs-AJs"     // AKs, AQs, AJs (12 combinaisons)

// Opérateurs
"AA-TT + AK"  // Paires + AK
"20% - AA"    // Top 20% sans les As
"!AA"         // Tout sauf les As
```

### Pourcentages

```c
"20%"         // Top 20% des mains (Hold'em)
"5%"          // Top 5%
```

## Utilisation de l'API

### Parsing de base

```c
#include <poker_eval/distributions/AdvancedRangeParser.h>

arp_range_t range;
StdDeck_CardMask dead_cards;
StdDeck_CardMask_RESET(dead_cards);

if (ARP_ParseRange("AA-TT, AKs", dead_cards, game_holdem, &range)) {
    printf("Range: %zu mains\n", range.count);
    ARP_FreeRange(&range);
}
```

### Fonctions Utilitaires (Phase 5)

L'API offre maintenant des outils puissants pour manipuler les ranges sans parsing complexe :

```c
// Compter les mains sans générer la range complète
size_t count = ARP_CountCombinations("AA-TT", dead_cards, game_holdem);

// Vérifier si une main est dans la range
bool has_aces = ARP_ContainsHand(&range, aces_mask);

// Intersection de deux ranges
arp_range_t intersection;
ARP_IntersectRanges(&range1, &range2, &intersection);

// Comparer deux ranges
bool equal = ARP_RangesEqual(&range1, &range2);

// Cloner une range
arp_range_t clone;
ARP_CloneRange(&original, &clone);
```

### Gestion d'erreurs détaillée

```c
arp_error_details_t error;
if (!ARP_ParseRangeWithError("invalid", dead, game_holdem, &range, &error)) {
    char buffer[256];
    ARP_FormatError(&error, buffer, sizeof(buffer));
    printf("Erreur: %s\n", buffer);
}
```

## Performance

### Cache de Pourcentages

Le parser utilise un cache thread-safe pour les requêtes de pourcentage ("20%", "5%").

- **Premier appel** : ~10-20µs (calcul initial)
- **Appels suivants** : ~0.2-0.5µs (copie mémoire)
- **Speedup** : ~40x

Voir [PERFORMANCE_GUIDE.md](../guides/PERFORMANCE_GUIDE.md) pour plus de détails.

### Optimisation Mémoire

- Allocation exacte pour les ranges simples (paires, mains spécifiques)
- Hash tables pour les grandes ranges (>50 mains) pour éviter les doublons en O(1)

## Benchmarks

Les résultats typiques sur une machine moderne :

| Scénario | Temps (µs) | Mains/sec |
|----------|------------|-----------|
| Single pair (AA) | 0.26 | 22M |
| Top 20% (Cache) | 0.42 | 516M |
| Top 50% (Cache) | 0.44 | 1.1G |

## Support Multi-Jeux

- **Hold'em** : Support complet
- **Omaha (PLO)** : Support partiel (patterns `AAxxds`, pourcentages basiques)
- **Stud** : Support des patterns `(AA)K`
