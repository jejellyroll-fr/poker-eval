# OFC module (Open Face Chinese Poker)

set(OFC_SOURCES
    ofc.c
    ofc_simd.c
    ofc_monte_carlo_simd.c
    ofc_adaptive.c
    solver/ismcts.c
    policy/ofc_rollout_heuristics.c
)

add_library(poker_ofc STATIC ${OFC_SOURCES})

target_include_directories(poker_ofc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(poker_ofc PRIVATE poker_engine poker_utils poker_core)

# SIMD selection for OFC
if(ENABLE_OFC_SIMD AND (HAS_AVX512 OR HAS_AVX2 OR HAS_SSE2))
    # SIMD operations are included in the main sources
    if(HAS_AVX512)
        set_source_files_properties(ofc_simd.c PROPERTIES COMPILE_OPTIONS "-mavx512f")
    elseif(HAS_AVX2)
        set_source_files_properties(ofc_simd.c PROPERTIES COMPILE_OPTIONS "-mavx2")
    elseif(HAS_SSE2)
        set_source_files_properties(ofc_simd.c PROPERTIES COMPILE_OPTIONS "-msse2")
    endif()
endif()

install(TARGETS poker_ofc
        EXPORT poker-eval-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)

