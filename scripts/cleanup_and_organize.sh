#!/bin/bash
# cleanup_and_organize.sh - Clean up build artifacts and organize project

echo "=== Cleaning up poker-eval project ==="

# Remove build artifacts from root directory
echo "Removing build artifacts from root..."
rm -f adaptive_gpu_example
rm -f batched_montecarlo_example
rm -f bench_compressed_tables
rm -f bench_doubleflop_simd
rm -f bench_eedc_quick
rm -f bench_eedc_vs_classic
rm -f bench_lto_comparison
rm -f bench_omaha_opt
rm -f benchmark_mt_batched
rm -f benchmark_range_equity_mt
rm -f doubleflop_example
rm -f eval
rm -f evx_gen
rm -f fish
rm -f gpu_eval_example
rm -f hand_distribution_example
rm -f hcmp2
rm -f hcmpn
rm -f mktab_astud
rm -f mktab_basic
rm -f mktab_evx
rm -f mktab_joker
rm -f mktab_lowball
rm -f mktab_packed
rm -f mktab_short
rm -f plo_equity_example
rm -f pokenum
rm -f seven_card_hands
rm -f simd_range_equity_example
rm -f usedecks
rm -f simple_lto_demo

# Remove test executables from tests directory
echo "Cleaning test executables..."
cd tests
rm -f benchmark_final_micro_opt
rm -f benchmark_micro_opt
rm -f benchmark_realistic_divisions
rm -f quick_benchmark
rm -f simple_alignment_test
rm -f test_aligned_tables
rm -f test_batched_montecarlo
rm -f test_card
rm -f test_cardconverter
rm -f test_deck_operations
rm -f test_eval_cache
rm -f test_gpu_acceleration
rm -f test_hand_distribution
rm -f test_hand_evaluation_high
rm -f test_handagnostichand
rm -f test_lowball
rm -f test_micro_optimizations
rm -f test_omaha_dynamic_allocation
rm -f test_PLONomenclature
rm -f test_range_equity_aavs_kk
rm -f test_range_equity_mt
rm -f test_simd_operations
rm -f test_simple_equity
cd ..

# Remove temporary build directories
echo "Removing temporary build directories..."
rm -rf build_lto
rm -rf build_test_lto

# Remove backup files
rm -f libpoker_lib_static.bak

# Remove temporary test files
rm -f test_doubleflop.c
rm -f debug_doubleflop.c

echo "Cleanup complete!"