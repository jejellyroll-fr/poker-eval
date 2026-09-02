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

/*
 * Several infosets, interleaved and repeated, with duplicate slots in both.
 *
 * This is the shape the index has to get right: a group is discovered, another
 * intervenes, and the first is reached again. Groups must appear in first-seen
 * order, each infoset must own exactly one group, and duplicate slots must sum
 * into the entry that already exists rather than appending a second one.
 */
static void test_interleaved_infosets(void)
{
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t config = {0};
    pe_gpu_update_pack_t pack;
    pe_update_batch_t batch = {0};
    void *storage = NULL;
    pe_infoset_id_t a;
    pe_infoset_id_t b;
    pe_infoset_id_t c;

    CHECK(storage_ops->create(&storage, 3u) == 0, "interleaved: storage failed");
    if (storage == NULL)
        return;
    a = storage_ops->resolve(storage, 0xA0u, 2u, 2u, PE_STREET_UNKNOWN);
    b = storage_ops->resolve(storage, 0xB0u, 3u, 1u, PE_STREET_UNKNOWN);
    c = storage_ops->resolve(storage, 0xC0u, 2u, 2u, PE_STREET_UNKNOWN);
    CHECK(a != PE_INFOSET_ID_INVALID && b != PE_INFOSET_ID_INVALID &&
              c != PE_INFOSET_ID_INVALID,
          "interleaved: infoset resolution failed");
    config.storage = storage_ops;
    config.storage_self = storage;

    /* a, b, a again, c, b again, then a duplicate of a's very first slot. */
    CHECK(pe_update_batch_push(&batch, (pe_update_t){a, 0u, 0u, 1.0, 0.5}) == 0 &&
              pe_update_batch_push(&batch, (pe_update_t){b, 2u, 0u, 2.0, 1.0}) == 0 &&
              pe_update_batch_push(&batch, (pe_update_t){a, 1u, 1u, 3.0, 1.5}) == 0 &&
              pe_update_batch_push(&batch, (pe_update_t){c, 0u, 1u, 4.0, 2.0}) == 0 &&
              pe_update_batch_push(&batch, (pe_update_t){b, 0u, 0u, 5.0, 2.5}) == 0 &&
              pe_update_batch_push(&batch, (pe_update_t){a, 0u, 0u, 6.0, 3.0}) == 0,
          "interleaved: pushes failed");
    CHECK(pe_gpu_update_pack_build(&config, &batch, &pack) == 0,
          "interleaved: pack build failed");

    /* Three groups in first-seen order; a is not duplicated by its revisit. */
    CHECK(pack.group_count == 3u, "interleaved: %zu groups, expected 3",
          pack.group_count);
    CHECK(pack.infosets[0] == a && pack.infosets[1] == b &&
              pack.infosets[2] == c,
          "interleaved: groups are not in first-seen order");
    /* a: 2*2, b: 3*1, c: 2*2 */
    CHECK(pack.total_slots == 11u, "interleaved: %zu slots, expected 11",
          pack.total_slots);
    CHECK(pack.offsets[0] == 0u && pack.offsets[1] == 4u &&
              pack.offsets[2] == 7u && pack.offsets[3] == 11u,
          "interleaved: group offsets are wrong");

    /* Five distinct slots: a(0,0) was hit twice and must have been summed. */
    CHECK(pack.count == 5u, "interleaved: %zu entries, expected 5", pack.count);
    CHECK(pack.slots[0] == 0u && pack.regret_deltas[0] == 7.0f &&
              pack.average_deltas[0] == 3.5f,
          "interleaved: the repeated slot did not accumulate (%f)",
          (double)pack.regret_deltas[0]);
    /* b lives at offset 4, so b(2,0) is slot 6 and b(0,0) is slot 4. */
    CHECK(pack.slots[1] == 6u && pack.regret_deltas[1] == 2.0f,
          "interleaved: second infoset resolved to the wrong slot (%u)",
          pack.slots[1]);
    CHECK(pack.slots[4] == 4u && pack.regret_deltas[4] == 5.0f,
          "interleaved: revisited infoset resolved to the wrong slot (%u)",
          pack.slots[4]);
    /* c lives at offset 7, so c(0,1) is slot 8. */
    CHECK(pack.slots[3] == 8u && pack.regret_deltas[3] == 4.0f,
          "interleaved: third infoset resolved to the wrong slot (%u)",
          pack.slots[3]);

    pe_gpu_update_pack_destroy(&pack);
    pe_update_batch_destroy(&batch);
    storage_ops->destroy(storage);
}

/* An update naming an infoset the storage does not know must be refused, not
   packed into whatever group the lookup happens to land on. */
static void test_unknown_infoset_is_refused(void)
{
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t config = {0};
    pe_gpu_update_pack_t pack;
    pe_update_batch_t batch = {0};
    void *storage = NULL;
    pe_infoset_id_t id;

    CHECK(storage_ops->create(&storage, 1u) == 0, "unknown: storage failed");
    if (storage == NULL)
        return;
    id = storage_ops->resolve(storage, 0xD0u, 2u, 1u, PE_STREET_UNKNOWN);
    config.storage = storage_ops;
    config.storage_self = storage;
    CHECK(pe_update_batch_push(&batch, (pe_update_t){id, 0u, 0u, 1.0, 0.5}) == 0 &&
              pe_update_batch_push(
                  &batch, (pe_update_t){id + 4096u, 0u, 0u, 1.0, 0.5}) == 0,
          "unknown: pushes failed");
    CHECK(pe_gpu_update_pack_build(&config, &batch, &pack) != 0,
          "unknown: an unresolvable infoset was accepted");
    pe_update_batch_destroy(&batch);
    storage_ops->destroy(storage);
}

static void test_soa_updates_are_packed(void)
{
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_compute_config_t config = {0};
    pe_gpu_update_pack_t pack;
    pe_update_batch_t batch = {0};
    void *storage = NULL;
    pe_infoset_id_t id;
    double *deltas = NULL;
    double *average_deltas = NULL;

    CHECK(storage_ops->create(&storage, 1u) == 0,
          "soa: storage creation failed");
    if (storage == NULL)
        return;
    id = storage_ops->resolve(storage, 0x50u, 2u, 2u, PE_STREET_UNKNOWN);
    config.storage = storage_ops;
    config.storage_self = storage;
    CHECK(id != PE_INFOSET_ID_INVALID &&
              pe_update_batch_soa_begin_group(&batch, id, 2u, 2u,
                                              &deltas, &average_deltas) == 0,
          "soa: group creation failed");
    if (deltas != NULL && average_deltas != NULL)
    {
        deltas[0] = 1.0;
        deltas[1] = 2.0;
        deltas[2] = 3.0;
        deltas[3] = 4.0;
        average_deltas[0] = 0.5;
        average_deltas[1] = 1.5;
        average_deltas[2] = 2.5;
        average_deltas[3] = 3.5;
    }
    CHECK(batch.count == 0u && batch.soa.value_count == 4u,
          "soa: vector payload was not created");
    CHECK(pe_gpu_update_pack_build(&config, &batch, &pack) == 0,
          "soa: GPU update pack build failed");
    CHECK(pack.group_count == 1u && pack.total_slots == 4u &&
              pack.count == 4u,
          "soa: pack shape is wrong");
    CHECK(pack.slots[0] == 0u && pack.slots[1] == 1u &&
              pack.slots[2] == 2u && pack.slots[3] == 3u &&
              pack.regret_deltas[3] == 4.0f &&
              pack.average_deltas[3] == 3.5f,
          "soa: vector values were not flattened");
    pe_gpu_update_pack_destroy(&pack);
    pe_update_batch_destroy(&batch);
    storage_ops->destroy(storage);
}

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

    test_interleaved_infosets();
    test_unknown_infoset_is_refused();
    test_soa_updates_are_packed();
    return failures != 0;
}
