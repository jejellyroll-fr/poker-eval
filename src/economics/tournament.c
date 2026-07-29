/*
 * tournament.c - Tournament structure management and economic calculations
 */

/* Suppress warnings for int64_t to double conversions - these are intentional
   in this economics module where precise decimal values aren't critical */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"
#endif

#include <math.h>
#include <poker_eval/economics/tournament.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * STRUCTURE CREATION AND MANAGEMENT
 * ============================================================================
 */

void tournament_structure_init(tournament_structure_t *structure) {
  if (!structure)
    return;
  memset(structure, 0, sizeof(*structure));
  structure->num_levels = 0;
  structure->current_level = 0;
  structure->time_remaining_ms = 0;
  structure->level_start_time_ms = 0;
  structure->is_paused = false;
}

int tournament_structure_add_level(tournament_structure_t *structure,
                                   int64_t sb, int64_t bb, int64_t ante,
                                   int64_t bb_ante, int duration_seconds) {
  if (!structure || structure->num_levels >= TOURNAMENT_MAX_LEVELS)
    return -1;

  int idx = structure->num_levels;
  tournament_level_t *level = &structure->levels[idx];

  level->small_blind = sb;
  level->big_blind = bb;
  level->ante = ante;
  level->big_blind_ante = bb_ante;
  level->duration_seconds = duration_seconds;
  level->is_break = false;

  structure->num_levels++;
  return idx;
}

int tournament_structure_add_break(tournament_structure_t *structure,
                                   int duration_seconds) {
  if (!structure || structure->num_levels >= TOURNAMENT_MAX_LEVELS)
    return -1;

  int idx = structure->num_levels;
  tournament_level_t *level = &structure->levels[idx];

  /* Copy blinds from previous level (break doesn't change blinds) */
  if (idx > 0) {
    level->small_blind = structure->levels[idx - 1].small_blind;
    level->big_blind = structure->levels[idx - 1].big_blind;
    level->ante = structure->levels[idx - 1].ante;
    level->big_blind_ante = structure->levels[idx - 1].big_blind_ante;
  }
  level->duration_seconds = duration_seconds;
  level->is_break = true;

  structure->num_levels++;
  return idx;
}

void tournament_structure_create_turbo(tournament_structure_t *structure,
                                       int64_t starting_sb) {
  tournament_structure_init(structure);
  int64_t sb = starting_sb;

  /* Turbo: 3-minute levels, aggressive increases */
  /* Levels 1-4: No ante */
  tournament_structure_add_level(structure, sb, sb * 2, 0, 0, 180);
  tournament_structure_add_level(structure, sb * 2, sb * 4, 0, 0, 180);
  tournament_structure_add_level(structure, sb * 3, sb * 6, 0, 0, 180);
  tournament_structure_add_level(structure, sb * 4, sb * 8, 0, 0, 180);

  /* Levels 5-8: Antes begin */
  tournament_structure_add_level(structure, sb * 5, sb * 10, sb, 0, 180);
  tournament_structure_add_level(structure, sb * 6, sb * 12, sb * 2, 0, 180);
  tournament_structure_add_level(structure, sb * 8, sb * 16, sb * 2, 0, 180);
  tournament_structure_add_level(structure, sb * 10, sb * 20, sb * 3, 0, 180);

  /* Break */
  tournament_structure_add_break(structure, 300);

  /* Levels 9-12: Higher stakes */
  tournament_structure_add_level(structure, sb * 12, sb * 24, sb * 4, 0, 180);
  tournament_structure_add_level(structure, sb * 15, sb * 30, sb * 5, 0, 180);
  tournament_structure_add_level(structure, sb * 20, sb * 40, sb * 6, 0, 180);
  tournament_structure_add_level(structure, sb * 25, sb * 50, sb * 8, 0, 180);
}

void tournament_structure_create_regular(tournament_structure_t *structure,
                                         int64_t starting_sb) {
  tournament_structure_init(structure);
  int64_t sb = starting_sb;

  /* Regular: 10-15 minute levels */
  /* Levels 1-4: No ante */
  tournament_structure_add_level(structure, sb, sb * 2, 0, 0, 600);
  tournament_structure_add_level(structure, (int64_t)(sb * 1.5), sb * 3, 0, 0,
                                 600);
  tournament_structure_add_level(structure, sb * 2, sb * 4, 0, 0, 600);
  tournament_structure_add_level(structure, (int64_t)(sb * 2.5), sb * 5, 0, 0,
                                 600);

  /* Levels 5-8: Antes begin */
  tournament_structure_add_level(structure, sb * 3, sb * 6, sb, 0, 600);
  tournament_structure_add_level(structure, sb * 4, sb * 8, sb, 0, 600);
  tournament_structure_add_level(structure, sb * 5, sb * 10,
                                 (int64_t)(sb * 1.5), 0, 600);
  tournament_structure_add_level(structure, sb * 6, sb * 12, sb * 2, 0, 600);

  /* Break */
  tournament_structure_add_break(structure, 600);

  /* Levels 9-16: Progressive increases */
  tournament_structure_add_level(structure, sb * 8, sb * 16, sb * 2, 0, 900);
  tournament_structure_add_level(structure, sb * 10, sb * 20, sb * 3, 0, 900);
  tournament_structure_add_level(structure, sb * 12, sb * 24, sb * 4, 0, 900);
  tournament_structure_add_level(structure, sb * 15, sb * 30, sb * 5, 0, 900);

  /* Break */
  tournament_structure_add_break(structure, 600);

  tournament_structure_add_level(structure, sb * 20, sb * 40, sb * 6, 0, 900);
  tournament_structure_add_level(structure, sb * 25, sb * 50, sb * 8, 0, 900);
  tournament_structure_add_level(structure, sb * 30, sb * 60, sb * 10, 0, 900);
  tournament_structure_add_level(structure, sb * 40, sb * 80, sb * 12, 0, 900);
}

void tournament_structure_create_deep_stack(tournament_structure_t *structure,
                                            int64_t starting_sb) {
  tournament_structure_init(structure);
  int64_t sb = starting_sb;

  /* Deep stack: 20-30 minute levels, slow progression */
  /* Levels 1-6: No ante, gentle increases */
  tournament_structure_add_level(structure, sb, sb * 2, 0, 0, 1200);
  tournament_structure_add_level(structure, (int64_t)(sb * 1.25),
                                 (int64_t)(sb * 2.5), 0, 0, 1200);
  tournament_structure_add_level(structure, (int64_t)(sb * 1.5), sb * 3, 0, 0,
                                 1200);
  tournament_structure_add_level(structure, sb * 2, sb * 4, 0, 0, 1200);
  tournament_structure_add_level(structure, (int64_t)(sb * 2.5), sb * 5, 0, 0,
                                 1200);
  tournament_structure_add_level(structure, sb * 3, sb * 6, 0, 0, 1200);

  /* Break */
  tournament_structure_add_break(structure, 900);

  /* Levels 7-12: BB ante format */
  tournament_structure_add_level(structure, sb * 4, sb * 8, 0, sb * 8, 1500);
  tournament_structure_add_level(structure, sb * 5, sb * 10, 0, sb * 10, 1500);
  tournament_structure_add_level(structure, sb * 6, sb * 12, 0, sb * 12, 1500);
  tournament_structure_add_level(structure, sb * 8, sb * 16, 0, sb * 16, 1500);
  tournament_structure_add_level(structure, sb * 10, sb * 20, 0, sb * 20, 1500);
  tournament_structure_add_level(structure, sb * 12, sb * 24, 0, sb * 24, 1500);

  /* Break */
  tournament_structure_add_break(structure, 900);

  /* Levels 13-18 */
  tournament_structure_add_level(structure, sb * 15, sb * 30, 0, sb * 30, 1800);
  tournament_structure_add_level(structure, sb * 20, sb * 40, 0, sb * 40, 1800);
  tournament_structure_add_level(structure, sb * 25, sb * 50, 0, sb * 50, 1800);
  tournament_structure_add_level(structure, sb * 30, sb * 60, 0, sb * 60, 1800);
  tournament_structure_add_level(structure, sb * 40, sb * 80, 0, sb * 80, 1800);
  tournament_structure_add_level(structure, sb * 50, sb * 100, 0, sb * 100,
                                 1800);
}

/* ============================================================================
 * TOURNAMENT STATE MANAGEMENT
 * ============================================================================
 */

void tournament_state_init(tournament_state_t *state) {
  if (!state)
    return;
  memset(state, 0, sizeof(*state));
  tournament_structure_init(&state->structure);
  state->type = TOURNAMENT_TYPE_FREEZEOUT;
  state->is_running = false;
}

tournament_error_t tournament_configure(tournament_state_t *state,
                                        tournament_type_t type, int64_t buy_in,
                                        int64_t starting_chips,
                                        int num_players) {
  if (!state)
    return TOURNAMENT_ERROR_INVALID_PARAMS;
  if (num_players <= 0 || num_players > TOURNAMENT_MAX_PLAYERS)
    return TOURNAMENT_ERROR_INVALID_PARAMS;
  if (buy_in < 0 || starting_chips <= 0)
    return TOURNAMENT_ERROR_INVALID_PARAMS;

  state->type = type;
  state->buy_in = buy_in;
  state->starting_chips = starting_chips;
  state->registered_players = num_players;
  state->players_remaining = num_players;
  state->total_chips = starting_chips * num_players;
  state->prize_pool = buy_in * num_players;

  /* Calculate ITM/bubble positions based on player count */
  if (num_players <= 9) {
    state->itm_position = 3; /* Top 3 paid */
  } else if (num_players <= 18) {
    state->itm_position = 4; /* Top 4 paid */
  } else if (num_players <= 45) {
    state->itm_position = (int)(num_players * 0.15);
  } else {
    state->itm_position = (int)(num_players * 0.12);
  }
  if (state->itm_position < 1)
    state->itm_position = 1;

  state->bubble_position = state->itm_position + 1;
  state->on_bubble = false;
  state->in_the_money = false;

  return TOURNAMENT_OK;
}

tournament_error_t tournament_set_payouts_percentage(tournament_state_t *state,
                                                     const double *percentages,
                                                     int num_payouts) {
  if (!state || !percentages)
    return TOURNAMENT_ERROR_INVALID_PARAMS;
  if (num_payouts <= 0 || num_payouts > TOURNAMENT_MAX_PAYOUTS)
    return TOURNAMENT_ERROR_INVALID_PARAMS;

  state->payouts.num_payouts = num_payouts;
  state->payouts.use_percentages = true;

  double total = 0.0;
  for (int i = 0; i < num_payouts; i++) {
    state->payouts.percentages[i] = percentages[i];
    total += percentages[i];
  }

  /* Normalize if not exactly 100% */
  if (fabs(total - 100.0) > 0.01) {
    for (int i = 0; i < num_payouts; i++) {
      state->payouts.percentages[i] = (percentages[i] / total) * 100.0;
    }
  }

  /* Update ITM position */
  state->itm_position = num_payouts;
  state->bubble_position = num_payouts + 1;

  return TOURNAMENT_OK;
}

tournament_error_t tournament_set_standard_payouts(tournament_state_t *state,
                                                   int num_players) {
  if (!state)
    return TOURNAMENT_ERROR_INVALID_PARAMS;

  /* Standard SNG/MTT payout structures */
  double payouts[20];
  int num_payouts;

  if (num_players <= 6) {
    /* 6-max: 65/35 */
    payouts[0] = 65.0;
    payouts[1] = 35.0;
    num_payouts = 2;
  } else if (num_players <= 9) {
    /* 9-max: 50/30/20 */
    payouts[0] = 50.0;
    payouts[1] = 30.0;
    payouts[2] = 20.0;
    num_payouts = 3;
  } else if (num_players <= 18) {
    /* 18-max: 40/30/20/10 */
    payouts[0] = 40.0;
    payouts[1] = 30.0;
    payouts[2] = 20.0;
    payouts[3] = 10.0;
    num_payouts = 4;
  } else if (num_players <= 45) {
    /* 45-max: Top 7 */
    payouts[0] = 30.0;
    payouts[1] = 20.0;
    payouts[2] = 14.0;
    payouts[3] = 11.0;
    payouts[4] = 9.0;
    payouts[5] = 8.0;
    payouts[6] = 8.0;
    num_payouts = 7;
  } else {
    /* Large MTT: ~15% ITM */
    int itm = (int)(num_players * 0.15);
    if (itm < 9)
      itm = 9;
    if (itm > 20)
      itm = 20;

    /* Top-heavy distribution */
    payouts[0] = 23.0;
    payouts[1] = 14.0;
    payouts[2] = 10.0;
    payouts[3] = 7.5;
    payouts[4] = 6.0;
    payouts[5] = 5.0;
    payouts[6] = 4.0;
    payouts[7] = 3.5;
    payouts[8] = 3.0;

    double remaining = 24.0; /* ~24% for positions 10+ */
    for (int i = 9; i < itm; i++) {
      payouts[i] = remaining / (itm - 9);
    }
    num_payouts = itm;
  }

  return tournament_set_payouts_percentage(state, payouts, num_payouts);
}

tournament_error_t tournament_start(tournament_state_t *state) {
  if (!state)
    return TOURNAMENT_ERROR_INVALID_PARAMS;
  if (state->is_running)
    return TOURNAMENT_ERROR_ALREADY_RUNNING;
  if (state->players_remaining <= 0)
    return TOURNAMENT_ERROR_NO_PLAYERS;
  if (state->structure.num_levels == 0)
    return TOURNAMENT_ERROR_INVALID_STATE;

  state->is_running = true;
  state->structure.current_level = 0;
  state->structure.is_paused = false;

  const tournament_level_t *level = &state->structure.levels[0];
  state->structure.time_remaining_ms = level->duration_seconds * 1000LL;

  return TOURNAMENT_OK;
}

tournament_error_t tournament_pause(tournament_state_t *state) {
  if (!state)
    return TOURNAMENT_ERROR_INVALID_PARAMS;
  if (!state->is_running)
    return TOURNAMENT_ERROR_NOT_RUNNING;

  state->structure.is_paused = true;
  return TOURNAMENT_OK;
}

tournament_error_t tournament_resume(tournament_state_t *state) {
  if (!state)
    return TOURNAMENT_ERROR_INVALID_PARAMS;
  if (!state->is_running)
    return TOURNAMENT_ERROR_NOT_RUNNING;

  state->structure.is_paused = false;
  return TOURNAMENT_OK;
}

bool tournament_update_time(tournament_state_t *state,
                            int64_t current_time_ms) {
  if (!state || !state->is_running || state->structure.is_paused)
    return false;

  state->current_time_ms = current_time_ms;

  /* Calculate elapsed time since level start */
  if (state->structure.level_start_time_ms == 0) {
    state->structure.level_start_time_ms = current_time_ms;
  }

  int64_t elapsed = current_time_ms - state->structure.level_start_time_ms;
  const tournament_level_t *level = tournament_get_current_level(state);

  if (!level || level->duration_seconds == 0) {
    /* Untimed level */
    return false;
  }

  int64_t level_duration_ms = level->duration_seconds * 1000LL;
  state->structure.time_remaining_ms = level_duration_ms - elapsed;

  if (state->structure.time_remaining_ms <= 0) {
    /* Level expired, advance */
    tournament_error_t err = tournament_advance_level(state);
    return (err == TOURNAMENT_OK);
  }

  return false;
}

tournament_error_t tournament_advance_level(tournament_state_t *state) {
  if (!state)
    return TOURNAMENT_ERROR_INVALID_PARAMS;
  if (!state->is_running)
    return TOURNAMENT_ERROR_NOT_RUNNING;

  int next = state->structure.current_level + 1;
  if (next >= state->structure.num_levels) {
    /* Stay at last level */
    return TOURNAMENT_ERROR_INVALID_LEVEL;
  }

  state->structure.current_level = next;
  state->structure.level_start_time_ms = state->current_time_ms;

  const tournament_level_t *level = &state->structure.levels[next];
  state->structure.time_remaining_ms = level->duration_seconds * 1000LL;

  return TOURNAMENT_OK;
}

tournament_error_t tournament_eliminate_player(tournament_state_t *state,
                                               int player_id,
                                               int64_t chips_redistributed) {
  (void)player_id; /* Player tracking would require additional data structure */

  if (!state)
    return TOURNAMENT_ERROR_INVALID_PARAMS;
  if (state->players_remaining <= 1)
    return TOURNAMENT_ERROR_INVALID_STATE;

  state->players_remaining--;

  /* Update bubble/ITM status */
  if (state->players_remaining == state->bubble_position) {
    state->on_bubble = true;
  } else if (state->players_remaining < state->bubble_position) {
    state->on_bubble = false;
    state->in_the_money = true;
  }

  /* Note: chips are already redistributed in actual game logic */
  (void)chips_redistributed;

  return TOURNAMENT_OK;
}

/* ============================================================================
 * BLIND/ANTE QUERIES
 * ============================================================================
 */

const tournament_level_t *
tournament_get_current_level(const tournament_state_t *state) {
  if (!state || state->structure.current_level < 0 ||
      state->structure.current_level >= state->structure.num_levels)
    return NULL;

  return &state->structure.levels[state->structure.current_level];
}

const tournament_level_t *tournament_get_level(const tournament_state_t *state,
                                               int level_index) {
  if (!state || level_index < 0 || level_index >= state->structure.num_levels)
    return NULL;

  return &state->structure.levels[level_index];
}

int64_t tournament_get_small_blind(const tournament_state_t *state) {
  const tournament_level_t *level = tournament_get_current_level(state);
  return level ? level->small_blind : 0;
}

int64_t tournament_get_big_blind(const tournament_state_t *state) {
  const tournament_level_t *level = tournament_get_current_level(state);
  return level ? level->big_blind : 0;
}

int64_t tournament_get_ante(const tournament_state_t *state) {
  const tournament_level_t *level = tournament_get_current_level(state);
  if (!level)
    return 0;

  /* Return BB ante if set, otherwise traditional ante */
  return (level->big_blind_ante > 0) ? level->big_blind_ante : level->ante;
}

bool tournament_uses_bb_ante(const tournament_state_t *state) {
  const tournament_level_t *level = tournament_get_current_level(state);
  return level && level->big_blind_ante > 0;
}

int tournament_get_time_remaining(const tournament_state_t *state) {
  if (!state)
    return 0;
  return (int)(state->structure.time_remaining_ms / 1000);
}

const tournament_level_t *
tournament_get_next_level(const tournament_state_t *state) {
  if (!state)
    return NULL;

  int next = state->structure.current_level + 1;

  /* Skip breaks to show next playing level */
  while (next < state->structure.num_levels &&
         state->structure.levels[next].is_break) {
    next++;
  }

  if (next >= state->structure.num_levels)
    return NULL;
  return &state->structure.levels[next];
}

/* ============================================================================
 * TOURNAMENT PRESSURE & ICM CALCULATIONS
 * ============================================================================
 */

double tournament_calculate_pressure(const tournament_state_t *state,
                                     int64_t player_chips) {
  if (!state || player_chips <= 0)
    return 0.0;

  double pressure = 0.0;

  /* Factor 1: Stack vs average (Q-ratio inverse) */
  int64_t avg = tournament_get_average_stack(state);
  if (avg > 0) {
    double q = (double)player_chips / (double)avg;
    if (q < 1.0) {
      pressure += (1.0 - q) * 0.3; /* Up to 0.3 for short stacks */
    }
  }

  /* Factor 2: M-ratio (blind pressure) */
  double m = tournament_calculate_m_ratio(state, player_chips, 9);
  if (m < 20.0) {
    pressure += (20.0 - m) / 20.0 * 0.4; /* Up to 0.4 for low M */
  }

  /* Factor 3: Bubble proximity */
  if (state->on_bubble) {
    pressure += 0.3;
  } else if (state->players_remaining <= state->bubble_position + 3) {
    /* Close to bubble */
    pressure += 0.15;
  }

  return pressure > 1.0 ? 1.0 : pressure;
}

double tournament_calculate_m_ratio(const tournament_state_t *state,
                                    int64_t player_chips,
                                    int players_at_table) {
  if (!state || player_chips <= 0 || players_at_table <= 0)
    return 0.0;

  int64_t orbit_cost = tournament_get_orbit_cost(state, players_at_table);
  if (orbit_cost <= 0)
    return 999.0; /* No blinds = infinite M */

  return (double)player_chips / (double)orbit_cost;
}

double tournament_calculate_q_ratio(const tournament_state_t *state,
                                    int64_t player_chips) {
  if (!state || player_chips <= 0)
    return 0.0;

  int64_t avg = tournament_get_average_stack(state);
  if (avg <= 0)
    return 0.0;

  return (double)player_chips / (double)avg;
}

double tournament_calculate_effective_m(const tournament_state_t *state,
                                        int64_t player_chips,
                                        int players_at_table) {
  double m =
      tournament_calculate_m_ratio(state, player_chips, players_at_table);

  /* Adjust for short-handed play */
  double adjustment = (double)players_at_table / 10.0;
  if (adjustment > 1.0)
    adjustment = 1.0;

  return m * adjustment;
}

bool tournament_is_on_bubble(const tournament_state_t *state) {
  return state && state->on_bubble;
}

double tournament_calculate_bubble_factor(const tournament_state_t *state,
                                          int64_t player_chips) {
  if (!state)
    return 1.0;

  /* No bubble factor if not near bubble */
  if (!state->on_bubble &&
      state->players_remaining > state->bubble_position + 5) {
    return 1.0;
  }

  double factor = 1.0;

  /* On bubble: significant factor */
  if (state->on_bubble) {
    factor = 1.5;

    /* Adjust based on stack size */
    int64_t avg = tournament_get_average_stack(state);
    if (avg > 0) {
      double q = (double)player_chips / (double)avg;
      if (q < 0.5) {
        /* Short stack: even higher factor (survival mode) */
        factor = 2.0;
      } else if (q > 2.0) {
        /* Big stack: can afford more risk */
        factor = 1.2;
      }
    }
  } else {
    /* Approaching bubble */
    int distance = state->players_remaining - state->bubble_position;
    factor = 1.0 + (0.1 * (5 - distance));
  }

  return factor;
}

int64_t tournament_get_payout(const tournament_state_t *state,
                              int finish_position) {
  if (!state || finish_position < 1 ||
      finish_position > state->payouts.num_payouts)
    return 0;

  int idx = finish_position - 1;

  if (state->payouts.use_percentages) {
    return (int64_t)((double)state->prize_pool *
                     state->payouts.percentages[idx] / 100.0);
  } else {
    return state->payouts.amounts[idx];
  }
}

double tournament_calculate_equity(const tournament_state_t *state,
                                   int64_t player_chips) {
  if (!state || player_chips <= 0 || state->total_chips <= 0)
    return 0.0;

  /* Simple chip-proportional equity (ChipEV) */
  double chip_fraction = (double)player_chips / state->total_chips;

  /* Apply bubble adjustment if applicable */
  if (state->on_bubble || state->in_the_money) {
    /* ICM tends to favor chip leaders less than ChipEV suggests */
    /* This is a simplified approximation */
    if (chip_fraction > 0.3) {
      chip_fraction = 0.3 + (chip_fraction - 0.3) * 0.8;
    }
  }

  return chip_fraction * state->prize_pool;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

int64_t tournament_get_average_stack(const tournament_state_t *state) {
  if (!state || state->players_remaining <= 0)
    return 0;

  return state->total_chips / state->players_remaining;
}

int64_t tournament_get_chip_leader_estimate(const tournament_state_t *state) {
  if (!state || state->players_remaining <= 0)
    return 0;

  /* Estimate: chip leader typically has 2-4x average */
  int64_t avg = tournament_get_average_stack(state);
  double multiplier = 2.0 + log10((double)state->players_remaining) * 0.5;

  return (int64_t)(avg * multiplier);
}

int64_t tournament_get_orbit_cost(const tournament_state_t *state,
                                  int players_at_table) {
  if (!state || players_at_table <= 0)
    return 0;

  const tournament_level_t *level = tournament_get_current_level(state);
  if (!level)
    return 0;

  int64_t cost = level->small_blind + level->big_blind;

  /* Add antes */
  if (level->big_blind_ante > 0) {
    /* BB ante: posted once per hand */
    cost += level->big_blind_ante;
  } else if (level->ante > 0) {
    /* Traditional ante: everyone posts */
    cost += level->ante * players_at_table;
  }

  return cost;
}

const char *tournament_error_string(tournament_error_t error) {
  switch (error) {
  case TOURNAMENT_OK:
    return "Success";
  case TOURNAMENT_ERROR_INVALID_PARAMS:
    return "Invalid parameters";
  case TOURNAMENT_ERROR_INVALID_LEVEL:
    return "Invalid level";
  case TOURNAMENT_ERROR_INVALID_STATE:
    return "Invalid state";
  case TOURNAMENT_ERROR_NOT_RUNNING:
    return "Tournament not running";
  case TOURNAMENT_ERROR_ALREADY_RUNNING:
    return "Tournament already running";
  case TOURNAMENT_ERROR_NO_PLAYERS:
    return "No players";
  case TOURNAMENT_ERROR_CALCULATION_FAILED:
    return "Calculation failed";
  default:
    return "Unknown error";
  }
}

void tournament_print_state(const tournament_state_t *state) {
  if (!state) {
    printf("Tournament state: NULL\n");
    return;
  }

  printf("=== Tournament State ===\n");
  printf("Type: %d, Running: %s\n", state->type,
         state->is_running ? "Yes" : "No");
  printf("Players: %d/%d remaining\n", state->players_remaining,
         state->registered_players);
  printf("Total chips: %lld, Prize pool: %lld\n", (long long)state->total_chips,
         (long long)state->prize_pool);
  printf("ITM position: %d, Bubble: %d\n", state->itm_position,
         state->bubble_position);
  printf("On bubble: %s, In the money: %s\n", state->on_bubble ? "Yes" : "No",
         state->in_the_money ? "Yes" : "No");

  const tournament_level_t *level = tournament_get_current_level(state);
  if (level) {
    printf("Current level %d: %lld/%lld", state->structure.current_level + 1,
           (long long)level->small_blind, (long long)level->big_blind);
    if (level->big_blind_ante > 0) {
      printf(" (BB ante: %lld)", (long long)level->big_blind_ante);
    } else if (level->ante > 0) {
      printf(" (ante: %lld)", (long long)level->ante);
    }
    printf("\n");

    if (level->duration_seconds > 0) {
      printf("Time remaining: %d seconds\n",
             tournament_get_time_remaining(state));
    }
  }
}

void tournament_print_structure(const tournament_structure_t *structure) {
  if (!structure) {
    printf("Tournament structure: NULL\n");
    return;
  }

  printf("=== Blind Structure (%d levels) ===\n", structure->num_levels);
  for (int i = 0; i < structure->num_levels; i++) {
    const tournament_level_t *level = &structure->levels[i];

    if (level->is_break) {
      printf("Level %2d: BREAK (%d min)\n", i + 1,
             level->duration_seconds / 60);
    } else {
      printf("Level %2d: %6lld/%6lld", i + 1, (long long)level->small_blind,
             (long long)level->big_blind);

      if (level->big_blind_ante > 0) {
        printf(" (BB ante: %lld)", (long long)level->big_blind_ante);
      } else if (level->ante > 0) {
        printf(" (ante: %lld)", (long long)level->ante);
      }

      if (level->duration_seconds > 0) {
        printf(" - %d min", level->duration_seconds / 60);
      }
      printf("\n");
    }
  }
}
