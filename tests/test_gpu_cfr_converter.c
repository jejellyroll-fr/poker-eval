#include <assert.h>
#include <stdlib.h>

#include <poker_eval/gpu/gpu_cfr.h>
#include <poker_eval/engine/solvers/cfr/cfr_core.h>

static void populate_storage(cfr_storage_t *storage)
{
    /* Infoset A with 2 actions */
    const uint64_t key_a = 0xAAAULL;
    double regret_a[2] = {0.50, -0.25};
    double avg_a[2] = {0.25, 0.75};
    cfr_storage_update_regret(storage, key_a, 2, regret_a, 1.0);
    cfr_storage_update_avg(storage, key_a, 2, avg_a, 1.0);

    /* Infoset B with 3 actions */
    const uint64_t key_b = 0xBEEF1234ULL;
    double regret_b[3] = {0.10, 0.20, 0.30};
    double avg_b[3] = {0.4, 0.3, 0.3};
    cfr_storage_update_regret(storage, key_b, 3, regret_b, 1.0);
    cfr_storage_update_avg(storage, key_b, 3, avg_b, 1.0);

    /* Infoset C with single action */
    const uint64_t key_c = 0xC0FFEEULL;
    double regret_c[1] = {0.0};
    double avg_c[1] = {1.0};
    cfr_storage_update_regret(storage, key_c, 1, regret_c, 1.0);
    cfr_storage_update_avg(storage, key_c, 1, avg_c, 1.0);
}

static int find_row_for_key(
    const cfr_matrix_storage_t *matrix,
    uint64_t key)
{
    for (int row = 0; row < matrix->num_infosets; ++row)
    {
        if (matrix->infoset_keys[row] == key)
            return row;
    }
    return -1;
}

static void assert_row_equals(
    const cfr_matrix_storage_t *matrix,
    int row,
    int expected_actions,
    const double *expected_regret,
    const double *expected_avg)
{
    int max_actions = matrix->max_actions;

    assert(matrix->action_counts[row] == (uint8_t)expected_actions);

    for (int a = 0; a < expected_actions; ++a)
    {
        assert(matrix->regrets[row * max_actions + a] == (float)expected_regret[a]);
        assert(matrix->avg_strategy[row * max_actions + a] == (float)expected_avg[a]);
    }

    for (int a = expected_actions; a < max_actions; ++a)
    {
        assert(matrix->regrets[row * max_actions + a] == 0.0f);
        assert(matrix->avg_strategy[row * max_actions + a] == 0.0f);
    }
}

int main(void)
{
    const int max_actions = 4;

    cfr_storage_t *cpu_storage = cfr_storage_create();
    assert(cpu_storage != NULL);

    populate_storage(cpu_storage);

    cfr_matrix_storage_t *matrix = cfr_convert_to_matrix(cpu_storage, max_actions);
    assert(matrix != NULL);
    assert(matrix->num_infosets == (int)cfr_storage_count_infosets(cpu_storage));
    assert(matrix->max_actions == max_actions);

    int row = find_row_for_key(matrix, 0xAAAULL);
    assert(row >= 0);
    double regret_a[2] = {0.50, -0.25};
    double avg_a[2] = {0.25, 0.75};
    assert_row_equals(matrix, row, 2, regret_a, avg_a);

    row = find_row_for_key(matrix, 0xBEEF1234ULL);
    assert(row >= 0);
    double regret_b[3] = {0.10, 0.20, 0.30};
    double avg_b[3] = {0.4, 0.3, 0.3};
    assert_row_equals(matrix, row, 3, regret_b, avg_b);

    row = find_row_for_key(matrix, 0xC0FFEEULL);
    assert(row >= 0);
    double regret_c[1] = {0.0};
    double avg_c[1] = {1.0};
    assert_row_equals(matrix, row, 1, regret_c, avg_c);

    assert(cfr_matrix_validate(matrix) == 0);

    cfr_storage_t *roundtrip = cfr_storage_create();
    assert(roundtrip != NULL);
    assert(cfr_convert_from_matrix(matrix, roundtrip) == 0);

    /* Round-trip verification: convert back again and compare */
    cfr_matrix_storage_t *matrix_again = cfr_convert_to_matrix(roundtrip, max_actions);
    assert(matrix_again != NULL);
    assert(matrix_again->num_infosets == matrix->num_infosets);

    for (int r = 0; r < matrix_again->num_infosets; ++r)
    {
        uint64_t key = matrix_again->infoset_keys[r];
        int original_row = find_row_for_key(matrix, key);
        (void)original_row; // Suppress unused variable warning
        assert(original_row >= 0);

        assert(matrix_again->action_counts[r] == matrix->action_counts[original_row]);

        for (int a = 0; a < max_actions; ++a)
        {
            assert(matrix_again->regrets[r * max_actions + a] == matrix->regrets[original_row * max_actions + a]);
            assert(matrix_again->avg_strategy[r * max_actions + a] == matrix->avg_strategy[original_row * max_actions + a]);
        }
    }

    cfr_matrix_storage_free(matrix_again);
    cfr_storage_destroy(roundtrip);
    cfr_matrix_storage_free(matrix);
    cfr_storage_destroy(cpu_storage);

    return 0;
}
