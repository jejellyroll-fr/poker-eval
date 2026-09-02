/*
 * test_pe_batch.c - PAR-01 thread-local update batches
 */

#include <poker_eval/solver/pe_batch.h>

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
            failures++;                                            \
        }                                                          \
    } while (0)

static void test_large_batch_round_trip(void)
{
    pe_update_batch_t batch = {0};
    size_t i;

    for (i = 0u; i < 100000u; ++i)
    {
        pe_update_t update = {(pe_infoset_id_t)i, (uint16_t)(i % 7u),
                              (uint16_t)(i % 11u), (double)i, -(double)i};
        CHECK(pe_update_batch_push(&batch, update) == 0,
              "push failed at update %zu", i);
        if (batch.count != i + 1u)
            break;
    }
    CHECK(batch.count == 100000u, "stored %zu updates, expected 100000",
          batch.count);
    CHECK(batch.items[0].infoset == 0u && batch.items[99999u].infoset == 99999u,
          "batch lost update ordering");
    CHECK(fabs(batch.items[54321u].average_delta + 54321.0) < 1e-12,
          "average delta was not retained");
    pe_update_batch_destroy(&batch);
}

static void test_merge_reduces_same_slot(void)
{
    pe_update_batch_t destination = {0};
    pe_update_batch_t source = {0};
    const pe_update_t first = {42u, 1u, 2u, 2.0, 3.0};
    const pe_update_t second = {42u, 1u, 2u, 5.0, 7.0};
    const pe_update_t other = {99u, 0u, 0u, -1.0, 4.0};

    CHECK(pe_update_batch_push(&destination, first) == 0, "destination push");
    CHECK(pe_update_batch_push(&source, second) == 0, "source duplicate push");
    CHECK(pe_update_batch_push(&source, other) == 0, "source distinct push");
    destination.iteration = 7u;
    source.iteration = 7u;
    CHECK(pe_update_batch_merge(&destination, &source) == 0, "merge failed");
    CHECK(destination.iteration == 7u, "merge lost iteration metadata");
    CHECK(destination.count == 2u, "merge kept %zu slots, expected 2",
          destination.count);
    CHECK(destination.items[0].delta == 7.0 &&
              destination.items[0].average_delta == 10.0,
          "duplicate slot was not reduced");
    CHECK(destination.items[1].infoset == 99u &&
              fabs(destination.items[1].delta + 1.0) < 1e-12,
          "distinct slot was not appended");
    CHECK(pe_update_batch_merge(&destination, &destination) == -1,
          "self-merge must be refused");
    pe_update_batch_destroy(&source);
    pe_update_batch_destroy(&destination);
}

static void test_reduction_preserves_iteration_metadata(void)
{
    pe_update_batch_t left = {0};
    pe_update_batch_t right = {0};
    pe_update_batch_t reduced = {0};
    pe_update_batch_t conflicting = {0};
    pe_update_batch_source_t sources[2];

    left.iteration = 11u;
    right.iteration = 11u;
    conflicting.iteration = 12u;
    CHECK(pe_update_batch_push(&left, (pe_update_t){1u, 0u, 0u, 1.0, 0.0}) == 0,
          "left iteration push");
    CHECK(pe_update_batch_push(&right, (pe_update_t){2u, 0u, 0u, 1.0, 0.0}) == 0,
          "right iteration push");
    sources[0] = (pe_update_batch_source_t){0u, &left};
    sources[1] = (pe_update_batch_source_t){1u, &right};
    CHECK(pe_update_batch_reduce(sources, 2u, &reduced) == 0 &&
              reduced.iteration == 11u,
          "reduction did not preserve the common iteration");
    sources[1].batch = &conflicting;
    CHECK(pe_update_batch_reduce(sources, 2u, &reduced) == -1,
          "reduction accepted conflicting iterations");

    pe_update_batch_destroy(&left);
    pe_update_batch_destroy(&right);
    pe_update_batch_destroy(&conflicting);
    pe_update_batch_destroy(&reduced);
}

static int same_results(const pe_update_batch_t *left,
                        const pe_update_batch_t *right)
{
    size_t i;

    if (left->count != right->count)
        return 0;
    for (i = 0u; i < left->count; ++i)
    {
        const pe_update_t *a = &left->items[i];
        const pe_update_t *b = &right->items[i];
        if (a->infoset != b->infoset || a->action != b->action ||
            a->combo != b->combo || fabs(a->delta - b->delta) > 1e-15 ||
            fabs(a->average_delta - b->average_delta) > 1e-15)
            return 0;
    }
    return 1;
}

static void test_reduction_ignores_arrival_order(void)
{
    pe_update_batch_t threads[4] = {{0}};
    pe_update_batch_t forward = {0};
    pe_update_batch_t shuffled = {0};
    const pe_update_t updates[] = {
        {7u, 0u, 0u, 0.1, 1.0},
        {7u, 0u, 0u, 0.2, 2.0},
        {7u, 0u, 0u, 0.4, 4.0},
        {7u, 0u, 0u, 0.8, 8.0},
        {3u, 1u, 0u, 2.0, -1.0},
        {3u, 1u, 0u, 3.0, -2.0}
    };
    const pe_update_batch_source_t sources_forward[] = {
        {0u, &threads[0]}, {1u, &threads[1]},
        {2u, &threads[2]}, {3u, &threads[3]}
    };
    const pe_update_batch_source_t sources_shuffled[] = {
        {3u, &threads[3]}, {1u, &threads[1]},
        {0u, &threads[0]}, {2u, &threads[2]}
    };
    size_t i;

    for (i = 0u; i < 6u; ++i)
    {
        CHECK(pe_update_batch_push(&threads[i % 4u], updates[i]) == 0,
              "thread batch push %zu", i);
    }
    CHECK(pe_update_batch_reduce(sources_forward, 4u, &forward) == 0,
          "forward deterministic reduction failed");
    CHECK(pe_update_batch_reduce(sources_shuffled, 4u, &shuffled) == 0,
          "shuffled deterministic reduction failed");
    CHECK(same_results(&forward, &shuffled),
          "arrival order changed deterministic reduction");
    CHECK(forward.count == 2u && forward.items[0].infoset == 3u &&
              forward.items[0].delta == 5.0 &&
              forward.items[1].infoset == 7u &&
              forward.items[1].delta == 1.5,
          "reduction did not sort and sum slots as expected");

    for (i = 0u; i < 4u; ++i)
        pe_update_batch_destroy(&threads[i]);
    pe_update_batch_destroy(&forward);
    pe_update_batch_destroy(&shuffled);
}

static void test_soa_group_layout(void)
{
    pe_update_batch_t batch = {0};
    double *deltas = NULL;
    double *averages = NULL;

    CHECK(pe_update_batch_soa_begin_group(
              &batch, 17u, 3u, 2u, &deltas, &averages) == 0,
          "SoA group allocation failed");
    if (deltas != NULL && averages != NULL)
    {
        size_t i;
        for (i = 0u; i < 6u; ++i)
        {
            deltas[i] = (double)i;
            averages[i] = (double)(i + 10u);
        }
        CHECK(batch.soa.group_count == 1u &&
                  batch.soa.groups[0].infoset == 17u &&
                  batch.soa.groups[0].actions == 3u &&
                  batch.soa.groups[0].combos == 2u &&
                  batch.soa.groups[0].offset == 0u &&
                  pe_update_soa_value_count(&batch.soa) == 6u &&
                  batch.soa.deltas[5u] == 5.0 &&
                  batch.soa.average_deltas[5u] == 15.0,
              "SoA group was not contiguous or action-major");
    }
    pe_update_batch_clear(&batch);
    CHECK(batch.soa.group_count == 0u && batch.soa.value_count == 0u,
          "SoA clear did not retain a reusable empty payload");
    pe_update_batch_destroy(&batch);
}

/*
 * Re-entering an infoset must return the span that is already in the batch, so
 * a traversal reaching the same infoset by two betting paths accumulates into
 * one group instead of creating a second one. This is the path the group index
 * answers; without a test, replacing the lookup would be unverifiable.
 */
static void test_soa_revisit_accumulates_in_place(void)
{
    pe_update_batch_t batch = {0};
    double *first_deltas = NULL;
    double *first_average = NULL;
    double *again_deltas = NULL;
    double *again_average = NULL;
    double *other_deltas = NULL;
    double *other_average = NULL;
    size_t i;

    CHECK(pe_update_batch_soa_begin_group(
              &batch, 42u, 2u, 3u, &first_deltas, &first_average) == 0,
          "SoA revisit: first group allocation failed");
    for (i = 0u; i < 6u; ++i)
    {
        first_deltas[i] = 1.0;
        first_average[i] = 2.0;
    }

    /* A different infoset in between, so a match cannot come from the group
       simply being the most recent one. */
    CHECK(pe_update_batch_soa_begin_group(
              &batch, 43u, 2u, 3u, &other_deltas, &other_average) == 0,
          "SoA revisit: second infoset allocation failed");
    CHECK(other_deltas != first_deltas,
          "SoA revisit: distinct infosets shared a span");

    CHECK(pe_update_batch_soa_begin_group(
              &batch, 42u, 2u, 3u, &again_deltas, &again_average) == 0,
          "SoA revisit: revisit failed");
    CHECK(again_deltas == first_deltas && again_average == first_average,
          "SoA revisit: revisit did not return the existing span");
    CHECK(batch.soa.group_count == 2u,
          "SoA revisit: revisit created a duplicate group");

    /* The span must not have been re-zeroed by the revisit. */
    for (i = 0u; i < 6u; ++i)
        again_deltas[i] += 1.0;
    CHECK(first_deltas[0] == 2.0 && first_deltas[5] == 2.0 &&
              first_average[0] == 2.0,
          "SoA revisit: revisit cleared values instead of accumulating");

    /* A same-id group with a different shape is a different group. */
    CHECK(pe_update_batch_soa_begin_group(
              &batch, 42u, 3u, 3u, &other_deltas, &other_average) == 0,
          "SoA revisit: reshaped group allocation failed");
    CHECK(other_deltas != first_deltas && batch.soa.group_count == 3u,
          "SoA revisit: a different shape reused an existing span");

    /* After a clear, the same infoset must allocate a fresh zeroed span. */
    pe_update_batch_clear(&batch);
    CHECK(pe_update_batch_soa_begin_group(
              &batch, 42u, 2u, 3u, &again_deltas, &again_average) == 0,
          "SoA revisit: post-clear allocation failed");
    CHECK(batch.soa.group_count == 1u && again_deltas[0] == 0.0 &&
              again_deltas[5] == 0.0,
          "SoA revisit: clear left a stale group or stale values");
    pe_update_batch_destroy(&batch);
}

/* Many distinct infosets, each revisited: the lookup must stay usable at a
   scale where a linear scan over the groups would not be. */
static void test_soa_many_infosets_revisited(void)
{
    pe_update_batch_t batch = {0};
    const pe_infoset_id_t count = 4096u;
    pe_infoset_id_t id;
    int ok = 1;

    for (id = 0u; id < count; ++id)
    {
        double *deltas = NULL;
        double *average = NULL;
        if (pe_update_batch_soa_begin_group(
                &batch, id, 2u, 2u, &deltas, &average) != 0)
        {
            ok = 0;
            break;
        }
        deltas[0] = (double)id;
    }
    CHECK(ok && batch.soa.group_count == (size_t)count,
          "SoA scale: distinct infosets were not all recorded");

    for (id = 0u; id < count && ok; ++id)
    {
        double *deltas = NULL;
        double *average = NULL;
        if (pe_update_batch_soa_begin_group(
                &batch, id, 2u, 2u, &deltas, &average) != 0 ||
            deltas[0] != (double)id)
            ok = 0;
    }
    CHECK(ok && batch.soa.group_count == (size_t)count,
          "SoA scale: revisiting resolved to the wrong span");
    pe_update_batch_destroy(&batch);
}

static void test_soa_reduction(void)
{
    pe_update_batch_t left = {0};
    pe_update_batch_t right = {0};
    pe_update_batch_t reduced = {0};
    pe_update_batch_source_t sources[2];
    double *left_deltas;
    double *left_average;
    double *right_deltas;
    double *right_average;

    CHECK(pe_update_batch_soa_begin_group(
              &left, 9u, 2u, 2u, &left_deltas, &left_average) == 0 &&
              pe_update_batch_soa_begin_group(
                  &right, 9u, 2u, 2u, &right_deltas, &right_average) == 0,
          "SoA reduction setup failed");
    if (left_deltas == NULL || right_deltas == NULL)
        goto cleanup;
    for (size_t i = 0u; i < 4u; ++i)
    {
        left_deltas[i] = 1.0;
        left_average[i] = 2.0;
        right_deltas[i] = 3.0;
        right_average[i] = 4.0;
    }
    left.iteration = 3u;
    right.iteration = 3u;
    sources[0] = (pe_update_batch_source_t){4u, &left};
    sources[1] = (pe_update_batch_source_t){1u, &right};
    CHECK(pe_update_batch_reduce(sources, 2u, &reduced) == 0 &&
              reduced.soa.group_count == 1u && reduced.soa.value_count == 4u &&
              reduced.soa.deltas[0] == 4.0 &&
              reduced.soa.average_deltas[3] == 6.0,
          "SoA reduction did not combine equal infosets");

cleanup:
    pe_update_batch_destroy(&left);
    pe_update_batch_destroy(&right);
    pe_update_batch_destroy(&reduced);
}

int main(void)
{
    test_large_batch_round_trip();
    test_merge_reduces_same_slot();
    test_reduction_preserves_iteration_metadata();
    test_reduction_ignores_arrival_order();
    test_soa_group_layout();
    test_soa_revisit_accumulates_in_place();
    test_soa_many_infosets_revisited();
    test_soa_reduction();
    if (failures != 0)
        return 1;
    puts("test_pe_batch: all tests passed");
    return 0;
}
