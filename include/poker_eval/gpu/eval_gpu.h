/*
 * eval_gpu.h: GPU-accelerated poker hand evaluator interface
 *
 * This header defines the common interface for both CUDA and OpenCL implementations
 * of the poker hand evaluator.
 */

#ifndef __EVAL_GPU_H__
#define __EVAL_GPU_H__

#include <stdint.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* GPU game types - extensible for all supported games */
    typedef enum
    {
        GPU_GAME_HOLDEM = 0,
        GPU_GAME_OMAHA = 1,
        GPU_GAME_OMAHA5 = 2,
        GPU_GAME_OMAHA6 = 3,
        GPU_GAME_STUD = 4,
        GPU_GAME_RAZZ = 5,
        GPU_GAME_HOLDEM8 = 6,  /* Hold'em Hi/Lo */
        GPU_GAME_OMAHA8 = 7,   /* Omaha Hi/Lo */
        GPU_GAME_STUD8 = 8,    /* Stud Hi/Lo */
        GPU_GAME_LOWBALL27 = 9 /* 2-7 Lowball */
    } gpu_game_type_t;

    /* GPU game configuration - describes how to evaluate a specific game */
    typedef struct
    {
        gpu_game_type_t game;
        int num_hole_cards;     /* 2 for Hold'em, 4-6 for Omaha, 7 for Stud */
        int num_board_cards;    /* 5 for Hold'em/Omaha, 0 for Stud/Razz */
        int eval_low;           /* 1 for Razz, Omaha8, Stud8; 0 otherwise */
        int split_pot;          /* 1 for hi/lo games; 0 otherwise */
        int low_qualifier;      /* 8 for 8-or-better, 0 for no qualifier */
    } gpu_game_config_t;

    /* GPU evaluation context structure */
    typedef struct
    {
        void *device_memory;
        void *lookup_tables;
        size_t max_batch_size;
        int device_id;
        int backend_type; /* 0 = CUDA, 1 = OpenCL */
        gpu_game_config_t game_config; /* NEW: Game-specific configuration */
    } gpu_eval_context_t;

    /* Result structure for batch evaluation */
    typedef struct
    {
        HandVal *hand_values;
        float *pot_fractions; /* For equity calculations */
        size_t batch_size;
    } gpu_eval_result_t;

    /* Result structure for hi/lo batch evaluation */
    typedef struct
    {
        HandVal *hand_values_hi;    /* High hand values */
        HandVal *hand_values_lo;    /* Low hand values (or NULL if not eval_low) */
        int *lo_qualifies;          /* 1 if low qualifies, 0 otherwise */
        float *pot_fractions_hi;    /* High pot equity */
        float *pot_fractions_lo;    /* Low pot equity */
        size_t batch_size;
    } gpu_eval_result_hilo_t;

    /* Helper functions to create game configurations */
    gpu_game_config_t gpu_game_config_holdem(void);
    gpu_game_config_t gpu_game_config_omaha(int num_hole_cards); /* 4, 5, or 6 */
    gpu_game_config_t gpu_game_config_omaha8(int num_hole_cards);
    gpu_game_config_t gpu_game_config_stud(void);
    gpu_game_config_t gpu_game_config_stud8(void);
    gpu_game_config_t gpu_game_config_razz(void);
    gpu_game_config_t gpu_game_config_holdem8(void);
    gpu_game_config_t gpu_game_config_lowball27(void); /* 2-7 Lowball */

    /* Initialize GPU context (legacy - defaults to Hold'em) */
    gpu_eval_context_t *gpu_eval_init(int device_id, size_t max_batch_size, int backend_type);

    /* Initialize GPU context with game configuration */
    gpu_eval_context_t *gpu_eval_init_game(int device_id, size_t max_batch_size,
                                            int backend_type, gpu_game_config_t game_config);

    /* Cleanup GPU context */
    void gpu_eval_cleanup(gpu_eval_context_t *ctx);

    /* Batch evaluate N boards with multiple hands per board */
    int gpu_eval_batch_boards(
        gpu_eval_context_t *ctx,
        StdDeck_CardMask *boards,     /* Array of board cards */
        StdDeck_CardMask *hole_cards, /* Array of hole cards for each player */
        int n_boards,                 /* Number of boards */
        int n_players,                /* Number of players per board */
        gpu_eval_result_t *result     /* Output results */
    );

    /* Batch evaluate N boards with hi/lo support */
    int gpu_eval_batch_boards_hilo(
        gpu_eval_context_t *ctx,
        StdDeck_CardMask *boards,     /* Array of board cards */
        StdDeck_CardMask *hole_cards, /* Array of hole cards for each player */
        int n_boards,                 /* Number of boards */
        int n_players,                /* Number of players per board */
        gpu_eval_result_hilo_t *result /* Output results with hi/lo */
    );

    /* Monte Carlo simulation for range vs range equity */
    int gpu_monte_carlo_equity(
        gpu_eval_context_t *ctx,
        StdDeck_CardMask *ranges, /* Player ranges */
        int n_players,            /* Number of players */
        int n_simulations,        /* Number of Monte Carlo iterations */
        float *equities           /* Output equity for each player */
    );

    /* Monte Carlo simulation with hi/lo support */
    int gpu_monte_carlo_equity_hilo(
        gpu_eval_context_t *ctx,
        StdDeck_CardMask *ranges, /* Player ranges */
        int n_players,            /* Number of players */
        int n_simulations,        /* Number of Monte Carlo iterations */
        float *equities_hi,       /* Output hi equity for each player */
        float *equities_lo,       /* Output lo equity for each player */
        float *scoop_equity       /* Output scoop equity for each player */
    );

    /* Get device information */
    int gpu_get_device_info(int device_id, char *name, size_t *memory, int *compute_units);

    /* Check if GPU acceleration is available */
    int gpu_is_available(int backend_type);

#ifdef __cplusplus
}
#endif

#endif /* __EVAL_GPU_H__ */
