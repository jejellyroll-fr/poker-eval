# Module Equity

Ce module contient tous les calculs d'équité et d'énumération :

## Composants

### Énumération de base
- `enumerate.h/c` - Énumération exhaustive
- `enumerate_eedc.h/c` - Énumération optimisée
- `enumord.h/c` - Ordonnancement des énumérations

### Monte Carlo
- `montecarlo.h/c` - Simulations Monte Carlo
- `batched_montecarlo.h/c` - Monte Carlo par batch

### Calculs d'équité
- `range_equity.h/c` - Équité entre ranges
- `equity_calculator.h/c` - Calculateur d'équité principal

### Optimisations
- `simd_operations.h/c` - Opérations SIMD
- `multithreading.h/c` - Support multi-threading
- `gpu_acceleration.h/c` - Accélération GPU

### Énumérations spécialisées
- `doubleflop.h/c` - Énumération double flop
- `enumerate_simd.h/c` - Énumération SIMD