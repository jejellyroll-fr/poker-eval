/**
 * @file ismcts_poker.h
 * @brief Information Set Monte Carlo Tree Search for general poker games
 *
 * Generic ISMCTS implementation for imperfect information poker games.
 * Supports Hold'em, Omaha, and other variants with hidden information.
 *
 * Key features:
 * - K-determinizations for opponent hand sampling
 * - UCB1 action selection with tunable exploration
 * - Efficient node pooling and memory management
 * - Support for multi-way pots
 *
 * Reference:
 * - Cowling et al. (2012): "Information Set Monte Carlo Tree Search"
 * - Ponsen et al. (2011): "Monte-Carlo Tree Search for Poker"
 */

#ifndef POKER_EVAL_ENGINE_SOLVERS_ISMCTS_POKER_H
#define POKER_EVAL_ENGINE_SOLVERS_ISMCTS_POKER_H

#include <poker_eval/core/pokereval_export.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Constants ===== */

#define ISMCTS_MAX_PLAYERS 10
#define ISMCTS_MAX_ACTIONS 8         /* fold, check, call, bet sizes... */
#define ISMCTS_MAX_ROUNDS  4         /* preflop, flop, turn, river */
#define ISMCTS_MAX_CARDS   7         /* Max cards per player (Omaha 4 hole + 5 board) */

/* Default configuration values */
#define ISMCTS_DEFAULT_UCT_C            1.414   /* sqrt(2) */
#define ISMCTS_DEFAULT_ITERATIONS       10000
#define ISMCTS_DEFAULT_DETERMINIZATIONS 8
#define ISMCTS_DEFAULT_EPSILON          0.1     /* epsilon-greedy exploration */

/* ===== Game State ===== */

/** Poker action types */
typedef enum {
    ISMCTS_ACTION_FOLD = 0,
    ISMCTS_ACTION_CHECK,
    ISMCTS_ACTION_CALL,
    ISMCTS_ACTION_BET,           /* min bet or specific size */
    ISMCTS_ACTION_RAISE,         /* min raise or specific size */
    ISMCTS_ACTION_ALL_IN
} ismcts_action_type_t;

/** A single action */
typedef struct {
    ismcts_action_type_t type;
    int amount;                  /* Bet/raise amount (0 for fold/check/call) */
} ismcts_action_t;

/** Game state representation */
typedef struct {
    /* Betting state */
    int pot;                     /* Total pot size */
    int stacks[ISMCTS_MAX_PLAYERS];  /* Player stacks */
    int invested[ISMCTS_MAX_PLAYERS]; /* Amount invested this round */
    int to_call;                 /* Amount to call for current player */
    
    /* Round/position state */
    int current_player;          /* Who's turn to act (0-based) */
    int num_players;             /* Total players in hand */
    int num_active;              /* Players still in hand (not folded) */
    bool folded[ISMCTS_MAX_PLAYERS];  /* True if player has folded */
    bool all_in[ISMCTS_MAX_PLAYERS];  /* True if player is all-in */
    
    int round;                   /* 0=preflop, 1=flop, 2=turn, 3=river */
    int first_to_act;            /* First player to act this round */
    int last_aggressor;          /* Last player to bet/raise */
    int actions_this_round;      /* Number of actions taken this round */
    
    /* Cards (represented as card indices 0-51) */
    int hole_cards[ISMCTS_MAX_PLAYERS][ISMCTS_MAX_CARDS];
    int num_hole_cards[ISMCTS_MAX_PLAYERS];
    int board[5];
    int num_board_cards;
    
    /* Deck state (bitmask of available cards) */
    uint64_t deck_mask;          /* Cards still in deck */
    
    /* Terminal state */
    bool is_terminal;            /* True if hand is over */
    double payoffs[ISMCTS_MAX_PLAYERS]; /* Final payoffs (if terminal) */
} ismcts_game_state_t;

/* ===== Game Interface ===== */

/**
 * Game interface - callbacks for game-specific logic
 * 
 * This allows ISMCTS to work with different poker variants.
 */
typedef struct ismcts_game_interface_t {
    /** Get legal actions at current state */
    int (*get_actions)(
        const ismcts_game_state_t* state,
        ismcts_action_t* out_actions,
        int max_actions,
        void* user_data
    );
    
    /** Apply action to state (returns new state) */
    void (*apply_action)(
        ismcts_game_state_t* state,
        const ismcts_action_t* action,
        void* user_data
    );
    
    /** Check if state is terminal */
    bool (*is_terminal)(
        const ismcts_game_state_t* state,
        void* user_data
    );
    
    /** Compute payoffs at terminal state */
    void (*get_payoffs)(
        const ismcts_game_state_t* state,
        double* out_payoffs,
        void* user_data
    );
    
    /** Sample opponent hole cards (determinization) */
    void (*sample_opponent_cards)(
        ismcts_game_state_t* state,
        int viewer_player,
        uint64_t* rng_state,
        void* user_data
    );
    
    /** Clone a game state */
    void (*clone_state)(
        ismcts_game_state_t* dst,
        const ismcts_game_state_t* src,
        void* user_data
    );
    
    /** User data passed to all callbacks */
    void* user_data;
} ismcts_game_interface_t;

/* ===== ISMCTS Node ===== */

/** ISMCTS tree node */
typedef struct ismcts_node_t {
    /* Statistics */
    double total_reward[ISMCTS_MAX_PLAYERS];  /* Accumulated reward per player */
    int visit_count;                          /* Number of visits */
    int availability_count;                   /* Times node was available */
    
    /* Action that led to this node */
    ismcts_action_t action;
    
    /* Tree structure */
    struct ismcts_node_t* parent;
    struct ismcts_node_t** children;
    int num_children;
    int children_capacity;
    
    /* State info */
    int acting_player;           /* Player who acted to reach this node */
    bool is_expanded;            /* True if children have been generated */
} ismcts_node_t;

/* ===== ISMCTS Configuration ===== */

/** ISMCTS solver configuration */
typedef struct {
    double uct_c;                /* UCT exploration constant */
    int max_iterations;          /* Max iterations per search */
    int num_determinizations;    /* K determinizations */
    double epsilon;              /* Epsilon-greedy exploration */
    bool use_rave;               /* Use RAVE (Rapid Action Value Estimation) */
    double rave_beta;            /* RAVE mixing parameter */
    uint64_t random_seed;        /* RNG seed */
    int max_depth;               /* Max search depth */
} ismcts_config_t;

/* ===== ISMCTS Solver ===== */

/** ISMCTS solver state */
typedef struct {
    ismcts_config_t config;
    ismcts_game_interface_t* game;
    
    /* Search tree */
    ismcts_node_t* root;
    
    /* Node pool for efficient allocation */
    ismcts_node_t* node_pool;
    int pool_size;
    int pool_used;
    
    /* RNG state */
    uint64_t rng_state;
    
    /* Statistics */
    uint64_t total_iterations;
    uint64_t total_simulations;
    uint64_t total_nodes_created;
    int live_children_arrays;    /* Heap-allocated children arrays not yet freed */
} ismcts_solver_t;

/* ===== Public API ===== */

/**
 * Create default ISMCTS configuration
 */
POKEREVAL_EXPORT ismcts_config_t ismcts_poker_default_config(void);

/**
 * Create ISMCTS solver
 *
 * @param config Configuration (NULL for defaults)
 * @param game   Game interface with callbacks
 * @return Solver instance or NULL on failure
 */
POKEREVAL_EXPORT ismcts_solver_t* ismcts_poker_create(
    const ismcts_config_t* config,
    ismcts_game_interface_t* game
);

/**
 * Free ISMCTS solver
 */
POKEREVAL_EXPORT void ismcts_poker_free(ismcts_solver_t* solver);

/**
 * Reset solver for new search
 */
POKEREVAL_EXPORT void ismcts_poker_reset(ismcts_solver_t* solver);

/**
 * Find best action using ISMCTS
 *
 * @param solver      ISMCTS solver
 * @param state       Current game state (from hero's perspective)
 * @param hero_player Hero's player index
 * @param out_action  Best action (output)
 * @return 0 on success, non-zero on error
 */
POKEREVAL_EXPORT int ismcts_poker_search(
    ismcts_solver_t* solver,
    const ismcts_game_state_t* state,
    int hero_player,
    ismcts_action_t* out_action
);

/**
 * Get action probabilities from search
 *
 * @param solver       ISMCTS solver
 * @param out_actions  Array of actions (output)
 * @param out_probs    Array of probabilities (output)
 * @param max_actions  Maximum number of actions
 * @return Number of actions, or -1 on error
 */
POKEREVAL_EXPORT int ismcts_poker_get_action_probs(
    const ismcts_solver_t* solver,
    ismcts_action_t* out_actions,
    double* out_probs,
    int max_actions
);

/**
 * Get search statistics
 */
POKEREVAL_EXPORT void ismcts_poker_get_stats(
    const ismcts_solver_t* solver,
    uint64_t* out_iterations,
    uint64_t* out_simulations,
    uint64_t* out_nodes
);

/* ===== Built-in Game Implementations ===== */

/**
 * Create Hold'em game interface
 *
 * @param num_players Number of players (2-10)
 * @return Game interface or NULL on failure
 */
POKEREVAL_EXPORT ismcts_game_interface_t* ismcts_holdem_create(int num_players);

/**
 * Create Omaha game interface
 *
 * @param num_players   Number of players (2-10)
 * @param num_hole_cards Number of hole cards (4 for PLO, 5 for PLO5, 6 for PLO6)
 * @param pot_limit     True for pot-limit, false for no-limit
 * @return Game interface or NULL on failure
 */
POKEREVAL_EXPORT ismcts_game_interface_t* ismcts_omaha_create(
    int num_players,
    int num_hole_cards,
    bool pot_limit
);

/**
 * Free game interface
 */
POKEREVAL_EXPORT void ismcts_game_interface_free(ismcts_game_interface_t* game);

/* ===== Utility Functions ===== */

/**
 * Initialize game state for new hand
 */
POKEREVAL_EXPORT void ismcts_state_init(
    ismcts_game_state_t* state,
    int num_players,
    const int* starting_stacks,
    int small_blind,
    int big_blind
);

/**
 * Set hole cards for a player
 */
POKEREVAL_EXPORT void ismcts_state_set_hole_cards(
    ismcts_game_state_t* state,
    int player,
    const int* cards,
    int num_cards
);

/**
 * Set board cards
 */
POKEREVAL_EXPORT void ismcts_state_set_board(
    ismcts_game_state_t* state,
    const int* cards,
    int num_cards
);

/**
 * Print game state (for debugging)
 */
POKEREVAL_EXPORT void ismcts_state_print(const ismcts_game_state_t* state);

/**
 * Print action
 */
POKEREVAL_EXPORT void ismcts_action_print(const ismcts_action_t* action);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_ENGINE_SOLVERS_ISMCTS_POKER_H */
