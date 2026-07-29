#include <poker_eval/games/mixed_game.h>
#include <stdlib.h>
#include <string.h>

struct mixed_game {
    mixed_variant_t variants[MIXED_GAME_MAX_VARIANTS];
    mixed_variant_stats_t stats[MIXED_GAME_MAX_VARIANTS];
    int variant_count;
    mixed_rotation_t rotation;
    int current_index;
    int hands_played_in_current;
    int total_hands_played;
};

static GameRules resolve_rules(const mixed_variant_t *variant)
{
    if (variant->custom_rules.game_type == variant->game && variant->custom_rules.num_hole_cards != 0)
    {
        return variant->custom_rules;
    }
    return game_engine_get_default_rules(variant->game);
}

mixed_game_t *mixed_game_create(const mixed_variant_t *variants,
                                int variant_count,
                                mixed_rotation_t rotation)
{
    if (!variants || variant_count <= 0 || variant_count > MIXED_GAME_MAX_VARIANTS)
    {
        return NULL;
    }

    mixed_game_t *mixed = calloc(1, sizeof(*mixed));
    if (!mixed)
    {
        return NULL;
    }

    for (int i = 0; i < variant_count; i++)
    {
        mixed->variants[i] = variants[i];
        if (mixed->variants[i].hands_per_round <= 0)
        {
            mixed->variants[i].hands_per_round = 1;
        }
        mixed->stats[i].game = variants[i].game;
    }
    mixed->variant_count = variant_count;
    mixed->rotation = rotation;
    mixed->current_index = 0;
    mixed->hands_played_in_current = 0;
    mixed->total_hands_played = 0;
    return mixed;
}

void mixed_game_destroy(mixed_game_t *mixed)
{
    free(mixed);
}

const mixed_variant_t *mixed_game_current(const mixed_game_t *mixed)
{
    if (!mixed || mixed->variant_count == 0)
    {
        return NULL;
    }
    return &mixed->variants[mixed->current_index];
}

void mixed_game_reset(mixed_game_t *mixed)
{
    if (!mixed)
        return;
    mixed->current_index = 0;
    mixed->hands_played_in_current = 0;
    mixed->total_hands_played = 0;
    memset(mixed->stats, 0, sizeof(mixed->stats));
    for (int i = 0; i < mixed->variant_count; i++)
    {
        mixed->stats[i].game = mixed->variants[i].game;
    }
}

void mixed_game_advance(mixed_game_t *mixed,
                        const GameState *state,
                        int dealer_player_id)
{
    if (!mixed || mixed->variant_count == 0)
        return;

    mixed->hands_played_in_current++;
    mixed->total_hands_played++;

    int next_index = mixed->current_index;

    switch (mixed->rotation)
    {
    case MIXED_ROTATION_SEQUENTIAL:
        if (mixed->hands_played_in_current >= mixed->variants[mixed->current_index].hands_per_round)
        {
            next_index = (mixed->current_index + 1) % mixed->variant_count;
            mixed->hands_played_in_current = 0;
        }
        break;

    case MIXED_ROTATION_RANDOM:
        next_index = rand() % mixed->variant_count;
        mixed->hands_played_in_current = 0;
        break;

    case MIXED_ROTATION_DEALER_CHOICE:
    case MIXED_ROTATION_BUTTON_CHOICE:
        (void)state;
        (void)dealer_player_id;
        /* Dealer/Button choice handled externally via explicit setter */
        break;

    default:
        break;
    }

    mixed->current_index = next_index;
}

void mixed_game_record_result(mixed_game_t *mixed,
                              const HandResult *results,
                              int result_count)
{
    if (!mixed || !results || result_count <= 0)
        return;

    mixed_variant_stats_t *current_stats = &mixed->stats[mixed->current_index];
    int winner_count = 0;
    for (int i = 0; i < result_count; i++)
    {
        if (results[i].is_winner)
        {
            winner_count++;
            current_stats->total_winnings += (double)results[i].winnings;
        }
    }

    current_stats->hands_played++;
    if (winner_count == 1)
        current_stats->wins++;
    else if (winner_count > 1)
        current_stats->ties++;
    current_stats->losses += (result_count > winner_count) ? (result_count - winner_count) : 0;
}

void mixed_game_get_report(const mixed_game_t *mixed,
                           mixed_game_report_t *out_report)
{
    if (!mixed || !out_report)
        return;

    memset(out_report, 0, sizeof(*out_report));
    out_report->rotation = mixed->rotation;
    out_report->variant_count = mixed->variant_count;
    out_report->current_index = mixed->current_index;
    out_report->total_hands_played = mixed->total_hands_played;
    for (int i = 0; i < mixed->variant_count; i++)
    {
        out_report->stats[i] = mixed->stats[i];
    }
}

bool mixed_game_set_current_variant(mixed_game_t *mixed, int variant_index)
{
    if (!mixed || variant_index < 0 || variant_index >= mixed->variant_count)
        return false;
    mixed->current_index = variant_index;
    mixed->hands_played_in_current = 0;
    return true;
}

bool mixed_game_get_current_rules(const mixed_game_t *mixed, GameRules *out_rules)
{
    if (!mixed || !out_rules || mixed->variant_count == 0)
        return false;
    *out_rules = resolve_rules(&mixed->variants[mixed->current_index]);
    return true;
}
