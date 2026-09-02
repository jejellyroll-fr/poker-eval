#include <poker_eval/solver/pe_holdem_chance.h>

#include <stdlib.h>
#include <string.h>

#include <poker_eval/solver/pe_holdem_river.h>

static int card_count(mask_t cards)
{
    int count = 0;
    int card;
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(cards, card))
            count++;
    return count;
}

static int nth_unseen(mask_t used, uint16_t ordinal)
{
    uint16_t seen = 0u;
    int card;
    for (card = 0; card < MODERN_DECK_SIZE; ++card)
    {
        if (mask_is_set(used, card))
            continue;
        if (seen++ == ordinal)
            return card;
    }
    return -1;
}

static int chance_is_terminal(const void *state, void *user)
{
    (void)user;
    return !((const pe_holdem_chance_state_t *)state)->is_chance;
}

static int chance_is_chance(const void *state, void *user)
{
    (void)user;
    return ((const pe_holdem_chance_state_t *)state)->is_chance;
}

static uint16_t chance_count(const void *state, void *user)
{
    pe_holdem_chance_game_t *game = (pe_holdem_chance_game_t *)user;
    const pe_holdem_chance_state_t *node =
        (const pe_holdem_chance_state_t *)state;
    mask_t used = node->board | game->hole[0] | game->hole[1];
    int count;
    if (!node->is_chance)
        return 0u;
    count = MODERN_DECK_SIZE - card_count(used);
    return count > 0 ? (uint16_t)count : 0u;
}

static double chance_weight(const void *state, uint16_t outcome, void *user)
{
    (void)state;
    (void)outcome;
    (void)user;
    return 1.0;
}

static int own_child(pe_holdem_chance_game_t *game,
                     pe_holdem_chance_state_t *child)
{
    pe_holdem_chance_state_t **grown;
    size_t capacity;
    if (game->child_count == game->child_capacity)
    {
        capacity = game->child_capacity == 0u ? 16u
                                               : game->child_capacity * 2u;
        grown = (pe_holdem_chance_state_t **)realloc(
            game->children, capacity * sizeof(*grown));
        if (!grown)
            return -1;
        game->children = grown;
        game->child_capacity = capacity;
    }
    game->children[game->child_count++] = child;
    return 0;
}

static void release_state(const void *state, void *user)
{
    pe_holdem_chance_game_t *game = (pe_holdem_chance_game_t *)user;
    size_t index;

    if (game == NULL || state == NULL)
        return;
    for (index = 0u; index < game->child_count; ++index)
    {
        if (game->children[index] == state)
        {
            free(game->children[index]);
            game->children[index] = game->children[--game->child_count];
            return;
        }
    }
}

static const void *apply_chance(const void *state, int outcome, void *user)
{
    pe_holdem_chance_game_t *game = (pe_holdem_chance_game_t *)user;
    const pe_holdem_chance_state_t *source =
        (const pe_holdem_chance_state_t *)state;
    pe_holdem_chance_state_t *child;
    int card;
    if (!source->is_chance || outcome < 0 ||
        outcome >= (int)chance_count(state, user))
        return NULL;
    card = nth_unseen(source->board | game->hole[0] | game->hole[1],
                      (uint16_t)outcome);
    if (card < 0)
        return NULL;
    child = (pe_holdem_chance_state_t *)malloc(sizeof(*child));
    if (!child)
        return NULL;
    *child = *source;
    child->board |= mask_set(MASK_EMPTY, card);
    child->is_chance = 0;
    if (own_child(game, child) != 0)
    {
        free(child);
        return NULL;
    }
    return child;
}

static int acting_player(const void *state, void *user)
{
    (void)state;
    (void)user;
    return -1;
}

static uint16_t action_count(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0u;
}

static uint64_t infoset_key(const void *state, void *user)
{
    (void)state;
    (void)user;
    return 0u;
}

static const void *apply_action_stub(const void *state, uint16_t action,
                                     void *user)
{
    (void)state;
    (void)action;
    (void)user;
    return NULL;
}

static int terminal_values(const void *state, const pe_reach_vec_t *reach,
                           pe_value_vec_t *out_values, uint8_t player_count,
                           void *user)
{
    pe_holdem_chance_game_t *game = (pe_holdem_chance_game_t *)user;
    const pe_holdem_chance_state_t *node =
        (const pe_holdem_chance_state_t *)state;
    const mask_t holes[] = {game->hole[0], game->hole[1]};
    pe_holdem_river_spec_t spec;
    spec.context = game->context;
    spec.board = node->board;
    spec.hole = holes;
    spec.combo_count = 1u;
    return pe_holdem_river_terminal_values(&spec, &node->betting, reach,
                                           out_values, player_count);
}

int pe_holdem_chance_game_init(
    pe_holdem_chance_game_t *out,
    const EvalContext *context,
    mask_t board,
    mask_t player0_hole,
    mask_t player1_hole,
    double pot,
    double invested_each)
{
    pe_betting_rules_t rules;
    const double stacks[] = {invested_each, invested_each};
    if (!out || !context || card_count(board) != 4 ||
        card_count(player0_hole) != 2 || card_count(player1_hole) != 2 ||
        (board & player0_hole) != 0 || (board & player1_hole) != 0 ||
        (player0_hole & player1_hole) != 0 || pot < 0.0 || invested_each < 0.0)
        return -1;
    memset(out, 0, sizeof(*out));
    pe_betting_rules_default(&rules, 2u);
    if (pe_betting_state_init(&out->root.betting, &rules, stacks, 2u, 0,
                              pot, 0.0) != PE_BETTING_OK)
        return -1;
    out->root.betting.invested[0] = invested_each;
    out->root.betting.invested[1] = invested_each;
    out->root.betting.round_complete = 1;
    out->root.board = board;
    out->root.is_chance = 1;
    out->context = context;
    out->hole[0] = player0_hole;
    out->hole[1] = player1_hole;
    out->vector.root = &out->root;
    out->vector.user = out;
    out->vector.player_count = 2u;
    out->vector.combo_count = 1u;
    out->vector.is_chance = chance_is_chance;
    out->vector.chance_outcome_count = chance_count;
    out->vector.chance_outcome_weight = chance_weight;
    out->vector.apply_chance = apply_chance;
    out->vector.is_terminal = chance_is_terminal;
    out->vector.acting_player = acting_player;
    out->vector.action_count = action_count;
    out->vector.infoset_key = infoset_key;
    out->vector.apply_action = apply_action_stub;
    out->vector.terminal_values = terminal_values;
    out->vector.release_state = release_state;
    return 0;
}

void pe_holdem_chance_game_destroy(pe_holdem_chance_game_t *game)
{
    size_t index;
    if (!game)
        return;
    for (index = 0u; index < game->child_count; ++index)
        free(game->children[index]);
    free(game->children);
    memset(game, 0, sizeof(*game));
}
