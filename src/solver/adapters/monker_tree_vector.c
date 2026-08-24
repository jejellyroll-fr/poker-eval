/*
 * monker_tree_vector.c - topology adapter for the vector traversal lane
 */

#include <poker_eval/solver/pe_monker_tree_vector.h>

#include <stdlib.h>
#include <string.h>

static const mpf_tree_node_t *tree_node(const void *state,
                                        const pe_monker_tree_vector_t *game)
{
    const pe_monker_tree_state_t *tree_state =
        (const pe_monker_tree_state_t *)state;
    if (!tree_state || !game || !game->tree ||
        tree_state->node_index < 0 ||
        tree_state->node_index >= game->tree->node_count)
        return NULL;
    return &game->tree->nodes[tree_state->node_index];
}

static int vector_is_terminal(const void *state, void *user)
{
    const pe_monker_tree_vector_t *game =
        (const pe_monker_tree_vector_t *)user;
    const mpf_tree_node_t *node = tree_node(state, game);
    return node != NULL && node->type == MPF_TREE_NODE_TERMINAL;
}

static int vector_acting_player(const void *state, void *user)
{
    const pe_monker_tree_vector_t *game =
        (const pe_monker_tree_vector_t *)user;
    const mpf_tree_node_t *node = tree_node(state, game);
    return node ? node->acting_player : -1;
}

static uint16_t vector_action_count(const void *state, void *user)
{
    const pe_monker_tree_vector_t *game =
        (const pe_monker_tree_vector_t *)user;
    const mpf_tree_node_t *node = tree_node(state, game);
    if (!node || node->type != MPF_TREE_NODE_PLAYER ||
        node->action_count < 0 || node->action_count > UINT16_MAX)
        return 0u;
    return (uint16_t)node->action_count;
}

static uint64_t vector_infoset_key(const void *state, void *user)
{
    const pe_monker_tree_vector_t *game =
        (const pe_monker_tree_vector_t *)user;
    const pe_monker_tree_state_t *tree_state =
        (const pe_monker_tree_state_t *)state;
    const mpf_tree_node_t *node = tree_node(state, game);
    uint64_t hash = UINT64_C(1469598103934665603);
    const unsigned char *text;

    if (!node || !tree_state)
        return 0u;
    if (!node->id)
        return (uint64_t)(unsigned)tree_state->node_index;
    for (text = (const unsigned char *)node->id; *text; ++text)
    {
        hash ^= (uint64_t)*text;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int own_state(pe_monker_tree_vector_t *game,
                     pe_monker_tree_state_t *state)
{
    pe_monker_tree_state_t **grown;
    size_t capacity;

    if (game->owned_count == game->owned_capacity)
    {
        capacity = game->owned_capacity ? game->owned_capacity * 2u : 16u;
        grown = (pe_monker_tree_state_t **)realloc(
            game->owned_states, capacity * sizeof(*grown));
        if (!grown)
            return -1;
        game->owned_states = grown;
        game->owned_capacity = capacity;
    }
    game->owned_states[game->owned_count++] = state;
    return 0;
}

static const void *vector_apply_action(const void *state, uint16_t action,
                                       void *user)
{
    pe_monker_tree_vector_t *game = (pe_monker_tree_vector_t *)user;
    const mpf_tree_node_t *node = tree_node(state, game);
    pe_monker_tree_state_t *child;
    int next;

    if (!node || action >= (uint16_t)node->action_count)
        return NULL;
    next = node->actions[action].next_index;
    if (next < 0 || next >= game->tree->node_count)
        return NULL;
    child = (pe_monker_tree_state_t *)malloc(sizeof(*child));
    if (!child)
        return NULL;
    child->node_index = next;
    if (own_state(game, child) != 0)
    {
        free(child);
        return NULL;
    }
    return child;
}

static int vector_terminal_values(const void *state,
                                  const pe_reach_vec_t *reach,
                                  pe_value_vec_t *out_values,
                                  uint8_t player_count, void *user)
{
    pe_monker_tree_vector_t *game = (pe_monker_tree_vector_t *)user;
    const pe_monker_tree_state_t *tree_state =
        (const pe_monker_tree_state_t *)state;

    if (!game->terminal_values || !tree_state)
        return -1;
    return game->terminal_values(tree_state->node_index, reach, out_values,
                                 player_count, game->user);
}

int pe_monker_tree_vector_init(
    pe_monker_tree_vector_t *out,
    const mpf_tree_def_t *tree,
    uint8_t player_count,
    uint16_t combo_count,
    pe_monker_tree_terminal_values_fn terminal_values,
    void *user)
{
    int node;

    if (!out || !tree || !tree->nodes || tree->node_count <= 0 ||
        tree->root_index < 0 || tree->root_index >= tree->node_count ||
        player_count == 0u || player_count > PE_TRAVERSAL_MAX_PLAYERS ||
        combo_count == 0u || !terminal_values)
        return -1;
    for (node = 0; node < tree->node_count; ++node)
        if (tree->nodes[node].type == MPF_TREE_NODE_CHANCE)
            return -1;

    memset(out, 0, sizeof(*out));
    out->tree = tree;
    out->terminal_values = terminal_values;
    out->user = user;
    out->root_state.node_index = tree->root_index;
    out->game.root = &out->root_state;
    out->game.user = out;
    out->game.player_count = player_count;
    out->game.combo_count = combo_count;
    out->game.is_terminal = vector_is_terminal;
    out->game.acting_player = vector_acting_player;
    out->game.action_count = vector_action_count;
    out->game.infoset_key = vector_infoset_key;
    out->game.apply_action = vector_apply_action;
    out->game.terminal_values = vector_terminal_values;
    return 0;
}

void pe_monker_tree_vector_destroy(pe_monker_tree_vector_t *game)
{
    size_t index;

    if (!game)
        return;
    for (index = 0u; index < game->owned_count; ++index)
        free(game->owned_states[index]);
    free(game->owned_states);
    memset(game, 0, sizeof(*game));
}
