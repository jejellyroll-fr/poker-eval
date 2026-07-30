# Equity Module

This module contains all implementations for equity calculations, board analysis, and enumeration engines in `poker-eval`.

## Source Files (`src/equity/`)

### Range Equity & Multiway
- `MultiwayEquity.c` : Multi-player equity calculation and side-pot management.
- `RangeEquity.c` : Single-threaded equity calculation between hand ranges (`CalculateEquityForRanges`).
- `RangeEquity_MT.c` : Base multithreaded implementation for range equity (`CalculateEquityForRanges_MT`).
- `RangeEquity_MT_v2.c`, `RangeEquity_MT_v3.c`, `RangeEquity_MT_v4.c`, `RangeEquity_MT_v5.c` : Multithreaded optimization variants.
- `RangeEquity_MT_Batched.c` : Optimized multithreaded implementation using batched Monte Carlo.

### Preflop, Flop Equity & Board
- `preflop_equity.c` : Preflop equity calculations, canonical hands management (169), range parsing, and lookup tables.
- `preflop_table_blob.c` : Binary embedding of the predefined preflop equity table.
- `omaha_preflop.c` : Preflop equity calculations and distributions specific to Omaha.
- `flop_equity.c` : Flop texture analysis (dry/wet/paired), improvement probabilities, and outs counting.
- `board_stats.c` : Board statistics and community card properties.

### Enumeration Engines
- `enumerate.c` : General exhaustive enumeration engine.
- `enumerate_dispatch.c` : Dispatching and automatic selection of the suitable enumeration engine.
- `enumerate_eedc.c` : EEDC-optimized enumeration.
- `enumerate_eedc_omaha_opt.c` : EEDC optimizations for Omaha.
- `enumerate_doubleflop_fix.c` : Enumeration for double-flop rules/variants.
- `enumerate_doubleflop_simd.c` : Double-flop enumeration optimized via SIMD vector instructions.
- `enumord.c` : Ordering and indexing of enumeration combinations.

### Monte Carlo, SIMD & Utilities
- `batched_montecarlo.c` : Vectorized/batched Monte Carlo simulation.
- `sampling_policies.c` : Sampling policies for Monte Carlo simulation.
- `simd_operations.c` : SIMD vector operations for equity calculations.
- `canonicalize.c` : Hand canonicalization and card isomorphism reduction.
- `pe_equity.c` : High-level poker-eval equity interface (`pe_equity_calculate`).
- `range_combo_buffers.c` : Memory buffer management for range combinations.
- `run_it_twice.c` : Equity calculation under the Run It Twice (RIT) rule.
- `sidepots.c` : Main and side pot calculation and distribution.