#include <poker_eval/equity/range_combo_buffers.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int range_combo_equals_ignore_case(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs)
        return 0;
    while (*lhs && *rhs)
    {
        char a = (char)tolower((unsigned char)*lhs++);
        char b = (char)tolower((unsigned char)*rhs++);
        if (a != b)
            return 0;
    }
    return (*lhs == '\0' && *rhs == '\0');
}

int range_combo_buffer_build(range_combo_buffer_t *buffer,
                             const PlayerRange *range,
                             StdDeck_CardMask board,
                             StdDeck_CardMask dead_cards)
{
    if (!buffer || !range)
        return -1;

    memset(buffer, 0, sizeof(*buffer));
    buffer->source = range;
    buffer->stats.total_combos = range->count;
    buffer->stats.total_weight = (range->total_weight > 0.0)
                                     ? range->total_weight
                                     : (double)range->count;

    if (range->count <= 0)
        return 0;

    buffer->indices = (int *)malloc((size_t)range->count * sizeof(int));
    if (!buffer->indices)
        return -1;

    StdDeck_CardMask blockers;
    StdDeck_CardMask_RESET(blockers);
    StdDeck_CardMask_OR(blockers, blockers, board);
    StdDeck_CardMask_OR(blockers, blockers, dead_cards);

    for (int i = 0; i < range->count; ++i)
    {
        double weight = range->weights ? range->weights[i] : 1.0;
        if (weight == 0.0)
            continue;

        StdDeck_CardMask hand = range->hand_masks[i];
        if (StdDeck_CardMask_ANY_SET(hand, blockers))
            continue;

        buffer->indices[buffer->count++] = i;
        buffer->stats.playable_combos++;
        buffer->stats.playable_weight += weight;
    }

    return 0;
}

void range_combo_buffer_free(range_combo_buffer_t *buffer)
{
    if (!buffer)
        return;
    free(buffer->indices);
    memset(buffer, 0, sizeof(*buffer));
}

StdDeck_CardMask range_combo_buffer_hand(const range_combo_buffer_t *buffer, int idx)
{
    if (!buffer || idx < 0 || idx >= buffer->count || !buffer->source)
    {
        StdDeck_CardMask empty;
        StdDeck_CardMask_RESET(empty);
        return empty;
    }
    return buffer->source->hand_masks[buffer->indices[idx]];
}

double range_combo_buffer_weight(const range_combo_buffer_t *buffer, int idx)
{
    if (!buffer || idx < 0 || idx >= buffer->count || !buffer->source)
        return 0.0;
    if (!buffer->source->weights)
        return 1.0;
    return buffer->source->weights[buffer->indices[idx]];
}

range_combo_mode_t range_combo_resolve_mode(void)
{
    static int initialized = 0;
    static range_combo_mode_t mode = RANGE_COMBO_MODE_PREFILTER;
    if (initialized)
        return mode;
    initialized = 1;
    const char *env = getenv("PE_RANGE_BUFFER_MODE");
    if (!env || !*env)
        return mode;
    if (range_combo_equals_ignore_case(env, "legacy"))
        mode = RANGE_COMBO_MODE_LEGACY;
    else if (range_combo_equals_ignore_case(env, "prefilter"))
        mode = RANGE_COMBO_MODE_PREFILTER;
    return mode;
}

int range_combo_build_buffers(range_combo_buffer_t buffers[],
                              const PlayerRange ranges[],
                              int num_players,
                              StdDeck_CardMask board,
                              StdDeck_CardMask dead_cards)
{
    if (!buffers || !ranges || num_players <= 0)
        return -1;
    for (int i = 0; i < num_players; ++i)
    {
        if (range_combo_buffer_build(&buffers[i], &ranges[i], board, dead_cards) != 0)
        {
            range_combo_free_buffers(buffers, i + 1);
            return -1;
        }
    }
    return 0;
}
