/* RBP-01: bounded regret-based pruning contract. */

#include <poker_eval/solver/pe_pruning.h>

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

static void test_invalid_config(const pe_pruning_ops_t *ops)
{
    void *ctx = NULL;
    pe_pruning_config_t config = {-10.0, 2u};
    CHECK(ops->create(&ctx, NULL) == -1 && ctx == NULL,
          "RBP accepted a NULL config");
    config.regret_threshold = 0.0;
    CHECK(ops->create(&ctx, &config) == -1 && ctx == NULL,
          "RBP accepted a non-negative threshold");
    config.regret_threshold = -10.0;
    config.revisit_interval = 0u;
    CHECK(ops->create(&ctx, &config) == -1 && ctx == NULL,
          "RBP accepted a zero revisit interval");
}

static void test_bounded_pruning(const pe_pruning_ops_t *ops)
{
    const pe_pruning_config_t config = {-10.0, 2u};
    const double regrets[] = {-20.0, -1.0, -30.0};
    const double all_negative[] = {-20.0, -30.0};
    uint8_t pruned[3] = {0};
    uint8_t fallback[2] = {0};
    pe_pruning_span_t span = {7u, 3u, regrets, pruned};
    pe_pruning_span_t fallback_span = {9u, 2u, all_negative, fallback};
    void *ctx = NULL;
    int state = -1;

    CHECK(ops->create(&ctx, &config) == 0 && ctx != NULL,
          "RBP creation failed");
    if (ctx == NULL)
        return;
    CHECK(ops->begin_iteration(ctx, 1u) == 0, "begin_iteration failed");
    CHECK(ops->evaluate(ctx, &span) == 0 && pruned[0] == 1u &&
              pruned[1] == 0u && pruned[2] == 1u,
          "RBP did not prune sufficiently negative actions");
    CHECK(ops->is_pruned(ctx, 7u, 0u, &state) == 0 && state == 1,
          "RBP state lookup failed");
    CHECK(ops->evaluate(ctx, &span) == 0 && pruned[0] == 1u,
          "RBP released an action before its revisit window");
    CHECK(ops->evaluate(ctx, &span) == 0 && pruned[0] == 1u,
          "RBP did not re-evaluate a still-negative action correctly");
    {
        const double recovered[] = {-1.0, -1.0, -30.0};
        span.cumulative_regrets = recovered;
        CHECK(ops->evaluate(ctx, &span) == 0 && pruned[0] == 1u,
              "RBP released an action before its revisit window");
        CHECK(ops->evaluate(ctx, &span) == 0 && pruned[0] == 0u,
              "RBP did not release an action after regret recovery");
    }
    CHECK(ops->evaluate(ctx, &fallback_span) == 0 && fallback[0] == 0u &&
              fallback[1] == 1u,
          "RBP pruned every action instead of retaining the best one");
    CHECK(ops->end_iteration(ctx, 1u) == 0, "end_iteration failed");
    CHECK(ops->begin_iteration(ctx, 0u) == -1,
          "RBP accepted a backwards iteration");
    ops->destroy(ctx);
}

int main(void)
{
    const pe_pruning_ops_t *ops = pe_pruning_rbp_ops();
    CHECK(ops != NULL && ops->name != NULL && ops->name[0] == 'r',
          "RBP adapter did not register");
    if (ops != NULL)
    {
        test_invalid_config(ops);
        test_bounded_pruning(ops);
    }
    return failures != 0;
}
