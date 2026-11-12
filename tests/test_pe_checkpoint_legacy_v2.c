/*
 * test_pe_checkpoint_legacy_v2.c - API-04: the v4 reader still loads a v2 file
 *
 * Version 4 widened the stored combo count from 16 to 32 bits. This fixture
 * writes a genuine version-2 file with the same writer (through the versioned
 * adapter entry point) and loads it with the current adapter, so the versioned
 * field read is exercised against real bytes rather than a hand-made blob.
 *
 * Version 2 cannot represent a range wider than 65,535 combos: the writer must
 * refuse it instead of truncating the count.
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

int main(void)
{
    const pe_storage_ops_t *ops = pe_storage_ram_ops();
    const pe_persist_ops_t *legacy = pe_persist_checkpoint_ops_versioned(2u);
    const pe_persist_ops_t *current = pe_persist_checkpoint_ops();
    const char *path = test_tmp_path("poker_eval_checkpoint_v2_test.bin");
    pe_solver_config_t config = pe_solver_config_default();
    pe_persist_target_t target = {0};
    pe_persist_source_t source = {0};
    void *left = NULL;
    void *right = NULL;
    void *too_wide = NULL;
    pe_infoset_id_t id;
    pe_infoset_id_t restored;
    pe_combo_count_t combos = 0u;
    uint16_t actions = 0u;
    size_t length = 0u;
    uint64_t iteration = 0u;
    double *values;
    const double *loaded;

    CHECK(ops != NULL && legacy != NULL && current != NULL && path != NULL,
          "checkpoint dependencies unavailable");
    if (!ops || !legacy || !current || !path)
        return 1;
    CHECK(pe_persist_checkpoint_ops_versioned(1u) == NULL &&
              pe_persist_checkpoint_ops_versioned(3u) == NULL,
          "the versioned adapter offered a format it cannot write");

    config.problem.expected_infosets = 1u;
    config.problem.expected_actions = 2u;
    config.problem.expected_combos = 1326u;
    target.path = path;
    target.game_hash = UINT64_C(0x11112222);
    target.tree_hash = UINT64_C(0x33334444);
    source.path = path;
    source.game_hash = UINT64_C(0x11112222);
    source.tree_hash = UINT64_C(0x33334444);

    CHECK(ops->create(&left, 1u) == 0 && ops->create(&right, 1u) == 0,
          "storage creation failed");
    if (!left || !right)
        goto done;
    id = ops->resolve(left, UINT64_C(0x77), 2u, 1326u, 2);
    CHECK(id != PE_INFOSET_ID_INVALID, "infoset resolution failed");
    if (id == PE_INFOSET_ID_INVALID)
        goto done;
    values = ops->values(left, id, PE_VALUES_REGRET, &length);
    CHECK(values != NULL && length == 2652u,
          "regret slab has %zu slots, expected 2652", length);
    if (!values)
        goto done;
    values[0] = 1.25;
    values[length - 1u] = -3.5;

    CHECK(legacy->save(NULL, &target, &config, ops, left, 42u) == 0,
          "version-2 save failed");
    CHECK(current->load(NULL, &source, &config, ops, right, &iteration) == 0,
          "the current reader refused a version-2 checkpoint");
    CHECK(iteration == 42u, "restored iteration is %llu",
          (unsigned long long)iteration);
    restored = ops->find(right, UINT64_C(0x77));
    CHECK(restored != PE_INFOSET_ID_INVALID,
          "the version-2 infoset was not restored");
    CHECK(ops->shape(right, restored, &actions, &combos, NULL) == 0 &&
              actions == 2u && combos == 1326u,
          "restored shape is %u x %u", actions, combos);
    loaded = ops->values_const(right, restored, PE_VALUES_REGRET, &length);
    CHECK(loaded != NULL && length == 2652u,
          "restored regret slab has %zu slots", length);
    if (loaded)
        CHECK(fabs(loaded[0] - 1.25) < 1e-15 &&
                  fabs(loaded[2651] + 3.5) < 1e-15,
              "the version-2 values changed during the round-trip");

    /* A 16-bit combo count cannot hold 65,536: the writer must fail, not
       truncate the count or the slab. */
    CHECK(ops->create(&too_wide, 1u) == 0,
          "wide storage creation failed");
    if (too_wide)
    {
        id = ops->resolve(too_wide, UINT64_C(0x78), 1u, 65536u, 2);
        CHECK(id != PE_INFOSET_ID_INVALID, "wide infoset resolution failed");
        if (id != PE_INFOSET_ID_INVALID)
            CHECK(legacy->save(NULL, &target, &config, ops, too_wide, 43u) != 0,
                  "the version-2 writer truncated a 65,536-combo infoset");
    }

done:
    if (ops != NULL)
    {
        ops->destroy(left);
        ops->destroy(right);
        ops->destroy(too_wide);
    }
    remove(path);
    if (failures != 0)
    {
        fprintf(stderr, "test_pe_checkpoint_legacy_v2: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("test_pe_checkpoint_legacy_v2: a version-2 checkpoint still loads");
    return 0;
}
