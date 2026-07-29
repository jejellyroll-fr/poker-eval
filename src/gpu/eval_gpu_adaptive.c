/*
 * eval_gpu_adaptive.c: Adaptive GPU/CPU evaluation implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poker_eval/gpu/eval_gpu_adaptive.h>
#include <poker_eval/core/eval.h>

/* Adaptive batch evaluation */
int adaptive_eval_batch_boards(
    gpu_eval_context_t* gpu_ctx,
    StdDeck_CardMask* boards,
    StdDeck_CardMask* hole_cards,
    int n_boards,
    int n_players,
    gpu_eval_result_t* result
) {
    int total_evals = n_boards * n_players;
    
    /* Use CPU for small batches to avoid GPU overhead */
    if (total_evals < GPU_BATCH_THRESHOLD_MIN || !gpu_ctx) {
        /* CPU evaluation */
        if (!result->hand_values) {
            result->hand_values = (HandVal*)malloc(total_evals * sizeof(HandVal));
            if (!result->hand_values) return -1;
        }
        
        for (int i = 0; i < n_boards; i++) {
            for (int j = 0; j < n_players; j++) {
                StdDeck_CardMask combined;
                StdDeck_CardMask_OR(combined, boards[i], hole_cards[i * n_players + j]);
                result->hand_values[i * n_players + j] = StdDeck_StdRules_EVAL_N(combined, 7);
            }
        }
        
        result->batch_size = total_evals;
        return 0;
    } else {
        /* GPU evaluation for large batches */
        return gpu_eval_batch_boards(gpu_ctx, boards, hole_cards, n_boards, n_players, result);
    }
}

/* Adaptive Monte Carlo */
int adaptive_monte_carlo_equity(
    gpu_eval_context_t* gpu_ctx,
    StdDeck_CardMask* ranges,
    int n_players,
    int n_simulations,
    float* equities
) {
    /* Use CPU for small simulations */
    if (n_simulations < GPU_MONTE_CARLO_THRESHOLD || !gpu_ctx) {
        /* CPU Monte Carlo */
        int* wins = calloc(n_players, sizeof(int));
        int* ties = calloc(n_players, sizeof(int));
        if (!wins || !ties) {
            free(wins);
            free(ties);
            return -1;
        }
        
        for (int sim = 0; sim < n_simulations; sim++) {
            /* Generate random hands for each player */
            StdDeck_CardMask player_hands[10];
            StdDeck_CardMask used_cards;
            StdDeck_CardMask_RESET(used_cards);
            
            /* Deal hole cards */
            for (int p = 0; p < n_players && p < 10; p++) {
                StdDeck_CardMask_RESET(player_hands[p]);
                for (int c = 0; c < 2; c++) {
                    int card;
                    StdDeck_CardMask card_mask;
                    do {
                        card = rand() % 52;
                        card_mask = StdDeck_MASK(card);
                    } while (StdDeck_CardMask_ANY_SET(used_cards, card_mask));
                    
                    StdDeck_CardMask_OR(player_hands[p], player_hands[p], card_mask);
                    StdDeck_CardMask_OR(used_cards, used_cards, card_mask);
                }
            }
            
            /* Deal board */
            StdDeck_CardMask board;
            StdDeck_CardMask_RESET(board);
            for (int c = 0; c < 5; c++) {
                int card;
                StdDeck_CardMask card_mask;
                do {
                    card = rand() % 52;
                    card_mask = StdDeck_MASK(card);
                } while (StdDeck_CardMask_ANY_SET(used_cards, card_mask));
                
                StdDeck_CardMask_OR(board, board, card_mask);
                StdDeck_CardMask_OR(used_cards, used_cards, card_mask);
            }
            
            /* Evaluate all hands */
            HandVal hand_values[10];
            for (int p = 0; p < n_players && p < 10; p++) {
                StdDeck_CardMask combined;
                StdDeck_CardMask_OR(combined, player_hands[p], board);
                hand_values[p] = StdDeck_StdRules_EVAL_N(combined, 7);
            }
            
            /* Find winner(s) */
            HandVal best_hand = 0;
            int winner_count = 0;
            
            for (int p = 0; p < n_players; p++) {
                if (hand_values[p] > best_hand) {
                    best_hand = hand_values[p];
                    winner_count = 1;
                } else if (hand_values[p] == best_hand) {
                    winner_count++;
                }
            }
            
            /* Update win/tie counts */
            for (int p = 0; p < n_players; p++) {
                if (hand_values[p] == best_hand) {
                    if (winner_count == 1) {
                        wins[p]++;
                    } else {
                        ties[p]++;
                    }
                }
            }
        }
        
        /* Calculate equities */
        for (int p = 0; p < n_players; p++) {
            equities[p] = (float)(wins[p] + ties[p] * 0.5) / (float)n_simulations;
        }
        
        free(wins);
        free(ties);
        return 0;
    } else {
        /* GPU Monte Carlo for large simulations */
        return gpu_monte_carlo_equity(gpu_ctx, ranges, n_players, n_simulations, equities);
    }
}

/* Get optimal batch size */
int get_optimal_batch_size(gpu_eval_context_t* ctx, int n_players) {
    if (!ctx) return 1000; /* Default CPU batch size */
    
    /* Estimate optimal batch size based on GPU memory and compute units */
    char device_name[256];
    size_t device_memory;
    int compute_units;
    
    if (gpu_get_device_info(ctx->device_id, device_name, &device_memory, &compute_units) == 0) {
        /* Calculate optimal batch size based on available memory and compute units */
        size_t memory_per_eval = sizeof(StdDeck_CardMask) * (1 + n_players) + sizeof(HandVal);
        size_t max_batch_by_memory = (size_t)((double)device_memory * 0.8) / memory_per_eval; /* Use 80% of memory */
        
        /* Aim for compute_units * 32 threads per compute unit */
        size_t optimal_by_compute = compute_units * 32;
        
        /* Take the minimum and ensure it's at least the threshold */
        size_t optimal = (max_batch_by_memory < optimal_by_compute) ? max_batch_by_memory : optimal_by_compute;
        
        if (optimal < GPU_BATCH_THRESHOLD_MIN) {
            optimal = GPU_BATCH_THRESHOLD_MIN;
        }
        
        return (int)optimal;
    }
    
    return GPU_BATCH_THRESHOLD_MIN;
}

/* Profile performance */
int profile_gpu_performance(gpu_eval_context_t* ctx, gpu_performance_profile_t* profile) {
    if (!ctx || !profile) return -1;
    
    const int test_sizes[] = {100, 500, 1000, 2000, 5000};
    #define N_TEST_SIZES 5
    const int n_players = 2;
    
    printf("Profiling GPU vs CPU performance...\n");
    
    double cpu_times[N_TEST_SIZES];
    double gpu_times[N_TEST_SIZES];
    
    for (int t = 0; t < N_TEST_SIZES; t++) {
        int n_boards = test_sizes[t];
        
        /* Generate test data */
        StdDeck_CardMask* boards = malloc(n_boards * sizeof(StdDeck_CardMask));
        StdDeck_CardMask* hole_cards = malloc(n_boards * n_players * sizeof(StdDeck_CardMask));
        
        for (int i = 0; i < n_boards; i++) {
            StdDeck_CardMask_RESET(boards[i]);
            for (int j = 0; j < 5; j++) {
                StdDeck_CardMask_SET(boards[i], rand() % 52);
            }
            
            for (int j = 0; j < n_players; j++) {
                StdDeck_CardMask_RESET(hole_cards[i * n_players + j]);
                for (int k = 0; k < 2; k++) {
                    StdDeck_CardMask_SET(hole_cards[i * n_players + j], rand() % 52);
                }
            }
        }
        
        /* CPU timing */
        clock_t cpu_start = clock();
        for (int i = 0; i < n_boards; i++) {
            for (int j = 0; j < n_players; j++) {
                StdDeck_CardMask combined;
                StdDeck_CardMask_OR(combined, boards[i], hole_cards[i * n_players + j]);
                StdDeck_StdRules_EVAL_N(combined, 7);
            }
        }
        clock_t cpu_end = clock();
        cpu_times[t] = ((double)(cpu_end - cpu_start)) / CLOCKS_PER_SEC;
        
        /* GPU timing */
        clock_t gpu_start = clock();
        gpu_eval_result_t result = {0};
        gpu_eval_batch_boards(ctx, boards, hole_cards, n_boards, n_players, &result);
        clock_t gpu_end = clock();
        gpu_times[t] = ((double)(gpu_end - gpu_start)) / CLOCKS_PER_SEC;
        
        printf("Size %d: CPU=%.4fs, GPU=%.4fs, Ratio=%.2fx\n", 
               n_boards, cpu_times[t], gpu_times[t], cpu_times[t] / gpu_times[t]);
        
        if (result.hand_values) free(result.hand_values);
        free(boards);
        free(hole_cards);
    }
    
    /* Calculate averages */
    profile->cpu_time_per_eval = 0;
    profile->gpu_time_per_eval = 0;
    
    for (int t = 0; t < N_TEST_SIZES; t++) {
        profile->cpu_time_per_eval += cpu_times[t] / (test_sizes[t] * n_players);
        profile->gpu_time_per_eval += gpu_times[t] / (test_sizes[t] * n_players);
    }
    
    profile->cpu_time_per_eval /= N_TEST_SIZES;
    profile->gpu_time_per_eval /= N_TEST_SIZES;
    profile->gpu_setup_overhead = gpu_times[0] - (profile->gpu_time_per_eval * test_sizes[0] * n_players);
    
    /* Find optimal threshold */
    profile->optimal_threshold = GPU_BATCH_THRESHOLD_MIN;
    for (int size = 100; size <= 10000; size += 100) {
        double cpu_time = profile->cpu_time_per_eval * size * n_players;
        double gpu_time = profile->gpu_setup_overhead + profile->gpu_time_per_eval * size * n_players;
        
        if (gpu_time < cpu_time) {
            profile->optimal_threshold = size;
            break;
        }
    }
    
    printf("\nProfile Results:\n");
    printf("CPU time per eval: %.6f seconds\n", profile->cpu_time_per_eval);
    printf("GPU time per eval: %.6f seconds\n", profile->gpu_time_per_eval);
    printf("GPU setup overhead: %.6f seconds\n", profile->gpu_setup_overhead);
    printf("Optimal threshold: %d evaluations\n", profile->optimal_threshold);
    
    return 0;
}
