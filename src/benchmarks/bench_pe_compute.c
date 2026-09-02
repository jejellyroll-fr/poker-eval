/*
 * bench_pe_compute.c - stable micro-benchmarks for the compute port (PERF-01)
 *
 * The benchmark intentionally uses only public compute/storage contracts. It
 * therefore runs on a CPU-only build and can use exactly the same workload
 * description when an optional backend is available.
 */

#include <poker_eval/solver/pe_compute.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BENCH_REPEATS 5
#define BENCH_INNER_LOOPS 128

static double seconds_now(void)
{
    clock_t ticks = clock();
    return (double)ticks / (double)CLOCKS_PER_SEC;
}

static double median_seconds(double samples[BENCH_REPEATS])
{
    int i;

    for (i = 1; i < BENCH_REPEATS; ++i)
    {
        double value = samples[i];
        int j = i;
        while (j > 0 && samples[j - 1] > value)
        {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = value;
    }
    return samples[BENCH_REPEATS / 2];
}

static const pe_compute_ops_t *select_backend(const char *name)
{
    if (name == NULL || strcmp(name, "cpu_ref") == 0)
        return pe_compute_cpu_ref_ops();
    if (strcmp(name, "cpu_par") == 0)
        return pe_compute_cpu_par_ops();
    if (strcmp(name, "cuda") == 0)
        return pe_compute_cuda_ops();
    if (strcmp(name, "opencl") == 0)
        return pe_compute_opencl_ops();
    return NULL;
}

static void print_row(const char *backend, const char *kernel,
                      size_t batch, uint16_t actions, uint16_t combos,
                      double seconds, size_t elements)
{
    double ns_per_element = elements != 0u && seconds > 0.0
        ? seconds * 1.0e9 / (double)elements : 0.0;
    double elements_per_second = seconds > 0.0
        ? (double)elements / seconds : 0.0;
    printf("%s,%s,%zu,%u,%u,%.6f,%.3f\n", backend, kernel, batch,
           (unsigned)actions, (unsigned)combos, ns_per_element,
           elements_per_second);
}

static int bench_strategy(const pe_compute_ops_t *ops, void *backend,
                          size_t batch, uint16_t actions)
{
    uint32_t *offsets;
    uint16_t *action_counts;
    float *regrets;
    float *strategies;
    pe_infoset_batch_t input;
    pe_strategy_batch_t output;
    size_t i;
    int repeat;
    size_t values = batch * (size_t)actions;
    double start;
    double elapsed;
    double samples[BENCH_REPEATS];

    offsets = (uint32_t *)malloc((batch + 1u) * sizeof(*offsets));
    action_counts = (uint16_t *)malloc(batch * sizeof(*action_counts));
    regrets = (float *)malloc(values * sizeof(*regrets));
    strategies = (float *)malloc(values * sizeof(*strategies));
    if (!offsets || !action_counts || !regrets || !strategies)
    {
        free(offsets); free(action_counts); free(regrets); free(strategies);
        return -1;
    }
    for (i = 0u; i < batch; ++i)
    {
        offsets[i] = (uint32_t)(i * (size_t)actions);
        action_counts[i] = actions;
    }
    offsets[batch] = (uint32_t)values;
    for (i = 0u; i < values; ++i)
        regrets[i] = (float)((int)(i % actions) - 1);
    input.count = batch;
    input.offsets = offsets;
    input.action_counts = action_counts;
    input.regrets = regrets;
    output.count = 0u;
    output.capacity = values;
    output.offsets = offsets;
    output.strategies = strategies;

    /* Warm the dispatch and instruction cache before recording the sample. */
    if (ops->strategy_batch(backend, &input, &output) != 0)
    {
        free(offsets); free(action_counts); free(regrets); free(strategies);
        return -1;
    }
    for (repeat = 0; repeat < BENCH_REPEATS; ++repeat)
    {
        int inner;
        start = seconds_now();
        for (inner = 0; inner < BENCH_INNER_LOOPS; ++inner)
            if (ops->strategy_batch(backend, &input, &output) != 0)
            {
                free(offsets); free(action_counts); free(regrets); free(strategies);
                return -1;
            }
        samples[repeat] = seconds_now() - start;
    }
    elapsed = median_seconds(samples) / (double)BENCH_INNER_LOOPS;
    print_row(ops->name, "strategy_batch", batch, actions, 0u, elapsed,
              values);
    free(offsets); free(action_counts); free(regrets); free(strategies);
    return 0;
}

static int bench_apply(const pe_compute_ops_t *ops, void *backend,
                       size_t batch, uint16_t actions, uint16_t combos)
{
    const pe_storage_ops_t *storage_ops = pe_storage_ram_ops();
    pe_update_batch_t updates = {0};
    void *storage = NULL;
    void *apply_backend = NULL;
    pe_infoset_id_t infoset;
    double *deltas;
    double *averages;
    size_t values = (size_t)actions * combos;
    size_t i;
    int repeat;
    double start;
    double elapsed;
    double samples[BENCH_REPEATS];

    (void)backend;
    if (!storage_ops || storage_ops->create(&storage, 1u) != 0)
        return -1;
    infoset = storage_ops->resolve(storage, UINT64_C(0xBEEF), actions, combos,
                                   PE_STREET_UNKNOWN);
    if (infoset == PE_INFOSET_ID_INVALID ||
        pe_update_batch_soa_begin_group(&updates, infoset, actions, combos,
                                        &deltas, &averages) != 0)
    {
        storage_ops->destroy(storage);
        pe_update_batch_destroy(&updates);
        return -1;
    }
    for (i = 0u; i < values; ++i)
    {
        deltas[i] = (double)((int)(i % 7u) - 3) * 0.01;
        averages[i] = 0.001;
    }
    {
        pe_compute_config_t config;
        memset(&config, 0, sizeof(config));
        config.cpu_threads = 1;
        config.deterministic = 1;
        config.storage = storage_ops;
        config.storage_self = storage;
        config.update_batch_size = values;
        config.averaging_mode = PE_AVG_UNIFORM;
        if (!ops->create || ops->create(&apply_backend, &config) != 0 ||
            apply_backend == NULL)
        {
            storage_ops->destroy(storage);
            pe_update_batch_destroy(&updates);
            return -1;
        }
    }
    updates.iteration = 1u;
    if (ops->apply_update_batch(apply_backend, &updates) != 0)
    {
        ops->destroy(apply_backend);
        storage_ops->destroy(storage);
        pe_update_batch_destroy(&updates);
        return -1;
    }
    for (repeat = 0; repeat < BENCH_REPEATS; ++repeat)
    {
        int inner;
        start = seconds_now();
        for (inner = 0; inner < BENCH_INNER_LOOPS; ++inner)
            if (ops->apply_update_batch(apply_backend, &updates) != 0)
            {
                ops->destroy(apply_backend);
                storage_ops->destroy(storage);
                pe_update_batch_destroy(&updates);
                return -1;
            }
        samples[repeat] = seconds_now() - start;
    }
    elapsed = median_seconds(samples) / (double)BENCH_INNER_LOOPS;
    print_row(ops->name, "apply_update_batch", batch, actions, combos,
              elapsed, values);
    ops->destroy(apply_backend);
    storage_ops->destroy(storage);
    pe_update_batch_destroy(&updates);
    return 0;
}

static int bench_terminal(const pe_compute_ops_t *ops, void *backend,
                          size_t batch, uint16_t actions, uint16_t combos)
{
    uint8_t *cards;
    uint32_t *values;
    pe_terminal_batch_t input;
    pe_value_batch_t output;
    size_t i;
    int repeat;
    double start;
    double elapsed;
    double samples[BENCH_REPEATS];

    (void)actions;
    cards = (uint8_t *)malloc(batch * 7u);
    values = (uint32_t *)malloc(batch * sizeof(*values));
    if (!cards || !values)
    {
        free(cards); free(values);
        return -1;
    }
    for (i = 0u; i < batch; ++i)
    {
        size_t card;
        for (card = 0u; card < 7u; ++card)
            cards[i * 7u + card] = (uint8_t)((card + i * 7u) % 52u);
    }
    input.game = game_holdem;
    input.cards = cards;
    input.hole = NULL;
    input.board = NULL;
    input.count = batch;
    output.values = values;
    output.capacity = batch;
    output.count = 0u;
    if (ops->terminal_eval_batch(backend, &input, &output) != 0)
    {
        free(cards); free(values);
        return -1;
    }
    for (repeat = 0; repeat < BENCH_REPEATS; ++repeat)
    {
        int inner;
        start = seconds_now();
        for (inner = 0; inner < BENCH_INNER_LOOPS; ++inner)
            if (ops->terminal_eval_batch(backend, &input, &output) != 0)
            {
                free(cards); free(values);
                return -1;
            }
        samples[repeat] = seconds_now() - start;
    }
    elapsed = median_seconds(samples) / (double)BENCH_INNER_LOOPS;
    print_row(ops->name, "terminal_eval_batch", batch, actions, combos,
              elapsed, batch);
    free(cards); free(values);
    return 0;
}

int main(int argc, char **argv)
{
    const char *backend_name = "cpu_ref";
    const pe_compute_ops_t *ops;
    pe_compute_config_t config;
    void *backend = NULL;
    const size_t batches[] = {1000u, 100000u, 1000000u};
    const uint16_t action_counts[] = {2u, 3u, 5u, 9u};
    const uint16_t combo_counts[] = {169u, 1326u, 65535u};
    size_t b;
    size_t a;
    size_t c;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
            backend_name = argv[++i];
        else if (strcmp(argv[i], "--csv") != 0)
        {
            fprintf(stderr, "usage: %s [--backend cpu_ref|cpu_par|cuda|opencl] --csv\n",
                    argv[0]);
            return 2;
        }
    }
    ops = select_backend(backend_name);
    if (!ops)
    {
        fprintf(stderr, "unknown backend: %s\n", backend_name);
        return 2;
    }
    memset(&config, 0, sizeof(config));
    config.cpu_threads = 1;
    config.deterministic = 1;
    config.averaging_mode = PE_AVG_UNIFORM;
    if (!ops->create || ops->create(&backend, &config) != 0 || !backend)
    {
        fprintf(stderr, "backend unavailable: %s\n", backend_name);
        return 1;
    }
    puts("backend,kernel,batch,actions,combos,ns_per_element,elements_per_s");
    for (b = 0u; b < sizeof(batches) / sizeof(batches[0]); ++b)
    {
        for (a = 0u; a < sizeof(action_counts) / sizeof(action_counts[0]); ++a)
            if (bench_strategy(ops, backend, batches[b], action_counts[a]) != 0)
                goto fail;
        for (c = 0u; c < sizeof(combo_counts) / sizeof(combo_counts[0]); ++c)
        {
            if (bench_apply(ops, backend, batches[b], 5u, combo_counts[c]) != 0)
                goto fail;
            if (bench_terminal(ops, backend, batches[b], 5u,
                               combo_counts[c]) != 0)
                goto fail;
        }
    }
    ops->destroy(backend);
    return 0;

fail:
    ops->destroy(backend);
    fprintf(stderr, "benchmark failed for backend %s\n", backend_name);
    return 1;
}
