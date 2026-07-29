#!/bin/bash

# Script principal pour exécuter les différents scripts du projet poker-eval

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Couleurs pour l'affichage
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

show_help() {
    echo "Usage: $0 [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  benchmark    Run benchmark scripts"
    echo "  test         Run test scripts"
    echo "  build        Build the project"
    echo "  clean        Clean build artifacts"
    echo "  help         Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 benchmark gpu         # Run GPU benchmarks"
    echo "  $0 benchmark batched     # Run batched Monte Carlo benchmarks"
    echo "  $0 test joker           # Run joker tests"
    echo "  $0 test gpu             # Validate GPU implementation"
}

run_benchmark() {
    case "$1" in
        gpu)
            echo -e "${BLUE}Running GPU benchmarks...${NC}"
            "$SCRIPT_DIR/benchmarks/benchmark_gpu_comprehensive.sh"
            ;;
        gpu-simple)
            echo -e "${BLUE}Running simple GPU benchmarks...${NC}"
            "$SCRIPT_DIR/benchmarks/benchmark_gpu.sh"
            ;;
        batched)
            echo -e "${BLUE}Running batched Monte Carlo benchmarks...${NC}"
            "$SCRIPT_DIR/benchmarks/benchmark_batched_mc.sh"
            ;;
        simd)
            echo -e "${BLUE}Running SIMD benchmarks...${NC}"
            "$SCRIPT_DIR/benchmarks/benchmark_simd.sh"
            ;;
        simd-cache)
            echo -e "${BLUE}Running batched SIMD vs cache benchmark...${NC}"
            "$SCRIPT_DIR/benchmarks/benchmark_batched_simd_cache.sh"
            ;;
        holdem8)
            echo -e "${BLUE}Running Hold'em8 SIMD benchmark...${NC}"
            "$SCRIPT_DIR/benchmarks/benchmark_holdem8_simd.sh"
            ;;
        variance)
            echo -e "${BLUE}Running variance benchmarks + plots...${NC}"
            "$SCRIPT_DIR/benchmarks/run_variance_bench_and_plot.sh"
            ;;
        postproc)
            echo -e "${BLUE}Running post-processing AVX2 benchmark...${NC}"
            "$SCRIPT_DIR/benchmarks/benchmark_postproc_avx2.sh"
            ;;
        canon7)
            echo -e "${BLUE}Running Hold'em 7-card classes benchmark...${NC}"
            "$SCRIPT_DIR/benchmarks/benchmark_holdem_canon7.sh"
            ;;
        mt)
            echo -e "${BLUE}Profiling MT versions...${NC}"
            "$SCRIPT_DIR/benchmarks/profile_mt_versions.sh"
            ;;
        all)
            echo -e "${BLUE}Running all benchmarks...${NC}"
            "$SCRIPT_DIR/benchmarks/benchmark_batched_mc.sh"
            echo ""
            "$SCRIPT_DIR/benchmarks/benchmark_simd.sh"
            echo ""
            "$SCRIPT_DIR/benchmarks/profile_mt_versions.sh"
            echo ""
            "$SCRIPT_DIR/benchmarks/benchmark_gpu_comprehensive.sh"
            ;;
        *)
            echo "Unknown benchmark: $1"
            echo "Available benchmarks: gpu, gpu-simple, batched, simd, simd-cache, holdem8, variance, postproc, canon7, mt, all"
            exit 1
            ;;
    esac
}

run_test() {
    case "$1" in
        joker)
            echo -e "${BLUE}Running joker tests...${NC}"
            "$SCRIPT_DIR/tests/run_joker_tests.sh"
            ;;
        lowball)
            echo -e "${BLUE}Running lowball tests...${NC}"
            "$SCRIPT_DIR/tests/run_lowball_tests.sh"
            ;;
        gpu)
            echo -e "${BLUE}Validating GPU implementation...${NC}"
            "$SCRIPT_DIR/tests/validate_gpu_implementation.sh"
            ;;
        all)
            echo -e "${BLUE}Running all tests...${NC}"
            cd "$PROJECT_ROOT" && make test
            ;;
        *)
            echo "Unknown test: $1"
            echo "Available tests: joker, lowball, gpu, all"
            exit 1
            ;;
    esac
}

build_project() {
    echo -e "${BLUE}Building project...${NC}"
    cd "$PROJECT_ROOT"
    
    if [ "$1" = "clean" ]; then
        make clean
    fi
    
    make -j$(nproc)
}

clean_project() {
    echo -e "${BLUE}Cleaning project...${NC}"
    cd "$PROJECT_ROOT"
    make clean
    
    # Nettoyer les fichiers temporaires
    find . -name "*.o" -delete
    find . -name "*.a" -delete
    find . -name "*.dSYM" -type d -exec rm -rf {} +
    
    echo -e "${GREEN}Project cleaned${NC}"
}

# Traitement des arguments
case "$1" in
    benchmark)
        run_benchmark "$2"
        ;;
    test)
        run_test "$2"
        ;;
    build)
        build_project "$2"
        ;;
    clean)
        clean_project
        ;;
    help|--help|-h|"")
        show_help
        ;;
    *)
        echo "Unknown command: $1"
        show_help
        exit 1
        ;;
esac
