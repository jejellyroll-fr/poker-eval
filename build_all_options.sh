#!/bin/bash

set -e

OPTIONS=(
    "BUILD_SHARED_LIBS"
    "BUILD_STATIC_LIBS"
    "BUILD_EXAMPLES"
    "BUILD_TESTS"
    "BUILD_BINDINGS"
    "BUILD_GPU"
    "ENABLE_LTO"
    "ENABLE_COVERAGE"
    "ENABLE_SANITIZERS"
)

mkdir -p build_logs

for option in "${OPTIONS[@]}"; do
    ( # Start a subshell
        echo "#################################################################"
        echo "## Testing build with $option=ON"
        echo "#################################################################"
        
        rm -rf build
        mkdir build
        cd build

        cmake_args="-DCMAKE_BUILD_TYPE=Release"
        for opt in "${OPTIONS[@]}"; do
            if [ "$opt" == "$option" ]; then
                cmake_args="$cmake_args -D$opt=ON"
            else
                cmake_args="$cmake_args -D$opt=OFF"
            fi
        done

        echo "Running: cmake .. $cmake_args" > ../build_logs/$option.log
        cmake .. $cmake_args >> ../build_logs/$option.log 2>&1

        echo "Running: make -j4" >> ../build_logs/$option.log
        make -j4 >> ../build_logs/$option.log 2>&1
    ) # End of subshell
done

echo "All builds completed. Logs are in the build_logs directory."
