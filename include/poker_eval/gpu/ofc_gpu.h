/*
 * ofc_gpu.h - Open Face Chinese Poker GPU Acceleration API
 *
 * This header extends the existing GPU infrastructure for OFC-specific
 * high-performance Monte Carlo simulations and batch processing.
 *
 * Based on the successful Phase 1 SIMD implementation (96x speedup),
 * targeting 1000-10000x speedup with GPU acceleration.
 *
 * Copyright (C) 2025
 */

#ifndef __OFC_GPU_H__
#define __OFC_GPU_H__

#include "eval_gpu.h" /* Existing GPU infrastructure */
#include <poker_eval/ofc/ofc.h>
#include <poker_eval/ofc/ofc_simd.h> /* Phase 1 SIMD integration */
#include <poker_eval/core/poker_defs.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ===== OFC GPU Configuration ===== */

/* GPU batch sizes optimized for different GPU tiers */
#define OFC_GPU_BATCH_SIZE_SMALL 1024  /* Entry-level GPU */
#define OFC_GPU_BATCH_SIZE_MEDIUM 4096 /* Mid-tier GPU */
#define OFC_GPU_BATCH_SIZE_LARGE 16384 /* High-end GPU */
#define OFC_GPU_MAX_BATCH_SIZE 32768   /* Maximum supported */

/* Monte Carlo simulation sizes */
#define OFC_GPU_MC_SAMPLES_SMALL 1000   /* Fast analysis */
#define OFC_GPU_MC_SAMPLES_MEDIUM 10000 /* Standard analysis */
#define OFC_GPU_MC_SAMPLES_LARGE 100000 /* Precision analysis */

    /* ===== OFC GPU Data Structures ===== */

    /**
     * OFC GPU backend types
     */
    typedef enum
    {
        OFC_GPU_BACKEND_CUDA = 0, /* NVIDIA CUDA backend */
        OFC_GPU_BACKEND_OPENCL,   /* OpenCL backend (AMD/Intel/NVIDIA) */
        OFC_GPU_BACKEND_AUTO      /* Automatic backend selection */
    } ofc_gpu_backend_t;

    /**
     * OFC GPU processing mode
     * Extends the SIMD modes with GPU and hybrid options
     */
    typedef enum
    {
        OFC_GPU_PROCESS_CPU,    /* CPU baseline fallback */
        OFC_GPU_PROCESS_SIMD,   /* SIMD only (96x proven) */
        OFC_GPU_PROCESS_GPU,    /* GPU only (target 1000x) */
        OFC_GPU_PROCESS_HYBRID, /* SIMD preprocessing + GPU (target 10000x) */
        OFC_GPU_PROCESS_AUTO    /* Automatic selection based on batch size */
    } ofc_gpu_processing_mode_t;

    /**
     * OFC GPU context - extends gpu_eval_context_t for OFC operations
     */
    typedef struct
    {
        gpu_eval_context_t *base_ctx; /* Base GPU context */

        /* OFC-specific GPU memory */
        void *device_partial_hands;   /* GPU buffer for partial hands */
        void *device_completed_hands; /* GPU buffer for completed hands */
        void *device_available_cards; /* GPU buffer for available cards */
        void *device_foul_risks;      /* GPU buffer for results */
        void *device_random_states;   /* GPU RNG states */

        /* Memory management */
        size_t max_ofc_batch_size;    /* Maximum OFC batch size */
        size_t allocated_memory_size; /* Total allocated GPU memory */

        /* Performance counters */
        unsigned long total_simulations; /* Total simulations performed */
        double total_gpu_time;           /* Total GPU processing time */
        double total_transfer_time;      /* Total CPU-GPU transfer time */
    } ofc_gpu_context_t;

    /**
     * OFC GPU batch for massive Monte Carlo processing
     */
    typedef struct
    {
        /* Input data */
        ofc_hand_t partial_hands[OFC_GPU_MAX_BATCH_SIZE]; /* Partial hands to analyze */
        int cards[OFC_GPU_MAX_BATCH_SIZE];                /* Cards to place */
        ofc_position_t positions[OFC_GPU_MAX_BATCH_SIZE]; /* Positions to test */
        int simulations_per_hand;                         /* MC simulations per hand */
        int batch_size;                                   /* Number of hands in batch */

        /* Output results */
        float foul_risks[OFC_GPU_MAX_BATCH_SIZE]; /* Foul risk probabilities */

        /* Processing metadata */
        ofc_gpu_processing_mode_t processing_mode; /* How to process this batch */
        double processing_time_ms;                 /* Time taken to process */
        unsigned long total_simulations_performed; /* Total simulations done */
    } ofc_gpu_batch_t;

    /**
     * OFC GPU evaluation results for batch hand evaluation
     */
    typedef struct
    {
        HandVal top_values[OFC_GPU_MAX_BATCH_SIZE];    /* Top hand values */
        HandVal middle_values[OFC_GPU_MAX_BATCH_SIZE]; /* Middle hand values */
        HandVal bottom_values[OFC_GPU_MAX_BATCH_SIZE]; /* Bottom hand values */
        int hierarchy_valid[OFC_GPU_MAX_BATCH_SIZE];   /* Hierarchy validation */
        int batch_size;                                /* Number of hands evaluated */
        double evaluation_time_ms;                     /* Time taken */
    } ofc_gpu_eval_result_t;

    /**
     * OFC GPU royalties calculation batch
     */
    typedef struct
    {
        ofc_hand_t hands[OFC_GPU_MAX_BATCH_SIZE];          /* Complete hands */
        ofc_royalties_t royalties[OFC_GPU_MAX_BATCH_SIZE]; /* Calculated royalties */
        int batch_size;                                    /* Number of hands */
        double calculation_time_ms;                        /* Time taken */
    } ofc_gpu_royalties_batch_t;

    /* ===== Core OFC GPU API Functions ===== */

    /**
     * Initialize OFC GPU context
     *
     * @param device_id GPU device to use (-1 for auto-select)
     * @param max_batch_size Maximum batch size to support
     * @param backend_type 0=CUDA, 1=OpenCL, -1=auto
     * @return OFC GPU context or NULL on failure
     */
    ofc_gpu_context_t *OFC_GPU_Init(int device_id, size_t max_batch_size, int backend_type);

    /**
     * Cleanup OFC GPU context and free all resources
     *
     * @param ctx OFC GPU context to cleanup
     */
    void OFC_GPU_Cleanup(ofc_gpu_context_t *ctx);

    /**
     * Get optimal batch size for current GPU configuration
     *
     * @param ctx OFC GPU context
     * @return Optimal batch size for best performance
     */
    size_t OFC_GPU_GetOptimalBatchSize(ofc_gpu_context_t *ctx);

    /**
     * Select optimal processing mode based on batch characteristics
     *
     * @param batch_size Number of hands to process
     * @param simulations_per_hand Number of MC simulations per hand
     * @param gpu_available Whether GPU is available
     * @return Recommended processing mode
     */
    ofc_gpu_processing_mode_t OFC_GPU_SelectProcessingMode(
        int batch_size,
        int simulations_per_hand,
        int gpu_available);

    /* ===== High-Performance Monte Carlo Functions ===== */

    /**
     * Calculate foul risk for batch of hands using GPU Monte Carlo
     * This is the main high-performance function targeting 1000x speedup
     *
     * @param ctx OFC GPU context
     * @param batch Batch of hands to analyze
     * @return 0 on success, -1 on error
     */
    int OFC_GPU_CalculateFoulRiskBatch(ofc_gpu_context_t *ctx, ofc_gpu_batch_t *batch);

    /**
     * Hybrid SIMD + GPU foul risk calculation for maximum performance
     * Combines Phase 1 SIMD (96x) with GPU for 10000x+ theoretical speedup
     *
     * @param partial_hands Array of partial hands
     * @param cards Array of cards to test
     * @param positions Array of positions to test
     * @param batch_size Number of hands
     * @param simulations_per_hand MC simulations per hand
     * @param foul_risks Output array of foul risk probabilities
     * @return 0 on success, -1 on error
     */
    int OFC_GPU_CalculateFoulRiskHybrid(
        const ofc_hand_t *partial_hands,
        const int *cards,
        const ofc_position_t *positions,
        int batch_size,
        int simulations_per_hand,
        float *foul_risks);

    /**
     * Single hand foul risk with automatic GPU/SIMD selection
     * Automatically chooses best method based on simulation count
     *
     * @param partial_hand Partial hand to analyze
     * @param card Card to place
     * @param position Position to test
     * @param simulations Number of MC simulations
     * @return Foul risk probability (0.0-1.0)
     */
    float OFC_GPU_CalculateFoulRiskAdaptive(
        const ofc_hand_t *partial_hand,
        int card,
        ofc_position_t position,
        int simulations);

    /* ===== Batch Hand Evaluation Functions ===== */

    /**
     * Evaluate multiple complete OFC hands using GPU parallelization
     *
     * @param ctx OFC GPU context
     * @param hands Array of complete OFC hands
     * @param batch_size Number of hands to evaluate
     * @param result Output evaluation results
     * @return 0 on success, -1 on error
     */
    int OFC_GPU_EvaluateHandsBatch(
        ofc_gpu_context_t *ctx,
        const ofc_hand_t *hands,
        int batch_size,
        ofc_gpu_eval_result_t *result);

    /**
     * Validate hierarchy for multiple hands using GPU
     *
     * @param ctx OFC GPU context
     * @param hands Array of complete OFC hands
     * @param batch_size Number of hands
     * @param valid_flags Output array (1=valid, 0=foul)
     * @return 0 on success, -1 on error
     */
    int OFC_GPU_ValidateHierarchyBatch(
        ofc_gpu_context_t *ctx,
        const ofc_hand_t *hands,
        int batch_size,
        int *valid_flags);

    /* ===== Royalties Calculation Functions ===== */

    /**
     * Calculate royalties for multiple hands using GPU
     *
     * @param ctx OFC GPU context
     * @param batch Royalties calculation batch
     * @return 0 on success, -1 on error
     */
    int OFC_GPU_CalculateRoyaltiesBatch(
        ofc_gpu_context_t *ctx,
        ofc_gpu_royalties_batch_t *batch);

    /* ===== Utility and Information Functions ===== */

    /**
     * Check if OFC GPU acceleration is available
     *
     * @param backend_type 0=CUDA, 1=OpenCL, -1=any
     * @return 1 if available, 0 if not
     */
    int OFC_GPU_IsAvailable(int backend_type);

    /**
     * Get detailed GPU device information
     *
     * @param device_id Device to query
     * @param name Output buffer for device name
     * @param name_size Size of name buffer
     * @param memory_mb Output for memory size in MB
     * @param compute_units Output for compute units count
     * @param max_batch_size Output for recommended max batch size
     * @return 0 on success, -1 on error
     */
    int OFC_GPU_GetDeviceInfo(
        int device_id,
        char *name,
        size_t name_size,
        size_t *memory_mb,
        int *compute_units,
        size_t *max_batch_size);

    /**
     * Get OFC GPU performance statistics
     *
     * @param ctx OFC GPU context
     * @param total_simulations Output for total simulations performed
     * @param avg_gpu_time_ms Output for average GPU processing time
     * @param avg_transfer_time_ms Output for average transfer time
     * @param efficiency_ratio Output for GPU efficiency ratio
     * @return 0 on success, -1 on error
     */
    int OFC_GPU_GetPerformanceStats(
        ofc_gpu_context_t *ctx,
        unsigned long *total_simulations,
        double *avg_gpu_time_ms,
        double *avg_transfer_time_ms,
        double *efficiency_ratio);

    /**
     * Get processing mode name for debugging/logging
     *
     * @param mode Processing mode
     * @return String name of the mode
     */
    const char *OFC_GPU_GetProcessingModeName(ofc_gpu_processing_mode_t mode);

    /* ===== GPU Kernel Management (Week 2) ===== */

    /**
     * Initialize GPU kernels (Monte Carlo and evaluation)
     * backend_type: OFC_GPU_BACKEND_CUDA, OFC_GPU_BACKEND_OPENCL, or OFC_GPU_BACKEND_AUTO
     * Returns 0 on success, -1 on failure
     */
    int OFC_GPU_InitializeKernels(int backend_type);

    /**
     * Clean up GPU kernel resources
     */
    void OFC_GPU_CleanupKernels(void);

    /**
     * Get GPU kernel status
     * Returns: 0 = no GPU, 1 = CUDA available, 2 = OpenCL available
     */
    int OFC_GPU_GetKernelStatus(void);

    /**
     * Execute Monte Carlo kernel
     */
    int OFC_GPU_ExecuteMonteCarloKernel(
        ofc_gpu_context_t *ctx,
        const ofc_hand_t *partial_hands,
        const int *cards,
        const ofc_position_t *positions,
        int batch_size,
        int simulations_per_hand,
        float *foul_risks);

    /**
     * Execute hand evaluation kernel
     */
    int OFC_GPU_ExecuteEvaluationKernel(
        ofc_gpu_context_t *ctx,
        const ofc_hand_t *hands,
        int batch_size,
        int *hand_strengths,
        int *hierarchy_valid);

    /**
     * Print kernel performance statistics
     */
    void OFC_GPU_PrintKernelPerformance(ofc_gpu_context_t *ctx);

    /* ===== Week 3 GPU Memory Management ===== */

    /**
     * Allocate GPU memory for batch processing
     * max_batch_size: Maximum number of hands to process in one batch
     * backend_type: GPU backend (CUDA/OpenCL/Auto)
     * Returns 0 on success, -1 on failure
     */
    int OFC_GPU_AllocateMemory(size_t max_batch_size, int backend_type);

    /**
     * Free all allocated GPU memory
     */
    void OFC_GPU_FreeMemory(void);

    /**
     * Transfer batch data from CPU to GPU
     */
    int OFC_GPU_TransferToDevice(const ofc_gpu_batch_t *batch);

    /**
     * Transfer results from GPU to CPU
     */
    int OFC_GPU_TransferFromDevice(ofc_gpu_batch_t *batch);

    /**
     * Get total allocated GPU memory size
     */
    size_t OFC_GPU_GetAllocatedMemorySize(void);

    /**
     * Print GPU memory usage statistics
     */
    void OFC_GPU_PrintMemoryUsage(void);

    /**
     * Validate GPU memory accessibility
     * Returns: 1 if valid, 0 if no memory, -1 if error
     */
    int OFC_GPU_ValidateMemory(void);

    /* ===== Week 3 GPU Kernel Compilation ===== */

    /**
     * Compile GPU kernels from source
     * backend_type: GPU backend (CUDA/OpenCL/Auto)
     * Returns 0 on success, -1 on failure
     */
    int OFC_GPU_CompileKernels(int backend_type);

    /**
     * Unload compiled kernels
     */
    void OFC_GPU_UnloadKernels(void);

    /**
     * Get kernel handles for direct execution
     */
    int OFC_GPU_GetKernelHandles(void **monte_carlo_kernel, void **evaluation_kernel);

    /**
     * Print kernel compilation information
     */
    void OFC_GPU_PrintCompilationInfo(void);

    /**
     * Validate compiled kernels
     * Returns: 1 if valid, 0 if no kernels, -1 if error
     */
    int OFC_GPU_ValidateKernels(void);

    /* ===== Week 4 Production GPU Interface ===== */

    /**
     * Initialize production-grade GPU system with hardware optimization
     * backend_preference: Preferred GPU backend (CUDA/OpenCL/Auto)
     * Returns 0 on success, -1 on failure
     */
    int OFC_GPU_InitializeProduction(int backend_preference);

    /**
     * Shutdown production GPU system with performance reporting
     */
    void OFC_GPU_ShutdownProduction(void);

    /**
     * Process batch with production optimizations
     * - Hardware-specific tuning
     * - Adaptive batch sizing
     * - Performance monitoring
     * - Auto-tuning parameters
     */
    int OFC_GPU_ProcessBatchProduction(ofc_gpu_batch_t *batch);

    /**
     * Get optimal batch size for current hardware
     * Returns hardware-optimized batch size
     */
    size_t OFC_GPU_GetOptimalBatchSizeProduction(void);

    /**
     * Get peak throughput achieved on current hardware
     * Returns simulations per second
     */
    double OFC_GPU_GetPeakThroughput(void);

    /**
     * Print comprehensive production statistics
     * - Hardware information
     * - Optimization parameters
     * - Performance metrics
     */
    void OFC_GPU_PrintProductionStats(void);

/* Production GPU constants */
#define OFC_GPU_PRODUCTION_MIN_BATCH 64                 /* Minimum efficient batch */
#define OFC_GPU_PRODUCTION_TARGET_THROUGHPUT 1000000000 /* 1B sims/sec target */
#define OFC_GPU_PRODUCTION_AUTO_TUNE_THRESHOLD 0.8      /* Tune if < 80% peak */

    /* ===== Benchmarking and Testing Functions ===== */

    /**
     * Comprehensive OFC GPU benchmark suite
     * Tests CPU vs SIMD vs GPU vs Hybrid performance
     */
    void OFC_GPU_RunBenchmarkSuite(void);

    /**
     * Validate GPU results against CPU reference
     *
     * @param ctx OFC GPU context
     * @param test_cases Number of test cases to validate
     * @param tolerance Acceptable difference tolerance
     * @return Number of test cases that passed
     */
    int OFC_GPU_ValidateResults(ofc_gpu_context_t *ctx, int test_cases, float tolerance);

#ifdef __cplusplus
}
#endif

#endif /* __OFC_GPU_H__ */
