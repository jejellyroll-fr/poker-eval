# Framework de Tests Unifié avec Unity

## Vue d'ensemble

Ce projet utilise maintenant Unity comme framework de tests unifié, permettant d'organiser et d'exécuter différents types de tests de manière cohérente.

## Structure des Tests

### 1. Tests Unitaires
- **Fichier**: `test_card_unity.c`
- **Objectif**: Tester des fonctions individuelles
- **Exemple**: Tests des fonctions `CharToRank`, `CharToSuit`, etc.

### 2. Tests d'Intégration
- **Dossier**: `unity_tests/`
- **Fichier**: `test_integration_example.c`
- **Objectif**: Tester l'interaction entre plusieurs composants
- **Exemple**: Intégration entre cartes et deck

### 3. Tests de Performance/Benchmarks
- **Fichier**: `test_benchmark_example.c`
- **Objectif**: Mesurer les performances des fonctions critiques
- **Exemple**: Benchmark des conversions de cartes

## Installation et Configuration

### Installation d'Unity

Unity sera automatiquement téléchargé lors de la première compilation. Si vous souhaitez l'installer manuellement :

```bash
# Depuis le dossier racine du projet
cd tests
git clone https://github.com/ThrowTheSwitch/Unity.git unity
```

### Compilation
```bash
cd build
cmake .. -DBUILD_TESTS=ON
make test_card_unity test_integration_example test_benchmark_example
```

**Note**: Si Unity n'est pas présent, CMake le téléchargera automatiquement.

### Exécution des tests Unity
```bash
# Exécuter tous les tests Unity
ctest -R "test_.*_unity|test_integration_example|test_benchmark_example" -V

# Exécuter un test spécifique
./tests/test_card_unity
./tests/test_integration_example
./tests/test_benchmark_example
```

## Ajouter de Nouveaux Tests

### 1. Créer un nouveau test unitaire

```c
#include "unity.h"
#include "VotreModule.h"

void setUp(void) {}
void tearDown(void) {}

static void test_votre_fonction(void) {
    TEST_ASSERT_EQUAL_INT(expected_value, votre_fonction(input));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_votre_fonction);
    return UNITY_END();
}
```

### 2. Ajouter le test au CMakeLists.txt

```cmake
add_unity_test(nom_du_test fichier_source.c)
```

## Assertions Unity Disponibles

- `TEST_ASSERT_EQUAL_INT(expected, actual)`
- `TEST_ASSERT_EQUAL_CHAR(expected, actual)`
- `TEST_ASSERT_TRUE(condition)`
- `TEST_ASSERT_FALSE(condition)`
- `TEST_ASSERT_NULL(pointer)`
- `TEST_ASSERT_NOT_NULL(pointer)`
- `TEST_ASSERT_EQUAL_STRING(expected, actual)`

## Avantages du Framework Unity

1. **Uniformité**: Tous les tests utilisent la même syntaxe
2. **Lisibilité**: Messages d'erreur clairs et informatifs
3. **Intégration**: Compatible avec CTest et CI/CD
4. **Performance**: Framework léger et rapide
5. **Portabilité**: Fonctionne sur toutes les plateformes

## Résultats des Tests

Les tests Unity affichent :
- Le nombre de tests exécutés
- Le nombre de tests réussis/échoués
- Les détails des échecs avec numéros de ligne
- Les métriques de performance pour les benchmarks

## Exemple de Sortie

```
/path/to/test.c:43:test_char_to_rank:PASS
/path/to/test.c:44:test_char_to_suit:PASS
/path/to/test.c:45:test_rank_to_char:PASS
/path/to/test.c:46:test_suit_to_char:PASS

-----------------------
4 Tests 0 Failures 0 Ignored 
OK
```

## Migration des Tests Existants

Les tests existants peuvent être progressivement migrés vers Unity :
1. Garder les tests existants fonctionnels
2. Créer des équivalents Unity pour les nouvelles fonctionnalités
3. Migrer progressivement les tests critiques vers Unity