#!/bin/bash
# compare_optimizations.sh - Compare performance with and without micro-optimizations

echo "=== Comparing Performance With/Without Micro-Optimizations ==="
echo

# Create a version of enumerate.c without optimizations
echo "Creating version without optimizations..."
cp ../lib/enumerate.c enumerate_no_opt.c

# Remove micro_optimizations.h include and revert to direct divisions
sed -i '' 's/#include "micro_optimizations.h"/\/\/ #include "micro_optimizations.h"/' enumerate_no_opt.c
sed -i '' 's/get_hishare_reciprocal(hishare)/1.0 \/ hishare/g' enumerate_no_opt.c
sed -i '' 's/get_hishare_half_reciprocal(hishare)/0.5 \/ hishare/g' enumerate_no_opt.c
sed -i '' 's/get_hishare_half_reciprocal(loshare)/0.5 \/ loshare/g' enumerate_no_opt.c
sed -i '' 's/get_hishare_reciprocal(loshare)/1.0 \/ loshare/g' enumerate_no_opt.c
sed -i '' 's/likely(/(/g' enumerate_no_opt.c
sed -i '' 's/unlikely(/(/g' enumerate_no_opt.c

# Build test program with original optimized version
echo "Building with optimizations..."
gcc -O3 -march=native -I../include -DWITH_OPT -c quick_benchmark.c -o quick_benchmark_opt.o
gcc quick_benchmark_opt.o -o benchmark_with_opt -L../build -lpoker_lib -lm

# Build library without optimizations
echo "Building library without optimizations..."
gcc -O3 -march=native -I../include -c enumerate_no_opt.c -o enumerate_no_opt.o
ar rcs libpoker_no_opt.a enumerate_no_opt.o ../build/CMakeFiles/poker_lib_static.dir/lib/*.o

# Build test program with non-optimized version
gcc -O3 -march=native -I../include -c quick_benchmark.c -o quick_benchmark_no_opt.o
gcc quick_benchmark_no_opt.o -o benchmark_no_opt -L. -lpoker_no_opt -L../build -lpoker_lib -lm

echo
echo "=== Running Benchmarks ==="
echo
echo "--- WITH Micro-Optimizations ---"
./benchmark_with_opt

echo
echo "--- WITHOUT Micro-Optimizations ---"
./benchmark_no_opt

# Cleanup
rm -f enumerate_no_opt.c *.o benchmark_with_opt benchmark_no_opt libpoker_no_opt.a