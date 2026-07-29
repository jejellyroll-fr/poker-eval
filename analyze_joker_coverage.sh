#!/bin/bash

# Script pour analyser la couverture des tests JokerDeck

echo "=== Analyse de la couverture des tests JokerDeck ==="
echo

# Couleurs pour l'affichage
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Compteurs
total_features=0
tested_features=0

# Fonction pour marquer une fonctionnalité
check_feature() {
    local feature="$1"
    local tested="$2"
    local details="$3"
    
    ((total_features++))
    
    if [ "$tested" = "yes" ]; then
        ((tested_features++))
        echo -e "${GREEN}✓${NC} $feature"
        if [ -n "$details" ]; then
            echo "  └─ $details"
        fi
    else
        echo -e "${RED}✗${NC} $feature"
        if [ -n "$details" ]; then
            echo "  └─ $details"
        fi
    fi
}

echo "1. PARSING ET RECONNAISSANCE DU JOKER"
echo "======================================"
check_feature "Parsing 'Xx' comme joker" "yes" "Testé manuellement avec pokenum"
check_feature "Parsing 'xx' comme joker" "yes" "Universal_StringToCard supporte toutes les variantes"
check_feature "Parsing 'XX' comme joker" "yes" "Universal_StringToCard supporte toutes les variantes"
check_feature "Parsing 'xX' comme joker" "yes" "Universal_StringToCard supporte toutes les variantes"
check_feature "Test unitaire du parsing" "yes" "test_joker_support.c: test_joker_parsing()"
echo

echo "2. OPÉRATIONS SUR LES MASQUES DE CARTES"
echo "========================================"
check_feature "JokerDeck_CardMask_SET avec joker" "yes" "test_joker_support.c: test_joker_cardmask()"
check_feature "JokerDeck_CardMask_CARD_IS_SET avec joker" "yes" "test_joker_support.c: test_joker_cardmask()"
check_feature "JokerDeck_numCards avec joker" "yes" "test_joker_support.c: test_joker_cardmask()"
check_feature "JokerDeck_CardMask_OR avec joker" "yes" "test_joker_mask_ops.c: test_joker_mask_or()"
check_feature "JokerDeck_CardMask_AND avec joker" "yes" "test_joker_mask_ops.c: test_joker_mask_and()"
check_feature "JokerDeck_CardMask_NOT avec joker" "yes" "test_joker_mask_ops.c: test_joker_mask_not()"
echo

echo "3. CONVERSIONS ENTRE DECKS"
echo "=========================="
check_feature "Universal_ConvertStdToJoker" "yes" "test_joker_support.c: test_deck_conversion()"
check_feature "Universal_ConvertJokerToStd" "yes" "test_joker_conversion.c: test_joker_to_std_conversion()"
check_feature "Conversion bidirectionnelle" "yes" "test_joker_conversion.c: test_bidirectional_conversion()"
check_feature "Conversion de masques vides" "yes" "test_joker_conversion.c: test_empty_mask_conversion()"
check_feature "Préservation des cartes lors de la conversion" "yes" "test_joker_support.c vérifie"
check_feature "Gestion du joker lors de la conversion" "yes" "Vérifié que le joker n'apparaît pas"
echo

echo "4. ÉNUMÉRATION"
echo "=============="
check_feature "JOKERDECK_ENUMERATE_COMBINATIONS_D" "yes" "Testé via pokenum -l"
check_feature "JOKERDECK_MONTECARLO_PERMUTATIONS_D" "yes" "Testé via pokenum -mc"
check_feature "Énumération avec 1 joueur" "yes" "test_joker_enum.c"
check_feature "Énumération avec 2 joueurs" "yes" "Tests pokenum multiples"
check_feature "Énumération avec 3+ joueurs" "yes" "test_joker_multiplayer.c: test_3_players() et test_4_players_monte_carlo()"
check_feature "Gestion des cartes mortes avec joker" "yes" "test_joker_enum.c teste avec joker mort"
echo

echo "5. JEUX SUPPORTÉS"
echo "================"
check_feature "game_lowball (-l)" "yes" "Testé extensivement"
check_feature "game_5draw (-5d)" "yes" "test_joker_games.c: test_5draw_hi() avec Monte Carlo"
check_feature "game_5draw8 (-5d8)" "yes" "test_joker_games.c: test_5draw8_hilo() avec Monte Carlo"
check_feature "game_5drawnsq (-5dnsq)" "yes" "test_joker_games.c: test_5drawnsq_hilo() avec Monte Carlo"
echo

echo "6. ÉVALUATION DES MAINS"
echo "======================="
check_feature "JokerDeck_JokerRules_EVAL_N" "yes" "Utilisé dans INNER_LOOP_5DRAW"
check_feature "JokerDeck_Lowball_EVAL" "yes" "Utilisé dans INNER_LOOP_LOWBALL"
check_feature "JokerDeck_Lowball8_EVAL" "yes" "Testé indirectement via test_joker_games.c"
check_feature "Joker comme As en lowball" "yes" "test_joker_hands.c: test_joker_in_lowball()"
check_feature "Joker pour compléter une quinte" "yes" "test_joker_hands.c: test_joker_completes_straight()"
check_feature "Joker pour compléter une couleur" "yes" "test_joker_hands.c: test_joker_completes_flush()"
check_feature "Joker pour faire un carré" "yes" "test_joker_hands.c: test_joker_makes_quads()"
check_feature "Joker pour faire un full" "yes" "test_joker_hands.c: test_joker_makes_fullhouse()"
echo

echo "7. CAS LIMITES ET ERREURS"
echo "========================="
check_feature "Deck avec 53 cartes" "yes" "JokerDeck_N_CARDS = 53 vérifié"
check_feature "Index du joker = 52" "yes" "test_joker_support.c: test_joker_index()"
check_feature "Gestion de 2 jokers (erreur)" "no" "Un seul joker dans le deck, mais non testé"
check_feature "Performance avec grandes énumérations" "partial" "Identifié des timeouts, solution MC proposée"
check_feature "Mémoire et fuites" "no" "Pas de tests valgrind"
echo

echo "8. INTÉGRATION"
echo "=============="
check_feature "Build avec CMake" "yes" "Compilation réussie"
check_feature "Intégration dans pokenum" "yes" "Fonctionne correctement"
check_feature "Documentation utilisateur" "yes" "JOKER_USAGE_GUIDE.md créé"
check_feature "Tests de régression" "no" "Pas de suite de tests automatisée"
check_feature "Compatibilité bindings (Python, etc.)" "no" "Non testé"
echo

# Calcul du pourcentage
percentage=$(echo "scale=2; $tested_features * 100 / $total_features" | bc)

echo "======================================"
echo "RÉSUMÉ DE LA COUVERTURE"
echo "======================================"
echo "Fonctionnalités testées : $tested_features / $total_features"
echo -e "Couverture : ${YELLOW}${percentage}%${NC}"
echo

# Détails par catégorie
echo "Recommandations prioritaires :"
echo "1. Tester tous les jeux avec joker (-5d, -5d8, -5dnsq)"
echo "2. Ajouter des tests pour 3+ joueurs"
echo "3. Tester les cas limites (joker pour quinte/couleur)"
echo "4. Exécuter des tests de mémoire (valgrind)"
echo "5. Créer une suite de tests de régression automatisée"