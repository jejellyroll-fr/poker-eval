/* GPU-06: shared host packing keeps storage updates deterministic. */

#include "compute_gpu_updates.h"

#include <stdio.h>

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
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t config = {0};
    pe_gpu_update_pack_t pack;
    pe_update_batch_t batch = {0};
    void *storage = NULL;
    pe_infoset_id_t id;
    const double *regrets;
    const double *averages;
    size_t length = 0u;

    CHECK(storage_ops->create(&storage, 1u) == 0, "storage creation failed");
    if (storage == NULL)
        return 1;
    id = storage_ops->resolve(storage, 0xBEEFu, 2u, 3u, PE_STREET_UNKNOWN);
    CHECK(id != PE_INFOSET_ID_INVALID, "infoset resolution failed");
    config.storage = storage_ops;
    config.storage_self = storage;
    CHECK(pe_update_batch_push(&batch, (pe_update_t){id, 1u, 2u, 1.25, 2.5}) == 0,
          "first update push failed");
    CHECK(pe_update_batch_push(&batch, (pe_update_t){id, 1u, 2u, 2.75, 3.5}) == 0,
          "duplicate update push failed");
    CHECK(pe_update_batch_push(&batch, (pe_update_t){id, 0u, 0u, 4.0, 5.0}) == 0,
          "second slot update push failed");
    CHECK(pe_gpu_update_pack_build(&config, &batch, &pack) == 0,
          "GPU update pack build failed");
    CHECK(pack.group_count == 1u && pack.total_slots == 6u && pack.count == 2u,
          "pack shape or duplicate reduction is wrong");
    CHECK(pack.slots[0] == 5u && pack.regret_deltas[0] == 4.0f &&
              pack.average_deltas[0] == 6.0f && pack.slots[1] == 0u,
          "pack did not preserve flattened slots and summed deltas");

    pack.regrets[5u] += pack.regret_deltas[0];
    pack.averages[5u] += pack.average_deltas[0];
    pack.regrets[0u] += pack.regret_deltas[1];
    pack.averages[0u] += pack.average_deltas[1];
    CHECK(pe_gpu_update_pack_commit(&pack) == 0, "pack commit failed");
    regrets = storage_ops->values_const(storage, id, PE_VALUES_REGRET, &length);
    averages = storage_ops->values_const(storage, id, PE_VALUES_AVERAGE,
                                         &length);
    CHECK(regrets != NULL && averages != NULL && regrets[5u] == 4.0 &&
              averages[5u] == 6.0 && regrets[0u] == 4.0 && averages[0u] == 5.0,
          "packed updates did not commit to storage");

    pe_gpu_update_pack_destroy(&pack);
    pe_update_batch_destroy(&batch);
    storage_ops->destroy(storage);
    return failures != 0;
}
