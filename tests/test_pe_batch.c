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
    CHECK(pe_update_batch_merge(&destination, &source) == 0, "merge failed");
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

int main(void)
{
    test_large_batch_round_trip();
    test_merge_reduces_same_slot();
    if (failures != 0)
        return 1;
    puts("test_pe_batch: all tests passed");
    return 0;
}
