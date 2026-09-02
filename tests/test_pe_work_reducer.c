#include <poker_eval/solver/pe_work_reducer.h>

#include <assert.h>
#include <string.h>

static pe_work_result_t result(uint64_t state, uint64_t begin,
                               uint64_t end, const uint8_t *delta,
                               size_t delta_size)
{
    pe_work_result_t value = {0};
    value.public_state = state;
    value.iteration_begin = begin;
    value.iteration_end = end;
    value.backend = PE_COMPUTE_CPU_REF;
    value.exploitability = 1.0;
    value.worst_margin = 0.0;
    value.mean_margin = 0.0;
    value.delta = delta;
    value.delta_size = delta_size;
    return value;
}

static void test_ownership_and_overlap(void)
{
    uint8_t delta[] = {1u, 2u, 3u};
    pe_work_reducer_t reducer;
    pe_work_result_t first = result(10u, 0u, 4u, delta, sizeof(delta));
    pe_work_result_t adjacent = result(10u, 4u, 8u, NULL, 0u);
    pe_work_result_t overlap = result(10u, 3u, 5u, NULL, 0u);
    const pe_work_result_record_t *record;

    pe_work_reducer_init(&reducer);
    assert(pe_work_reducer_accept(&reducer, 11u, &first) == 0);
    assert(pe_work_reducer_accept(&reducer, 22u, &adjacent) == 0);
    assert(pe_work_reducer_accept(&reducer, 33u, &overlap) == -1);
    delta[0] = 99u;
    record = pe_work_reducer_get(&reducer, 0u);
    assert(record != NULL && record->delta_storage != NULL);
    assert(record->result.delta[0] == 1u);
    pe_work_reducer_destroy(&reducer);
    assert(pe_work_reducer_count(&reducer) == 0u);
}

static void test_stable_sort(void)
{
    pe_work_reducer_t reducer;
    pe_work_result_t low = result(1u, 4u, 5u, NULL, 0u);
    pe_work_result_t high = result(2u, 0u, 1u, NULL, 0u);
    pe_work_result_t middle = result(1u, 0u, 4u, NULL, 0u);

    pe_work_reducer_init(&reducer);
    assert(pe_work_reducer_accept(&reducer, 3u, &low) == 0);
    assert(pe_work_reducer_accept(&reducer, 2u, &high) == 0);
    assert(pe_work_reducer_accept(&reducer, 1u, &middle) == 0);
    pe_work_reducer_sort(&reducer);
    assert(pe_work_reducer_get(&reducer, 0u)->result.public_state == 1u);
    assert(pe_work_reducer_get(&reducer, 0u)->result.iteration_begin == 0u);
    assert(pe_work_reducer_get(&reducer, 1u)->result.public_state == 1u);
    assert(pe_work_reducer_get(&reducer, 2u)->result.public_state == 2u);
    assert(pe_work_reducer_get(&reducer, 3u) == NULL);
    pe_work_reducer_destroy(&reducer);
}

int main(void)
{
    test_ownership_and_overlap();
    test_stable_sort();
    return 0;
}
