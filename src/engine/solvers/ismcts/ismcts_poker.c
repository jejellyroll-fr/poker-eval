/**
 * @file ismcts_poker.c
 * @brief Information Set Monte Carlo Tree Search for general poker games
 */

#include <poker_eval/engine/solvers/ismcts/ismcts_poker.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/handval.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ===== Random Number Generator (PCG) ===== */

static inline uint64_t pcg_next(uint64_t* state) {
    uint64_t oldstate = *state;
    *state = oldstate * 6364136223846793005ULL + 1442695040888963407ULL;
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
}

static inline double pcg_uniform(uint64_t* state) {
    return (double)pcg_next(state) / (double)UINT64_MAX;
}

static inline int pcg_range(uint64_t* state, int max) {
    return (int)(pcg_next(state) % (uint64_t)max);
}

/* ===== Configuration ===== */

ismcts_config_t ismcts_poker_default_config(void) {
    ismcts_config_t cfg;
    cfg.uct_c = ISMCTS_DEFAULT_UCT_C;
    cfg.max_iterations = ISMCTS_DEFAULT_ITERATIONS;
    cfg.num_determinizations = ISMCTS_DEFAULT_DETERMINIZATIONS;
    cfg.epsilon = ISMCTS_DEFAULT_EPSILON;
    cfg.use_rave = false;
    cfg.rave_beta = 0.5;
    cfg.random_seed = 1234567890ULL;
    cfg.max_depth = 100;
    return cfg;
}

/* ===== Node Management ===== */

static ismcts_node_t* node_create(ismcts_solver_t* solver) {
    ismcts_node_t* node;
    
    /* Try to use pooled node */
    if (solver->node_pool && solver->pool_used < solver->pool_size) {
        node = &solver->node_pool[solver->pool_used++];
    } else {
        node = (ismcts_node_t*)calloc(1, sizeof(ismcts_node_t));
        if (!node) return NULL;
    }
    
    memset(node, 0, sizeof(ismcts_node_t));
    solver->total_nodes_created++;
    return node;
}

static void node_free_recursive(ismcts_node_t* node, bool pool_allocated) {
    if (!node) return;
    
    for (int i = 0; i < node->num_children; i++) {
        node_free_recursive(node->children[i], pool_allocated);
    }
    
    free(node->children);
    
    /* Only free node if not from pool */
    if (!pool_allocated) {
        free(node);
    }
}

static int node_add_child(ismcts_node_t* parent, ismcts_node_t* child, const ismcts_action_t* action) {
    /* Expand children array if needed */
    if (parent->num_children >= parent->children_capacity) {
        int new_cap = parent->children_capacity == 0 ? 4 : parent->children_capacity * 2;
        ismcts_node_t** new_children = (ismcts_node_t**)realloc(
            parent->children,
            new_cap * sizeof(ismcts_node_t*)
        );
        if (!new_children) return -1;
        parent->children = new_children;
        parent->children_capacity = new_cap;
    }
    
    child->parent = parent;
    child->action = *action;
    parent->children[parent->num_children++] = child;
    return 0;
}

/* ===== UCT Selection ===== */

static double uct_value(const ismcts_node_t* node, int player, double uct_c, int parent_visits) {
    if (node->availability_count == 0) {
        return INFINITY; /* Unvisited - highest priority */
    }
    
    /* UCB1 with availability counts for ISMCTS */
    double exploit = node->total_reward[player] / (double)node->visit_count;
    double explore = uct_c * sqrt(log((double)parent_visits) / (double)node->availability_count);
    
    return exploit + explore;
}

static ismcts_node_t* select_child(ismcts_node_t* node, int player, double uct_c) {
    if (!node || node->num_children == 0) return NULL;
    
    double best_value = -INFINITY;
    ismcts_node_t* best_child = NULL;
    
    for (int i = 0; i < node->num_children; i++) {
        ismcts_node_t* child = node->children[i];
        double value = uct_value(child, player, uct_c, node->visit_count);
        
        if (value > best_value) {
            best_value = value;
            best_child = child;
        }
    }
    
    return best_child;
}

/* ===== Expansion ===== */

static int expand_node(ismcts_solver_t* solver, ismcts_node_t* node, const ismcts_game_state_t* state) {
    if (!solver || !node || !state || node->is_expanded) return 0;
    
    ismcts_action_t actions[ISMCTS_MAX_ACTIONS];
    int num_actions = solver->game->get_actions(state, actions, ISMCTS_MAX_ACTIONS, solver->game->user_data);
    
    if (num_actions <= 0) return 0;
    
    for (int i = 0; i < num_actions; i++) {
        ismcts_node_t* child = node_create(solver);
        if (!child) continue;
        
        child->acting_player = state->current_player;
        node_add_child(node, child, &actions[i]);
    }
    
    node->is_expanded = true;
    return node->num_children;
}

/* ===== Simulation (Rollout) ===== */

static void simulate(
    ismcts_solver_t* solver,
    const ismcts_game_state_t* start_state,
    double* out_payoffs
) {
    ismcts_game_state_t state;
    solver->game->clone_state(&state, start_state, solver->game->user_data);
    
    int depth = 0;
    
    /* Play out until terminal */
    while (!solver->game->is_terminal(&state, solver->game->user_data) && 
           depth < solver->config.max_depth) {
        
        ismcts_action_t actions[ISMCTS_MAX_ACTIONS];
        int num_actions = solver->game->get_actions(&state, actions, ISMCTS_MAX_ACTIONS, solver->game->user_data);
        
        if (num_actions <= 0) break;
        
        /* Random action selection during rollout */
        int action_idx = pcg_range(&solver->rng_state, num_actions);
        solver->game->apply_action(&state, &actions[action_idx], solver->game->user_data);
        
        depth++;
    }
    
    /* Get final payoffs */
    solver->game->get_payoffs(&state, out_payoffs, solver->game->user_data);
    solver->total_simulations++;
}

/* ===== Backpropagation ===== */

static void backpropagate(ismcts_node_t* node, const double* payoffs, int num_players) {
    while (node != NULL) {
        node->visit_count++;
        for (int p = 0; p < num_players; p++) {
            node->total_reward[p] += payoffs[p];
        }
        node = node->parent;
    }
}

/* ===== Main ISMCTS Algorithm ===== */

static int ismcts_iteration(
    ismcts_solver_t* solver,
    const ismcts_game_state_t* root_state,
    int hero_player
) {
    ismcts_game_state_t state;
    
    /* Determinization: sample opponent cards */
    solver->game->clone_state(&state, root_state, solver->game->user_data);
    solver->game->sample_opponent_cards(&state, hero_player, &solver->rng_state, solver->game->user_data);
    
    /* Selection phase */
    ismcts_node_t* node = solver->root;
    
    /* Update availability counts along path */
    while (node->is_expanded && !solver->game->is_terminal(&state, solver->game->user_data)) {
        /* Find child matching a legal action */
        ismcts_action_t actions[ISMCTS_MAX_ACTIONS];
        int num_actions = solver->game->get_actions(&state, actions, ISMCTS_MAX_ACTIONS, solver->game->user_data);
        
        if (num_actions <= 0) break;
        
        /* Mark available children */
        for (int i = 0; i < node->num_children; i++) {
            ismcts_node_t* child = node->children[i];
            for (int a = 0; a < num_actions; a++) {
                if (child->action.type == actions[a].type && 
                    child->action.amount == actions[a].amount) {
                    child->availability_count++;
                    break;
                }
            }
        }
        
        /* Select best child */
        ismcts_node_t* selected = select_child(node, state.current_player, solver->config.uct_c);
        if (!selected) break;
        
        /* Apply action */
        solver->game->apply_action(&state, &selected->action, solver->game->user_data);
        node = selected;
    }
    
    /* Expansion phase */
    if (!node->is_expanded && !solver->game->is_terminal(&state, solver->game->user_data)) {
        expand_node(solver, node, &state);
        
        /* Select a random child for simulation */
        if (node->num_children > 0) {
            int child_idx = pcg_range(&solver->rng_state, node->num_children);
            node = node->children[child_idx];
            solver->game->apply_action(&state, &node->action, solver->game->user_data);
        }
    }
    
    /* Simulation phase */
    double payoffs[ISMCTS_MAX_PLAYERS] = {0};
    simulate(solver, &state, payoffs);
    
    /* Backpropagation phase */
    backpropagate(node, payoffs, state.num_players);
    
    return 0;
}

/* ===== Solver Management ===== */

ismcts_solver_t* ismcts_poker_create(
    const ismcts_config_t* config,
    ismcts_game_interface_t* game
) {
    if (!game) return NULL;
    
    ismcts_solver_t* solver = (ismcts_solver_t*)calloc(1, sizeof(ismcts_solver_t));
    if (!solver) return NULL;
    
    solver->config = config ? *config : ismcts_poker_default_config();
    solver->game = game;
    solver->rng_state = solver->config.random_seed;
    
    /* Allocate node pool */
    solver->pool_size = solver->config.max_iterations * 2; /* Estimate */
    solver->node_pool = (ismcts_node_t*)calloc(solver->pool_size, sizeof(ismcts_node_t));
    solver->pool_used = 0;
    
    return solver;
}

void ismcts_poker_free(ismcts_solver_t* solver) {
    if (!solver) return;
    
    /* Free tree nodes */
    if (solver->root) {
        node_free_recursive(solver->root, solver->node_pool != NULL);
    }
    
    free(solver->node_pool);
    free(solver);
}

void ismcts_poker_reset(ismcts_solver_t* solver) {
    if (!solver) return;
    
    /* Reset tree */
    if (solver->root && !solver->node_pool) {
        node_free_recursive(solver->root, false);
    }
    solver->root = NULL;
    
    /* Reset pool */
    solver->pool_used = 0;
    if (solver->node_pool) {
        memset(solver->node_pool, 0, solver->pool_size * sizeof(ismcts_node_t));
    }
    
    /* Reset stats */
    solver->total_iterations = 0;
    solver->total_simulations = 0;
    solver->total_nodes_created = 0;
}

int ismcts_poker_search(
    ismcts_solver_t* solver,
    const ismcts_game_state_t* state,
    int hero_player,
    ismcts_action_t* out_action
) {
    if (!solver || !state || !out_action) return -1;
    
    /* Reset for new search */
    ismcts_poker_reset(solver);
    
    /* Create root node */
    solver->root = node_create(solver);
    if (!solver->root) return -1;
    
    solver->root->acting_player = state->current_player;
    
    /* Main ISMCTS loop with K determinizations */
    for (int iter = 0; iter < solver->config.max_iterations; iter++) {
        for (int d = 0; d < solver->config.num_determinizations; d++) {
            ismcts_iteration(solver, state, hero_player);
        }
        solver->total_iterations++;
    }
    
    /* Select best action (most visited) */
    if (solver->root->num_children == 0) {
        /* Need to expand root first */
        ismcts_game_state_t tmp;
        solver->game->clone_state(&tmp, state, solver->game->user_data);
        expand_node(solver, solver->root, &tmp);
    }
    
    if (solver->root->num_children == 0) {
        return -1; /* No legal actions */
    }
    
    int best_idx = 0;
    int max_visits = -1;
    
    for (int i = 0; i < solver->root->num_children; i++) {
        ismcts_node_t* child = solver->root->children[i];
        if (child->visit_count > max_visits) {
            max_visits = child->visit_count;
            best_idx = i;
        }
    }
    
    *out_action = solver->root->children[best_idx]->action;
    return 0;
}

int ismcts_poker_get_action_probs(
    const ismcts_solver_t* solver,
    ismcts_action_t* out_actions,
    double* out_probs,
    int max_actions
) {
    if (!solver || !solver->root || !out_actions || !out_probs) return -1;
    
    int num = solver->root->num_children;
    if (num > max_actions) num = max_actions;
    
    /* Count total visits */
    int total_visits = 0;
    for (int i = 0; i < solver->root->num_children; i++) {
        total_visits += solver->root->children[i]->visit_count;
    }
    
    if (total_visits == 0) {
        /* Uniform distribution */
        for (int i = 0; i < num; i++) {
            out_actions[i] = solver->root->children[i]->action;
            out_probs[i] = 1.0 / num;
        }
    } else {
        for (int i = 0; i < num; i++) {
            out_actions[i] = solver->root->children[i]->action;
            out_probs[i] = (double)solver->root->children[i]->visit_count / (double)total_visits;
        }
    }
    
    return num;
}

void ismcts_poker_get_stats(
    const ismcts_solver_t* solver,
    uint64_t* out_iterations,
    uint64_t* out_simulations,
    uint64_t* out_nodes
) {
    if (!solver) return;
    
    if (out_iterations) *out_iterations = solver->total_iterations;
    if (out_simulations) *out_simulations = solver->total_simulations;
    if (out_nodes) *out_nodes = solver->total_nodes_created;
}

/* ===== State Utilities ===== */

void ismcts_state_init(
    ismcts_game_state_t* state,
    int num_players,
    const int* starting_stacks,
    int small_blind,
    int big_blind
) {
    if (!state) return;
    
    memset(state, 0, sizeof(ismcts_game_state_t));
    
    state->num_players = num_players;
    state->num_active = num_players;
    
    /* Initialize stacks */
    for (int i = 0; i < num_players; i++) {
        state->stacks[i] = starting_stacks ? starting_stacks[i] : 1000;
    }
    
    /* Post blinds */
    int sb_player = 0; /* Assuming button is player N-1 */
    int bb_player = (num_players > 2) ? 1 : 1;
    
    if (num_players == 2) {
        /* Heads-up: button posts SB */
        sb_player = 0;
        bb_player = 1;
    }
    
    state->stacks[sb_player] -= small_blind;
    state->invested[sb_player] = small_blind;
    state->stacks[bb_player] -= big_blind;
    state->invested[bb_player] = big_blind;
    state->pot = small_blind + big_blind;
    state->to_call = big_blind;
    
    /* UTG acts first preflop */
    state->current_player = (bb_player + 1) % num_players;
    state->first_to_act = state->current_player;
    state->round = 0;
    
    /* Full deck */
    state->deck_mask = (1ULL << 52) - 1;
    
    /* Initialize hole/board cards to -1 */
    for (int i = 0; i < ISMCTS_MAX_PLAYERS; i++) {
        for (int j = 0; j < ISMCTS_MAX_CARDS; j++) {
            state->hole_cards[i][j] = -1;
        }
    }
    for (int i = 0; i < 5; i++) {
        state->board[i] = -1;
    }
}

void ismcts_state_set_hole_cards(
    ismcts_game_state_t* state,
    int player,
    const int* cards,
    int num_cards
) {
    if (!state || player < 0 || player >= state->num_players) return;
    
    state->num_hole_cards[player] = num_cards;
    for (int i = 0; i < num_cards && i < ISMCTS_MAX_CARDS; i++) {
        state->hole_cards[player][i] = cards[i];
        /* Remove from deck */
        if (cards[i] >= 0 && cards[i] < 52) {
            state->deck_mask &= ~(1ULL << cards[i]);
        }
    }
}

void ismcts_state_set_board(
    ismcts_game_state_t* state,
    const int* cards,
    int num_cards
) {
    if (!state) return;
    
    state->num_board_cards = num_cards;
    for (int i = 0; i < num_cards && i < 5; i++) {
        state->board[i] = cards[i];
        /* Remove from deck */
        if (cards[i] >= 0 && cards[i] < 52) {
            state->deck_mask &= ~(1ULL << cards[i]);
        }
    }
    
    /* Update round based on board cards */
    if (num_cards == 0) state->round = 0;
    else if (num_cards == 3) state->round = 1;
    else if (num_cards == 4) state->round = 2;
    else if (num_cards == 5) state->round = 3;
}

void ismcts_state_print(const ismcts_game_state_t* state) {
    if (!state) return;
    
    printf("=== Game State ===\n");
    printf("Pot: %d  To Call: %d  Round: %d\n", state->pot, state->to_call, state->round);
    printf("Current Player: %d  Active: %d/%d\n", 
           state->current_player, state->num_active, state->num_players);
    
    printf("Board: ");
    for (int i = 0; i < state->num_board_cards; i++) {
        int card = state->board[i];
        if (card >= 0) {
            const char* ranks = "23456789TJQKA";
            const char* suits = "cdhs";
            printf("%c%c ", ranks[card % 13], suits[card / 13]);
        }
    }
    printf("\n");
    
    for (int p = 0; p < state->num_players; p++) {
        printf("P%d: stack=%d invested=%d %s%s\n",
               p, state->stacks[p], state->invested[p],
               state->folded[p] ? "[FOLD]" : "",
               state->all_in[p] ? "[ALL-IN]" : "");
    }
}

void ismcts_action_print(const ismcts_action_t* action) {
    if (!action) return;
    
    const char* names[] = {"FOLD", "CHECK", "CALL", "BET", "RAISE", "ALL-IN"};
    printf("%s", names[action->type]);
    if (action->amount > 0) {
        printf(" %d", action->amount);
    }
}

/* ===== Hold'em Game Implementation ===== */

typedef struct {
    int num_players;
    int small_blind;
    int big_blind;
} holdem_game_data_t;

static int holdem_get_actions(
    const ismcts_game_state_t* state,
    ismcts_action_t* out_actions,
    int max_actions,
    void* user_data
) {
    (void)user_data;
    if (!state || !out_actions || state->is_terminal) return 0;
    
    int num_actions = 0;
    int player = state->current_player;
    
    /* Check if player can act */
    if (state->folded[player] || state->all_in[player]) return 0;
    
    int to_call = state->to_call - state->invested[player];
    
    /* Fold (if there's a bet to call) */
    if (to_call > 0 && num_actions < max_actions) {
        out_actions[num_actions].type = ISMCTS_ACTION_FOLD;
        out_actions[num_actions].amount = 0;
        num_actions++;
    }
    
    /* Check (if no bet to call) */
    if (to_call == 0 && num_actions < max_actions) {
        out_actions[num_actions].type = ISMCTS_ACTION_CHECK;
        out_actions[num_actions].amount = 0;
        num_actions++;
    }
    
    /* Call */
    if (to_call > 0 && num_actions < max_actions) {
        int call_amount = (to_call < state->stacks[player]) ? to_call : state->stacks[player];
        out_actions[num_actions].type = ISMCTS_ACTION_CALL;
        out_actions[num_actions].amount = call_amount;
        num_actions++;
    }
    
    /* Bet/Raise */
    if (state->stacks[player] > to_call && num_actions < max_actions) {
        /* Min bet/raise */
        int min_raise = state->to_call; /* Simplified */
        int raise_amount = to_call + min_raise;
        if (raise_amount <= state->stacks[player]) {
            out_actions[num_actions].type = (to_call == 0) ? ISMCTS_ACTION_BET : ISMCTS_ACTION_RAISE;
            out_actions[num_actions].amount = raise_amount;
            num_actions++;
        }
    }
    
    /* All-in */
    if (state->stacks[player] > 0 && num_actions < max_actions) {
        out_actions[num_actions].type = ISMCTS_ACTION_ALL_IN;
        out_actions[num_actions].amount = state->stacks[player];
        num_actions++;
    }
    
    return num_actions;
}

static void holdem_apply_action(
    ismcts_game_state_t* state,
    const ismcts_action_t* action,
    void* user_data
) {
    (void)user_data;
    if (!state || !action) return;
    
    int player = state->current_player;
    int amount = action->amount;
    
    switch (action->type) {
        case ISMCTS_ACTION_FOLD:
            state->folded[player] = true;
            state->num_active--;
            break;
            
        case ISMCTS_ACTION_CHECK:
            /* Nothing to do */
            break;
            
        case ISMCTS_ACTION_CALL:
            state->stacks[player] -= amount;
            state->invested[player] += amount;
            state->pot += amount;
            break;
            
        case ISMCTS_ACTION_BET:
        case ISMCTS_ACTION_RAISE:
            state->stacks[player] -= amount;
            state->invested[player] += amount;
            state->pot += amount;
            state->to_call = state->invested[player];
            state->last_aggressor = player;
            break;
            
        case ISMCTS_ACTION_ALL_IN:
            state->stacks[player] = 0;
            state->invested[player] += amount;
            state->pot += amount;
            if (state->invested[player] > state->to_call) {
                state->to_call = state->invested[player];
            }
            state->all_in[player] = true;
            break;
    }
    
    state->actions_this_round++;
    
    /* Count players who can still act */
    int can_act = 0;
    for (int i = 0; i < state->num_players; i++) {
        if (!state->folded[i] && !state->all_in[i]) can_act++;
    }
    
    /* Check terminal conditions first */
    if (state->num_active <= 1) {
        /* Hand is over - all but one folded */
        state->is_terminal = true;
        return;
    }
    
    if (can_act == 0) {
        /* Everyone all-in or folded - go to showdown */
        state->round = 3; /* Jump to river */
        state->is_terminal = true;
        return;
    }
    
    /* Move to next player who can act */
    int next = (player + 1) % state->num_players;
    int attempts = 0;
    while ((state->folded[next] || state->all_in[next]) && attempts < state->num_players) {
        next = (next + 1) % state->num_players;
        attempts++;
    }
    
    if (attempts >= state->num_players) {
        /* No one can act - go to showdown */
        state->is_terminal = true;
        return;
    }
    
    /* Check if round is over */
    bool round_over = false;
    
    /* Check if action is back to the first player who opened/called */
    if (state->actions_this_round >= can_act) {
        /* Everyone who can act has acted at least once */
        bool all_matched = true;
        for (int i = 0; i < state->num_players; i++) {
            if (!state->folded[i] && !state->all_in[i]) {
                if (state->invested[i] < state->to_call) {
                    all_matched = false;
                    break;
                }
            }
        }
        if (all_matched) round_over = true;
    }
    
    if (round_over) {
        /* Move to next round */
        state->round++;
        state->actions_this_round = 0;
        
        /* Reset invested amounts */
        state->to_call = 0;
        for (int i = 0; i < state->num_players; i++) {
            state->invested[i] = 0;
        }
        
        /* Find first active player */
        state->first_to_act = 0;
        attempts = 0;
        while ((state->folded[state->first_to_act] || state->all_in[state->first_to_act]) && 
               attempts < state->num_players) {
            state->first_to_act = (state->first_to_act + 1) % state->num_players;
            attempts++;
        }
        state->current_player = state->first_to_act;
        
        if (state->round > 3) {
            state->is_terminal = true;
        }
    } else {
        state->current_player = next;
    }
}

static bool holdem_is_terminal(
    const ismcts_game_state_t* state,
    void* user_data
) {
    (void)user_data;
    return state->is_terminal;
}

static void holdem_get_payoffs(
    const ismcts_game_state_t* state,
    double* out_payoffs,
    void* user_data
) {
    (void)user_data;
    if (!state || !out_payoffs) return;
    
    /* Initialize to zero */
    for (int i = 0; i < state->num_players; i++) {
        out_payoffs[i] = 0.0;
    }
    
    /* Find winners */
    if (state->num_active == 1) {
        /* Everyone folded - winner takes pot */
        for (int i = 0; i < state->num_players; i++) {
            if (!state->folded[i]) {
                out_payoffs[i] = (double)state->pot;
                return;
            }
        }
    }
    
    /* Showdown - evaluate hands */
    int best_rank = -1;
    int num_winners = 0;
    int winners[ISMCTS_MAX_PLAYERS];
    
    for (int i = 0; i < state->num_players; i++) {
        if (state->folded[i]) continue;
        
        /* Build hand mask */
        StdDeck_CardMask hand;
        StdDeck_CardMask_RESET(hand);
        
        for (int j = 0; j < state->num_hole_cards[i]; j++) {
            int card = state->hole_cards[i][j];
            if (card >= 0 && card < 52) {
                StdDeck_CardMask_SET(hand, card);
            }
        }
        
        for (int j = 0; j < state->num_board_cards; j++) {
            int card = state->board[j];
            if (card >= 0 && card < 52) {
                StdDeck_CardMask_SET(hand, card);
            }
        }
        
        /* Evaluate */
        HandVal hv = StdDeck_StdRules_EVAL_N(hand, state->num_hole_cards[i] + state->num_board_cards);
        int rank = (int)hv;
        
        if (rank > best_rank) {
            best_rank = rank;
            num_winners = 1;
            winners[0] = i;
        } else if (rank == best_rank) {
            winners[num_winners++] = i;
        }
    }
    
    /* Split pot among winners */
    double share = (double)state->pot / (double)num_winners;
    for (int i = 0; i < num_winners; i++) {
        out_payoffs[winners[i]] = share;
    }
}

static void holdem_sample_opponent_cards(
    ismcts_game_state_t* state,
    int viewer_player,
    uint64_t* rng_state,
    void* user_data
) {
    (void)user_data;
    if (!state) return;
    
    /* Build available deck */
    uint64_t deck = state->deck_mask;
    
    /* Remove viewer's known cards */
    for (int i = 0; i < state->num_hole_cards[viewer_player]; i++) {
        int card = state->hole_cards[viewer_player][i];
        if (card >= 0) deck &= ~(1ULL << card);
    }
    
    /* Remove board cards */
    for (int i = 0; i < state->num_board_cards; i++) {
        int card = state->board[i];
        if (card >= 0) deck &= ~(1ULL << card);
    }
    
    /* Sample cards for each opponent */
    for (int p = 0; p < state->num_players; p++) {
        if (p == viewer_player) continue;
        if (state->folded[p]) continue;
        
        /* Need 2 hole cards for Hold'em */
        int needed = 2;
        state->num_hole_cards[p] = needed;
        
        for (int c = 0; c < needed; c++) {
            /* Count available cards */
            int available = 0;
            for (int i = 0; i < 52; i++) {
                if (deck & (1ULL << i)) available++;
            }
            
            if (available == 0) break;
            
            /* Pick random card */
            int idx = pcg_range(rng_state, available);
            int card = -1;
            int count = 0;
            for (int i = 0; i < 52; i++) {
                if (deck & (1ULL << i)) {
                    if (count == idx) {
                        card = i;
                        break;
                    }
                    count++;
                }
            }
            
            if (card < 0) break;

            state->hole_cards[p][c] = card;
            deck &= ~(1ULL << card);
        }
    }
}

static void holdem_clone_state(
    ismcts_game_state_t* dst,
    const ismcts_game_state_t* src,
    void* user_data
) {
    (void)user_data;
    memcpy(dst, src, sizeof(ismcts_game_state_t));
}

ismcts_game_interface_t* ismcts_holdem_create(int num_players) {
    if (num_players < 2 || num_players > ISMCTS_MAX_PLAYERS) return NULL;
    
    ismcts_game_interface_t* game = (ismcts_game_interface_t*)calloc(1, sizeof(ismcts_game_interface_t));
    if (!game) return NULL;
    
    holdem_game_data_t* data = (holdem_game_data_t*)calloc(1, sizeof(holdem_game_data_t));
    if (!data) {
        free(game);
        return NULL;
    }
    
    data->num_players = num_players;
    data->small_blind = 1;
    data->big_blind = 2;
    
    game->get_actions = holdem_get_actions;
    game->apply_action = holdem_apply_action;
    game->is_terminal = holdem_is_terminal;
    game->get_payoffs = holdem_get_payoffs;
    game->sample_opponent_cards = holdem_sample_opponent_cards;
    game->clone_state = holdem_clone_state;
    game->user_data = data;
    
    return game;
}

void ismcts_game_interface_free(ismcts_game_interface_t* game) {
    if (!game) return;
    free(game->user_data);
    free(game);
}

/* Omaha implementation - similar structure but with 4+ hole cards */
ismcts_game_interface_t* ismcts_omaha_create(
    int num_players,
    int num_hole_cards,
    bool pot_limit
) {
    /* For now, reuse Hold'em implementation */
    /* TODO: Implement proper Omaha hand evaluation (2+3 rule) */
    (void)num_hole_cards;
    (void)pot_limit;
    return ismcts_holdem_create(num_players);
}
