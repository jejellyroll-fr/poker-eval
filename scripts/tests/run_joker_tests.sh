#!/bin/bash

# Script to compile and run all joker tests

echo "=== Compiling and Running Joker Tests ==="
echo

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Base directory
BASE_DIR="/Users/jdenis/Documents/GitHub/poker-eval"
BUILD_DIR="$BASE_DIR/build"
TESTS_DIR="$BASE_DIR/tests"

# Counter for test results
total_tests=0
passed_tests=0
failed_tests=0

# Function to compile and run a test
run_test() {
    local test_name=$1
    local test_file="$TESTS_DIR/$test_name.c"
    local test_binary="$BUILD_DIR/$test_name"
    
    ((total_tests++))
    
    echo -e "${YELLOW}Compiling $test_name...${NC}"
    
    # Compile the test
    if gcc -I"$BASE_DIR/include" -L"$BUILD_DIR" -o "$test_binary" "$test_file" -lpoker_lib -lm 2>/dev/null; then
        echo -e "${GREEN}✓ Compilation successful${NC}"
        
        # Run the test
        echo "Running $test_name..."
        if "$test_binary"; then
            echo -e "${GREEN}✓ Test passed${NC}"
            ((passed_tests++))
        else
            echo -e "${RED}✗ Test failed${NC}"
            ((failed_tests++))
        fi
    else
        echo -e "${RED}✗ Compilation failed${NC}"
        ((failed_tests++))
    fi
    
    echo
}

# List of tests to run
tests=(
    "test_joker_support"
    "test_joker_mask_ops"
    "test_joker_conversion"
    "test_joker_multiplayer"
    "test_joker_games"
    "test_joker_hands"
)

# Run each test
for test in "${tests[@]}"; do
    if [ -f "$TESTS_DIR/$test.c" ]; then
        run_test "$test"
    else
        echo -e "${RED}✗ Test file not found: $test.c${NC}"
        ((failed_tests++))
        ((total_tests++))
    fi
done

# Summary
echo "======================================"
echo "TEST SUMMARY"
echo "======================================"
echo -e "Total tests: $total_tests"
echo -e "${GREEN}Passed: $passed_tests${NC}"
echo -e "${RED}Failed: $failed_tests${NC}"

if [ $failed_tests -eq 0 ]; then
    echo -e "\n${GREEN}✅ All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}❌ Some tests failed${NC}"
    exit 1
fi