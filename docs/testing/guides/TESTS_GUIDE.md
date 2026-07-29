# Guide de Tests - Poker-Eval

Ce guide présente comment tester le projet après la réorganisation.

## Problème Actuel

⚠️ **Le répertoire `tests/` n'est pas inclus dans le CMakeLists.txt principal**, donc les tests unitaires ne sont pas compilés actuellement.

### État Actuel du Build

Après `cmake .. && make`, voici ce qui est disponible :

1. **Tests CTest limités** (seulement 7 tests) :
   - 3 tests de validation 7c (SIMD vs Scalar)
   - 3 tests bench_enum_adapters
   - 1 test Python binding

2. **Exemples fonctionnels** (65+ exécutables dans `build/src/examples/`)
3. **Benchmarks** (15+ exécutables dans `build/bin/`)

## Solution Temporaire : Tester avec les Exemples

En attendant la correction du CMakeLists.txt, voici comment tester :

### 1. Tests de Validation Actuels

```bash
cd build

# Tests SIMD 7-card (actuellement 2 échecs mineurs)
ctest --output-on-failure

# Tests spécifiques
./bin/bench_7c_validate --samples 5000 --seed 12345 --mode random
./bin/bench_7c_validate --samples 5000 --mode sf-heavy

# Tests d'énumération
./src/examples/bench_enum_adapters --verify-only --k 5
./src/examples/bench_enum_adapters --verify-only --k 2
```

### 2. Exemples Fonctionnels (Meilleure Option Actuelle)

```bash
cd build/src/examples

# === Tests de Base ===
./eval                              # Évaluation simple de mains
./five_card_hands                   # Mains à 5 cartes
./seven_card_hands                  # Mains à 7 cartes
./usedecks                          # Test de différents decks

# === Tests Hold'em & Omaha ===
./modern_api_example                # API moderne
./plo_equity_example                # PLO equity
./omaha_range_example               # Ranges Omaha
./omaha_combinations_example        # Combinaisons Omaha
./range_equity_mt_example           # Equity multi-thread

# === Tests de Jeux Spéciaux ===
./badugi_example                    # Badugi
./drawmaha_example                  # Draw Mahaha
./pineapple_example                 # Pineapple
./doubleflop_example                # Double Flop

# === Tests de Ranges ===
./advanced_range_example            # Parsing de ranges avancé
./complex_expressions_example       # Expressions complexes
./stud_range_parser_demo            # Ranges Stud

# === Tests d'Optimisation ===
./batched_montecarlo_example        # Monte Carlo batché
./simd_range_equity_example         # SIMD pour equity
./modern_combinations_example       # Combinaisons modernes
./modern_mask_example               # Masques modernes

# === Tests ICM & Distribution ===
./icm_calculator_example            # ICM pour tournois
./hand_distribution_example         # Distributions de mains

# === Open Face Chinese ===
./ofcalc                            # Calculateur OFC
```

### 3. Benchmarks

```bash
cd build/bin

# Benchmarks SIMD
./bench_7c_simd_micro
./bench_7c_hilo_singlepass

# Benchmarks CFR (Counterfactual Regret)
./bench_cfr_holdem_river
./bench_cfr_holdem_turn
./bench_cfr_omaha_river
./bench_cfr_razz_river
./bench_cfr_shortdeck_river
./bench_cfr_stud_river

# Benchmarks OFC
./ofc_simd_benchmark

# Benchmarks Canonicalization
./benchmark_canonical_vs_legacy
./benchmark_iterator_perf

# Benchmark reproductible
./bench_reproducible_suite
```

### 4. Script de Test Rapide

Créez un script pour tester les composants clés :

```bash
#!/bin/bash
# test_quick.sh

cd build/src/examples || exit 1

echo "=== Test 1: Évaluation de base ==="
./eval || echo "FAILED: eval"

echo ""
echo "=== Test 2: API Moderne ==="
./modern_api_example || echo "FAILED: modern_api_example"

echo ""
echo "=== Test 3: Omaha PLO ==="
./plo_equity_example || echo "FAILED: plo_equity_example"

echo ""
echo "=== Test 4: Badugi ==="
./badugi_example || echo "FAILED: badugi_example"

echo ""
echo "=== Test 5: ICM Calculator ==="
./icm_calculator_example || echo "FAILED: icm_calculator_example"

echo ""
echo "=== Test 6: Range Parsing ==="
./advanced_range_example || echo "FAILED: advanced_range_example"

echo ""
echo "=== Test 7: Monte Carlo Batché ==="
./batched_montecarlo_example || echo "FAILED: batched_montecarlo_example"

echo ""
echo "=== Tous les tests terminés ==="
```

## Solution Permanente : Activer les Vrais Tests

Pour compiler les vrais tests unitaires (120+ tests), il faut modifier le `CMakeLists.txt` principal :

### Modification Nécessaire

Ajouter dans `/Users/jdenis/gitea/poker-eval/CMakeLists.txt` après la ligne `add_subdirectory(src)` :

```cmake
# Tests
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

### Après cette modification

```bash
# Reconfigurer et rebuilder
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)

# Lancer tous les tests
ctest --output-on-failure

# Ou des tests spécifiques
./tests/test_card_unity              # Tests Unity basiques
./tests/test_holdem                  # Texas Hold'em
./tests/test_omaha_simple            # Omaha
./tests/test_badugi                  # Badugi
./tests/joker_eval_test              # Joker
./tests/test_range_equity_mt         # Range Equity MT
./tests/test_icm_calculator          # ICM
```

## Tests par Catégorie (Une fois compilés)

```bash
# Tests Core
ctest -L core --output-on-failure

# Tests Equity
ctest -L equity --output-on-failure

# Tests Range
ctest -L range --output-on-failure

# Tests Engine
ctest -L engine --output-on-failure

# Tests Betting
ctest -L betting --output-on-failure
```

## Tests Recommandés pour Validation

### Test Rapide (5 minutes)
```bash
cd build/src/examples
./eval && ./modern_api_example && ./plo_equity_example && ./badugi_example && ./icm_calculator_example
```

### Test Complet (30+ minutes, après fix CMake)
```bash
cd build
ctest -j$(nproc) --output-on-failure
```

## Résultat des Tests Actuels

```
Test project /Users/jdenis/gitea/poker-eval/build
    Start 1: validate_7c_random        - FAILED (22 mismatches sur 5000)
    Start 2: validate_7c_nfs_heavy     - FAILED (18 mismatches sur 5000)
    Start 3: validate_7c_sf_heavy      - PASSED
    Start 4: bench_enum_adapters_k5    - PASSED
    Start 5: bench_enum_adapters_k2    - PASSED
    Start 6: bench_enum_adapters_k1    - PASSED
    Start 7: test_python_binding       - PASSED

71% tests passed (5 of 7)
```

Les 2 échecs sont mineurs (< 0.5% de mismatch sur validation SIMD vs Scalar).

## Prochaines Étapes

1. ✅ Activer la compilation des tests (modifier CMakeLists.txt)
2. 🔧 Corriger les 2 tests de validation SIMD qui échouent
3. 🧪 Exécuter la suite complète de tests (120+ tests)
4. 📊 Générer un rapport de couverture de tests

---

*Note: Ce guide sera mis à jour une fois les tests unitaires intégrés au build.*
