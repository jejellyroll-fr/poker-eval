# Guide de Tests - Poker-Eval

Ce guide présente comment exécuter et gérer les tests du projet.

## Building and Running Tests

Le répertoire `tests/` est directement configuré dans le `CMakeLists.txt` principal à la line 480 (`add_subdirectory(tests)` est activé lorsque la variable `BUILD_TESTS` est activée).

### Activer et Executer la Suite de Tests

Par défaut ou lors de la configuration CMake, activez `BUILD_TESTS` pour compiler l'ensemble des tests unitaires et d'intégration :

```bash
mkdir -p build
cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Exécuter l'ensemble des tests via CTest
ctest --output-on-failure
```

### Exécution ciblée des executables de tests

Une fois compilés, vous pouvez exécuter directement les exécutables de test situés dans `build/tests/` :

```bash
./tests/test_card_unity              # Tests Unity basiques
./tests/test_holdem                  # Texas Hold'em
./tests/test_omaha_simple            # Omaha
./tests/test_badugi                  # Badugi
./tests/joker_eval_test              # Joker
./tests/test_range_equity_mt         # Range Equity MT
./tests/test_icm                     # ICM
```

### Exécution par catégorie avec CTest

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

## Exemples et Benchmarks

En complément de la suite de tests unitaires, le projet fournit :

1. **Exemples d'utilisation** (dans `src/examples/`)
2. **Benchmarks de performance** (dans `src/benchmarks/` et `bin/`)

Exemples d'exécution des benchmarks :
```bash
./bin/bench_7c_simd_micro
./bin/bench_cfr_holdem_river
```
