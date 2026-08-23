/* API-04: portable checkpoint round-trip over the storage port. */

#include <poker_eval/solver/pe_persist.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                          \
            fputc('\n', stderr);                                   \
            failures++;                                             \
        }                                                          \
    } while (0)

int main(void)
{
    const char *path = "/tmp/poker_eval_checkpoint_v2_test.bin";
    const pe_storage_ops_t *ops = pe_storage_ram_ops();
    const pe_persist_ops_t *persist = pe_persist_checkpoint_ops();
    pe_solver_config_t config = pe_solver_config_default();
    pe_persist_target_t target = {path};
    pe_persist_source_t source = {path};
    void *left = NULL;
    void *right = NULL;
    pe_infoset_id_t left_id;
    pe_infoset_id_t right_id;
    uint64_t iteration = 0u;
    uint64_t key = 0u;
    uint16_t actions = 0u;
    uint16_t combos = 0u;
    uint8_t flags = 0u;
    size_t length = 0u;
    double *regret;
    double *average;
    const double *restored_regret;
    const double *restored_average;

    config.problem.expected_infosets = 1u;
    config.problem.expected_actions = 2u;
    config.problem.expected_combos = 2u;
    CHECK(ops != NULL && persist != NULL, "checkpoint dependencies unavailable");
    CHECK(ops->create(&left, 1u) == 0 && ops->create(&right, 1u) == 0,
          "storage creation failed");
    if (!left || !right)
        goto done;

    left_id = ops->resolve(left, 0xfeedu, 2u, 2u, 3);
    CHECK(left_id != PE_INFOSET_ID_INVALID, "infoset resolution failed");
    if (left_id == PE_INFOSET_ID_INVALID)
        goto done;
    regret = ops->values(left, left_id, PE_VALUES_REGRET, &length);
    CHECK(regret != NULL && length == 4u, "regret slab shape is incorrect");
    if (!regret)
        goto done;
    regret[0] = 1.0;
    regret[1] = -2.0;
    regret[2] = 3.5;
    regret[3] = 4.25;
    average = ops->values(left, left_id, PE_VALUES_AVERAGE, &length);
    CHECK(average != NULL && length == 4u, "average slab shape is incorrect");
    if (!average)
        goto done;
    average[0] = 0.25;
    average[1] = 0.75;
    average[2] = 0.6;
    average[3] = 0.4;
    CHECK(ops->set_flags(left, left_id, 0x05u, 0u) == 0,
          "setting infoset flags failed");
    CHECK(persist->save(NULL, &target, &config, ops, left, 500u) == 0,
          "checkpoint save failed");
    CHECK(persist->load(NULL, &source, &config, ops, right, &iteration) == 0,
          "checkpoint load failed");
    CHECK(iteration == 500u, "restored iteration is %llu",
          (unsigned long long)iteration);

    right_id = ops->find(right, 0xfeedu);
    CHECK(right_id != PE_INFOSET_ID_INVALID, "restored infoset was not indexed");
    if (right_id == PE_INFOSET_ID_INVALID)
        goto done;
    CHECK(ops->key_at(right, right_id, &key) == 0 && key == 0xfeedu,
          "restored key is incorrect");
    CHECK(ops->shape(right, right_id, &actions, &combos, NULL) == 0 &&
              actions == 2u && combos == 2u,
          "restored shape is incorrect");
    CHECK(ops->get_flags(right, right_id, &flags) == 0 && flags == 0x05u,
          "restored flags are incorrect");
    restored_regret = ops->values_const(right, right_id, PE_VALUES_REGRET,
                                        &length);
    restored_average = ops->values_const(right, right_id, PE_VALUES_AVERAGE,
                                         NULL);
    CHECK(restored_regret != NULL && restored_average != NULL && length == 4u,
          "restored value slabs are unavailable");
    if (restored_regret && restored_average)
    {
        CHECK(fabs(restored_regret[2] - 3.5) < 1e-15 &&
                  fabs(restored_average[1] - 0.75) < 1e-15,
              "restored values changed during round-trip");
    }

done:
    if (left)
        ops->destroy(left);
    if (right)
        ops->destroy(right);
    remove(path);
    return failures != 0;
}
