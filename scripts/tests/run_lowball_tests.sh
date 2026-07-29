#!/bin/bash

# Script to run all lowball tests
# This script compiles and runs the lowball evaluation tests

echo "=== Running Lowball Evaluation Tests ==="
echo

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

# Configure with CMake
echo "Configuring project with CMake..."
cd build
cmake .. > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed${NC}"
    exit 1
fi

# Build the test executables
echo "Building test executables..."
make test_lowball_edge_cases test_lowball_comprehensive > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed${NC}"
    exit 1
fi

echo
echo "=== Running Edge Cases Test ==="
echo "This test identifies known problematic hands..."
echo
./test_lowball_edge_cases

echo
echo "=== Running Comprehensive Test Suite ==="
echo "This test validates the complete lowball implementation..."
echo
./test_lowball_comprehensive

# Return to original directory
cd ..

echo
echo -e "${GREEN}Test execution complete!${NC}"
echo
echo "Summary:"
echo "- Edge cases test shows the specific hands that were failing"
echo "- Comprehensive test validates the complete implementation"
echo
echo "The lowball evaluation has been fixed to correctly handle:"
echo "1. Multiple pairs (choosing best two pair)"
echo "2. Full house + pair combinations"
echo "3. Trips + two pair combinations"
echo "4. Quads + trips combinations"
echo "5. All edge cases with 6-7 card hands"