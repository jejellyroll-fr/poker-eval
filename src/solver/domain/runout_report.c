/* runout_report.c - exact conditional Hold'em runout enumeration. */

#include <poker_eval/solver/pe_runout_report.h>
#include <poker_eval/solver/pe_holdem_streets.h>

#include <string.h>

typedef struct
{
    pe_runout_report_callback callback;
    void *user;
    size_t count;
    double probability_sum;
    int stopped;
} report_context_t;

typedef struct
{
    report_context_t *report;
    mask_t dead;
    double probability;
    mask_t parent_board;
    int last_card;
    int coalesce_pairs;
} transition_context_t;

static int recurse(mask_t board, mask_t dead, double probability,
                   report_context_t *report, int last_card,
                   int coalesce_pairs);

static int transition_callback(mask_t child, uint8_t cards, double weight,
                                void *opaque)
{
    transition_context_t *context = opaque;
    mask_t added = child & ~context->parent_board;
    int added_card = -1;
    (void)cards;
    (void)weight;
    for (int card = 0; card < MODERN_DECK_SIZE; ++card)
        if (mask_is_set(added, card))
        {
            added_card = card;
            break;
        }
    /* Turn/river is a sequential chance process, but a report is about the
       final board set. Keep one ordering and add the mass of its mirror. */
    if (context->coalesce_pairs && mask_popcount(context->parent_board) == 4 &&
        added_card <= context->last_card)
        return 0;
    return recurse(child, context->dead, context->probability,
                   context->report,
                   mask_popcount(context->parent_board) == 3 ? added_card : -1,
                   context->coalesce_pairs);
}

static int recurse(mask_t board, mask_t dead, double probability,
                   report_context_t *report, int last_card,
                   int coalesce_pairs)
{
    pe_holdem_street_t street;
    uint8_t added;

    if (pe_holdem_street_from_board(board, &street) != 0)
        return -1;
    added = pe_holdem_next_public_count(street);
    if (added == 0u)
    {
        pe_runout_report_row_t row;
        row.board = board;
        row.probability = probability;
        if (report->callback(&row, report->user) != 0)
        {
            report->stopped = 1;
            return 1;
        }
        ++report->count;
        report->probability_sum += probability;
        return 0;
    }
    {
        size_t outcome_count = pe_holdem_public_outcome_count(board, dead);
        transition_context_t context;
        size_t count = 0u;
        double weight_sum = 0.0;

        if (outcome_count == 0u)
            return -1;
        context.report = report;
        context.dead = dead;
        context.probability = probability / (double)outcome_count;
        if (coalesce_pairs && mask_popcount(board) == 4)
            context.probability *= 2.0;
        context.parent_board = board;
        context.last_card = last_card;
        context.coalesce_pairs = coalesce_pairs;
        /* Public chance is uniform in the canonical Hold'em model. The
           helper's callback still receives the exact child board, which
           keeps the enumeration order and legality checks in one place. */
        (void)weight_sum;
        if (pe_holdem_public_chance_enumerate(
                board, dead, transition_callback, &context,
                &count, &weight_sum) != 0)
            return report->stopped ? 1 : -1;
    }
    return 0;
}

int pe_holdem_runout_report_generate(
    mask_t board,
    mask_t dead,
    pe_runout_report_callback callback,
    void *user,
    size_t *out_count,
    double *out_probability_sum)
{
    pe_holdem_street_t street;
    if (!callback || !out_count || !out_probability_sum ||
        !mask_is_valid(board) || !mask_is_valid(dead) || (board & dead) != 0 ||
        pe_holdem_street_from_board(board, &street) != 0)
        return -1;
    {
        report_context_t report;
        memset(&report, 0, sizeof(report));
        report.callback = callback;
        report.user = user;
        if (recurse(board, dead, 1.0, &report, -1,
                    mask_popcount(board) < 4) != 0 && !report.stopped)
            return -1;
        *out_count = report.count;
        *out_probability_sum = report.probability_sum;
        return report.stopped ? 1 : 0;
    }
}
