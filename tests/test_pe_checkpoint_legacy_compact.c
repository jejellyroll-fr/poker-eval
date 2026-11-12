/*
 * test_pe_checkpoint_legacy_compact.c - API-04: the compacted legacy format
 *
 * Before API-04 checkpoints were a compacted format: the magic is "CFRCHKPT",
 * a record stores a slot count instead of an action x combo shape, and nothing
 * is checksummed. pe_persist_checkpoint_ops() still routes that magic to the
 * legacy reader, and no writer emits it any more, so this fixture builds the
 * bytes by hand.
 *
 * Layout (native endianness, as the reader consumes it):
 *   "CFRCHKPT" | version u32 | reserved u32 | capacity u64 | count u64 |
 *   iteration u64 | records
 * A record is: key u64 | slots u32 | ev_sum f64 |
 *   (version >= 2) ev_sq_sum f64 | ev_count u64 |
 *   regret[slots] f64 | average[slots] f64 |
 * A record's slots are its actions, with a single combo.
 */

#include <poker_eval/solver/pe_persist.h>
#include <poker_eval/solver/pe_ports.h>

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
            failures++;                                            \
        }                                                          \
    } while (0)

static const char *test_tmp_path(const char *name)
{
    static char path[512];
    const char *directory = getenv("PE_TEST_TMPDIR");
    int written;

    if (!directory || !*directory)
        directory = ".";
    written = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (written < 0 || (size_t)written >= sizeof(path))
        return NULL;
    return path;
}

static int put(FILE *file, const void *bytes, size_t count)
{
    return fwrite(bytes, 1u, count, file) == count ? 0 : -1;
}

static int put_u32(FILE *file, uint32_t value)
{
    return put(file, &value, sizeof(value));
}

static int put_u64(FILE *file, uint64_t value)
{
    return put(file, &value, sizeof(value));
}

static int put_f64(FILE *file, double value)
{
    return put(file, &value, sizeof(value));
}

static int write_compact(const char *path, uint32_t version, uint64_t capacity,
                         uint64_t iteration, uint64_t key, uint32_t slots,
                         const double *regret, const double *average)
{
    FILE *file = fopen(path, "w+b");
    size_t i;

    if (!file)
        return -1;
    if (put(file, "CFRCHKPT", 8u) != 0 || put_u32(file, version) != 0 ||
        put_u32(file, 0u) != 0 || put_u64(file, capacity) != 0 ||
        put_u64(file, 1u) != 0 || put_u64(file, iteration) != 0 ||
        put_u64(file, key) != 0 || put_u32(file, slots) != 0 ||
        put_f64(file, 0.5) != 0 ||
        (version >= 2u && put_f64(file, 0.25) != 0) ||
        put_u64(file, 2u) != 0)
        goto fail;
    for (i = 0u; i < (size_t)slots; ++i)
        if (put_f64(file, regret[i]) != 0)
            goto fail;
    for (i = 0u; i < (size_t)slots; ++i)
        if (put_f64(file, average[i]) != 0)
            goto fail;
    return fclose(file) == 0 ? 0 : -1;
fail:
    fclose(file);
    return -1;
}

static int load_and_verify(uint32_t version, const char *name,
                           uint64_t iteration)
{
    const pe_storage_ops_t *ops = pe_storage_ram_ops();
    const pe_persist_ops_t *persist = pe_persist_checkpoint_ops();
    const char *path = test_tmp_path(name);
    pe_solver_config_t config = pe_solver_config_default();
    pe_persist_source_t source = {0};
    void *storage = NULL;
    pe_infoset_id_t id;
    pe_combo_count_t combos = 0u;
    uint16_t actions = 0u;
    uint64_t loaded_iteration = 0u;
    const double *regret = NULL;
    const double *average = NULL;
    const double regret_in[3] = {1.5, -2.25, 0.125};
    const double average_in[3] = {0.5, 0.75, -1.0};
    size_t i;
    int status = 1;

    if (!ops || !persist || !path)
    {
        CHECK(0, "checkpoint dependencies unavailable");
        return 1;
    }
    CHECK(write_compact(path, version, 4u, iteration, UINT64_C(0x5151), 3u,
                        regret_in, average_in) == 0,
          "fixture write failed");
    CHECK(ops->create(&storage, 1u) == 0, "storage creation failed");
    if (!storage)
        goto done;

    source.path = path;
    CHECK(persist->load(NULL, &source, &config, ops, storage,
                        &loaded_iteration) == 0,
          "the compacted version-%u fixture was refused", version);
    CHECK(loaded_iteration == iteration, "restored iteration is %llu",
          (unsigned long long)loaded_iteration);
    id = ops->find(storage, UINT64_C(0x5151));
    CHECK(id != PE_INFOSET_ID_INVALID,
          "the compacted infoset was not restored");
    if (id == PE_INFOSET_ID_INVALID)
        goto done;
    CHECK(ops->shape(storage, id, &actions, &combos, NULL) == 0 &&
              actions == 3u && combos == 1u,
          "restored shape is %u x %u", actions, combos);
    regret = ops->values_const(storage, id, PE_VALUES_REGRET, NULL);
    average = ops->values_const(storage, id, PE_VALUES_AVERAGE, NULL);
    CHECK(regret != NULL && average != NULL, "restored spans are missing");
    if (!regret || !average)
        goto done;
    for (i = 0u; i < 3u; ++i)
    {
        CHECK(fabs(regret[i] - regret_in[i]) < 1e-15,
              "regret %zu is %.17g, expected %.17g", i, regret[i], regret_in[i]);
        CHECK(fabs(average[i] - average_in[i]) < 1e-15,
              "average %zu is %.17g, expected %.17g", i, average[i],
              average_in[i]);
    }
    status = 0;

done:
    if (ops)
        ops->destroy(storage);
    remove(path);
    return status;
}

int main(void)
{
    /* Version 1 has no second-moment field; version 2 added it. Both predate
       the API-04 flat format and must still load. */
    (void)load_and_verify(1u, "poker_eval_checkpoint_compact_v1.bin", 7u);
    (void)load_and_verify(2u, "poker_eval_checkpoint_compact_v2.bin", 9u);

    /* The header's capacity must be a power of two: a hand-made file that is
       not is refused, which shows the fixture really goes through the legacy
       header parser rather than being ignored. */
    {
        const pe_storage_ops_t *ops = pe_storage_ram_ops();
        const pe_persist_ops_t *persist = pe_persist_checkpoint_ops();
        const char *path = test_tmp_path("poker_eval_checkpoint_compact_bad.bin");
        pe_solver_config_t config = pe_solver_config_default();
        pe_persist_source_t source = {0};
        const double values[1] = {1.0};
        void *storage = NULL;
        uint64_t iteration = 0u;

        CHECK(ops != NULL && persist != NULL && path != NULL,
              "checkpoint dependencies unavailable");
        if (ops && persist && path)
        {
            CHECK(write_compact(path, 2u, 3u, 1u, UINT64_C(0x52), 1u, values,
                                values) == 0,
                  "bad-capacity fixture write failed");
            CHECK(ops->create(&storage, 1u) == 0,
                  "storage creation failed");
            source.path = path;
            CHECK(persist->load(NULL, &source, &config, ops, storage,
                                &iteration) != 0,
                  "a non-power-of-two capacity was accepted");
            ops->destroy(storage);
            remove(path);
        }
    }

    if (failures != 0)
    {
        fprintf(stderr, "test_pe_checkpoint_legacy_compact: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("test_pe_checkpoint_legacy_compact: the CFRCHKPT format still loads");
    return 0;
}
