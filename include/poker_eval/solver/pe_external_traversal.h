/* pe_external_traversal.h - External Sampling MCCFR (LNB-01). */

#ifndef POKER_EVAL_PE_EXTERNAL_TRAVERSAL_H
#define POKER_EVAL_PE_EXTERNAL_TRAVERSAL_H

#include <poker_eval/solver/pe_batch.h>
#include <poker_eval/solver/pe_capabilities.h>
#include <poker_eval/solver/pe_game_rules.h>
#include <poker_eval/solver/pe_sampling_policy.h>
#include <poker_eval/solver/pe_online_stats.h>
#include <poker_eval/solver/pe_storage_port.h>
#include <poker_eval/solver/pe_traversal.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_EXTERNAL_MAX_ACTIONS 64u

typedef struct pe_external_game_t
{
    const void *root;
    void *user;
    uint8_t player_count;

    int (*is_terminal)(const void *state, void *user);
    int (*acting_player)(const void *state, void *user);
    uint16_t (*action_count)(const void *state, void *user);
    uint64_t (*infoset_key)(const void *state, void *user);
    const void *(*apply_action)(const void *state, uint16_t action, void *user);

    /* Return the current behavioral probability of one action. NULL means
       uniform over the actions returned by action_count(). */
    double (*action_probability)(const void *state, uint64_t infoset_key,
                                 uint16_t action, void *user);
    double (*terminal_value)(const void *state, int player, void *user);

    /* Optional sampled chance layer. A non-zero sampler return means that the
       state is not a chance node and normal player callbacks are used. */
    pe_chance_sample_fn sample_chance;
    /* Context-aware form for adapters that own a sampler (for example the
       correlated Hold'em/PLO private-deal sampler). Takes precedence over the
       legacy callback when both are set. */
    int (*sample_chance_with_user)(const void *state, pe_rng_t *rng,
                                   pe_chance_sample_t *out, void *user);
    /* Optional direct sampler. It returns the already-created child state,
       avoiding an unbounded outcome index for a large private-deal space. It
       is called only when acting_player(state, user) is negative. */
    const void *(*sample_chance_child)(const void *state, pe_rng_t *rng,
                                       pe_chance_sample_t *out, void *user);
    const void *(*apply_chance)(const void *state, int outcome, void *user);

    /* Optional: bytes the adapter itself holds, on top of the solver's own
       storage.  A sampled lane discovers its footprint as it runs -- every
       new board is a new infoset -- so an up-front estimate cannot bound it
       and only the adapter knows what it has accumulated.  NULL reports 0. */
    size_t (*footprint_bytes)(void *user);

    /* Optional street tag of a PLAYER state (pe_holdem_street_t indexing,
       negative for "unknown").  Feeds the per-street traversal statistics;
       NULL means the adapter does not report streets. */
    int8_t (*street_of)(const void *state, void *user);

    /* Optional lifetime hook for adapters whose apply_action/apply_chance
       allocate a temporary child.  Sampling adapters that own a per-deal
       arena can leave this NULL and reclaim the arena at the chance boundary. */
    void (*release_state)(const void *state, void *user);

    /* Issue #233: number of chance outcomes at a chance node, for exact
       (deterministic) traversal.  apply_chance(state, outcome, user) with
       outcome in [0, count) must then reproduce every child exactly once.
       Outcomes are weighted uniformly, so an adapter with non-equiprobable
       events must expose one outcome per elementary outcome.  NULL means the
       adapter cannot enumerate chance; exact BR then refuses the game with an
       explicit error instead of silently sampling. */
    uint32_t (*chance_outcome_count)(const void *state, void *user);
} pe_external_game_t;

/* Issue #256: adaptive-variance groups, four street-tagged and four by
   chance depth. */
#define PE_ADAPTIVE_GROUP_COUNT 8

/* Telemetry of one adaptive group, summed over the updating players. */
typedef struct
{
    uint64_t estimates;  /* chance visits sampled adaptively */
    uint64_t samples;    /* draws those visits made */
    uint64_t min_hits;   /* visits drawn at the minimum budget */
    uint64_t max_hits;   /* visits drawn at the maximum budget */
    uint64_t resolved;   /* visits whose own confidence half-width met the
                            tolerance */
    pe_pooled_variance_t within; /* per-draw spread */
    pe_online_stats_t means;     /* visit values */
} pe_adaptive_group_stats_t;

/* Group name for reports: "street:flop", "chance-depth:1", ... */
const char *pe_adaptive_group_name(int group);

typedef struct
{
    const pe_external_game_t *game;
    const pe_storage_ops_t *storage_ops;
    void *storage;
    pe_rng_t rng;
    int updating_player;
    uint64_t iteration;
    size_t visited_nodes;
    size_t terminal_nodes;
    size_t sampled_chance_nodes;
    /* ISS-232: sampling policy and its per-street work table. The default
       (STANDARD, all replicates 1) is bit-identical to the pre-policy
       traversal, so existing callers change nothing by ignoring it. */
    pe_sampling_policy_t policy;
    uint16_t chance_replicates[PE_SAMPLING_STREET_COUNT];
    /* Cumulative per-street traversal statistics, reset by ctx_init. Streets
       the adapter does not tag are not counted. */
    size_t visits_by_street[PE_SAMPLING_STREET_COUNT];
    size_t updates_by_street[PE_SAMPLING_STREET_COUNT];
    size_t chance_samples_by_street[PE_SAMPLING_STREET_COUNT];
    int initialized;

    /* Issue #256: PE_SAMPLING_ADAPTIVE_VARIANCE state. Chance visits are
       grouped by the street the adapter tags them with (groups 0..3) or,
       when it does not say, by how many chance nodes lie above them on the
       trajectory (groups 4..7, depth 3 and deeper sharing group 7). Each
       updating player keeps its own statistics per group, since values are
       seen from its side. All of it is reset by ctx_init and by
       pe_external_sampling_set_adaptive. */
    pe_adaptive_sampling_t adaptive;   /* resolved settings */
    double adaptive_z;
    int chance_depth;                  /* chance nodes above the current visit */
    /* Product of the adaptive replicate counts above the current visit.
       max_samples caps it, so nested chance nodes cannot multiply a
       trajectory's work past that bound. */
    uint64_t replicate_product;
    pe_online_stats_t adaptive_means[PE_TRAVERSAL_MAX_PLAYERS]
                                    [PE_ADAPTIVE_GROUP_COUNT];
    pe_pooled_variance_t adaptive_within[PE_TRAVERSAL_MAX_PLAYERS]
                                        [PE_ADAPTIVE_GROUP_COUNT];
    uint32_t adaptive_budget[PE_TRAVERSAL_MAX_PLAYERS][PE_ADAPTIVE_GROUP_COUNT];
    uint64_t adaptive_next_check[PE_TRAVERSAL_MAX_PLAYERS]
                                [PE_ADAPTIVE_GROUP_COUNT];
    pe_adaptive_group_stats_t adaptive_stats[PE_ADAPTIVE_GROUP_COUNT];
    /* Terminal evaluations over the ctx's whole life (terminal_nodes counts
       one run only). */
    uint64_t total_terminal_nodes;
} pe_external_sampling_ctx_t;

int pe_external_sampling_ctx_init(pe_external_sampling_ctx_t *ctx,
                                  const pe_external_game_t *game,
                                  const pe_storage_ops_t *storage_ops,
                                  void *storage,
                                  int updating_player,
                                  uint64_t seed);
void pe_external_sampling_ctx_destroy(pe_external_sampling_ctx_t *ctx);

/* Select the sampling policy. `street_replicates` is a
   PE_SAMPLING_STREET_COUNT table of replicate counts per street; NULL or a
   zero entry means one. Only PE_SAMPLING_STREET_BALANCED reads the table. */
void pe_external_sampling_set_policy(pe_external_sampling_ctx_t *ctx,
                                     pe_sampling_policy_t policy,
                                     const uint16_t *street_replicates);

/* Issue #256: what the adaptive policy has learned, per updating player and
   group. A checkpoint carries it so a resumed solve keeps its budgets
   instead of re-running the pilot. */
typedef struct
{
    uint32_t budget[PE_TRAVERSAL_MAX_PLAYERS][PE_ADAPTIVE_GROUP_COUNT];
    uint64_t next_check[PE_TRAVERSAL_MAX_PLAYERS][PE_ADAPTIVE_GROUP_COUNT];
    pe_online_stats_t means[PE_TRAVERSAL_MAX_PLAYERS][PE_ADAPTIVE_GROUP_COUNT];
    pe_pooled_variance_t within[PE_TRAVERSAL_MAX_PLAYERS]
                               [PE_ADAPTIVE_GROUP_COUNT];
} pe_adaptive_state_t;

/* Serialised size: a tag, then per player and group a budget (4 bytes) and
   next check, n, mean, m2, dof and ssd (8 bytes each). */
#define PE_ADAPTIVE_STATE_BYTES                                          \
    (8u + (size_t)PE_TRAVERSAL_MAX_PLAYERS * PE_ADAPTIVE_GROUP_COUNT *   \
              (4u + 6u * 8u))

void pe_external_sampling_get_adaptive_state(const pe_external_sampling_ctx_t *ctx,
                                             pe_adaptive_state_t *out);
/* Restore learned state; call after pe_external_sampling_set_adaptive. */
void pe_external_sampling_set_adaptive_state(pe_external_sampling_ctx_t *ctx,
                                             const pe_adaptive_state_t *state);
/* Write the state field by field, native byte order (checkpoints record and
   check theirs). @return PE_ADAPTIVE_STATE_BYTES, or 0 when it does not
   fit. */
size_t pe_adaptive_state_serialize(const pe_adaptive_state_t *state,
                                   unsigned char *out, size_t capacity);
/* @return 0, or -1 for a size or tag mismatch or a non-finite value. */
int pe_adaptive_state_deserialize(pe_adaptive_state_t *state,
                                  const unsigned char *in, size_t size);

/* Issue #256: install adaptive-variance settings (NULL means defaults) and
   reset the adaptive statistics. pe_external_sampling_set_policy installs
   the defaults itself when it selects PE_SAMPLING_ADAPTIVE_VARIANCE, so this
   is only needed to override them. @return 0, or -1 when the settings are
   invalid (see pe_adaptive_sampling_resolve), leaving the ctx unchanged. */
int pe_external_sampling_set_adaptive(pe_external_sampling_ctx_t *ctx,
                                      const pe_adaptive_sampling_t *settings);

/* Run one external-sampling iteration for ctx->updating_player. */
int pe_external_sampling_run(pe_external_sampling_ctx_t *ctx,
                             pe_update_batch_t *out_batch);

static inline uint64_t pe_external_sampling_required_caps(void)
{
    return PE_CAP_DIRECT_CHANCE_SAMPLING;
}

#ifdef __cplusplus
}
#endif

#endif /* PE_EXTERNAL_TRAVERSAL_H */
