#!/bin/bash
# build_optimized.sh - Build poker-eval with various optimizations

set -e

echo "=== Building poker-eval with optimizations ==="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Create build directory
BUILD_DIR="build_optimized"
rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# Configure with optimizations
echo -e "${GREEN}Configuring with LTO and architecture-specific builds...${NC}"
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_LTO=ON \
    -DBUILD_ARCH_SPECIFIC=ON \
    -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG"

# Build
echo -e "${GREEN}Building libraries...${NC}"
make -j$(nproc)

# List generated libraries
echo -e "${YELLOW}Generated libraries:${NC}"
ls -la libpoker_lib*.{so,a} 2>/dev/null || echo "No libraries found"

# Build comparison benchmark
echo -e "${GREEN}Building architecture comparison benchmark...${NC}"
if [ -f ../examples/bench_arch_comparison.c ]; then
    gcc -o bench_arch_comparison ../examples/bench_arch_comparison.c \
        -L. -lpoker_lib_static -lm -ldl \
        -I../include -I../inlines \
        -O3 -march=native
    echo -e "${GREEN}Benchmark built successfully${NC}"
else
    echo -e "${RED}Benchmark source not found${NC}"
fi

# Create a summary of optimizations
echo -e "${YELLOW}=== Optimization Summary ===${NC}"
echo "1. Link Time Optimization (LTO): ENABLED"
echo "2. Architecture-specific builds:"
echo "   - libpoker_lib.*         : -march=native"
echo "   - libpoker_lib_generic.* : -march=x86-64"
echo "   - libpoker_lib_avx2.*    : -march=native -mavx2 -mfma"
echo "   - libpoker_lib_avx512.*  : -march=native -mavx512f"
echo "3. Compiler flags: -O3 -DNDEBUG"

# Show how to run the benchmark
echo -e "${YELLOW}To run the architecture comparison:${NC}"
echo "cd $BUILD_DIR && ./bench_arch_comparison"

# Additional build configurations for testing
echo -e "${YELLOW}=== Additional Build Configurations ===${NC}"

# Profile-guided optimization setup
echo -e "${GREEN}For Profile-Guided Optimization (PGO):${NC}"
cat << 'EOF'
# Step 1: Build with profiling
cmake .. -DCMAKE_C_FLAGS="-fprofile-generate" -DCMAKE_EXE_LINKER_FLAGS="-fprofile-generate"
make

# Step 2: Run representative workloads
./pokenum -h5 -n1000000 -- as ah - ks kh

# Step 3: Rebuild with profile data
cmake .. -DCMAKE_C_FLAGS="-fprofile-use" -DCMAKE_EXE_LINKER_FLAGS="-fprofile-use"
make
EOF

echo -e "${GREEN}Build complete!${NC}"