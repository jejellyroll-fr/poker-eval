/*
 * cfr_storage.c - Simple hash-map storage for regrets and avg strategy
 */

#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>

#include "../../../solver/domain/finite_double.h"

#ifdef PE_LEGACY_CFR_OPENMP
#include <omp.h>
#endif

#if defined(HAS_AVX2)
#include <immintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#define PE_CFR_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define PE_CFR_TARGET_AVX2
#endif
#else
#define PE_CFR_TARGET_AVX2
#endif
#if defined(HAS_NEON) && (defined(__aarch64__) || defined(__arm64__))
#include <arm_neon.h>
#endif

static PE_CFR_TARGET_AVX2 double cfr_sum_positive(const double *values, int count)
{
#if defined(HAS_AVX2)
    __m256d sum = _mm256_setzero_pd();
    const __m256d zero = _mm256_setzero_pd();
    int i = 0;
    for (; i + 4 <= count; i += 4)
    {
        __m256d v = _mm256_loadu_pd(values + i);
        sum = _mm256_add_pd(sum, _mm256_max_pd(v, zero));
    }
    double lanes[4];
    _mm256_storeu_pd(lanes, sum);
    double total = lanes[0] + lanes[1] + lanes[2] + lanes[3];
    for (; i < count; ++i)
        if (values[i] > 0.0)
            total += values[i];
    return total;
#elif defined(HAS_NEON) && (defined(__aarch64__) || defined(__arm64__))
    float64x2_t sum = vdupq_n_f64(0.0);
    const float64x2_t zero = vdupq_n_f64(0.0);
    int i = 0;
    for (; i + 2 <= count; i += 2)
    {
        float64x2_t v = vld1q_f64(values + i);
        sum = vaddq_f64(sum, vmaxq_f64(v, zero));
    }
    double lanes[2];
    vst1q_f64(lanes, sum);
    double total = lanes[0] + lanes[1];
    for (; i < count; ++i)
        if (values[i] > 0.0)
            total += values[i];
    return total;
#else
    double total = 0.0;
    for (int i = 0; i < count; ++i)
        if (values[i] > 0.0)
            total += values[i];
    return total;
#endif
}

#if defined(_MSC_VER)
#define CFR_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define CFR_THREAD_LOCAL __thread
#else
#define CFR_THREAD_LOCAL _Thread_local
#endif


static int keep_for_street(uint32_t mask, int street)
{
    return mask == 0 || street < 0 || street >= 32 || (mask & (1u << street));
}

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t reserved;
    uint64_t cap;
    uint64_t entry_count;
    uint64_t iteration;
} cfr_checkpoint_header_t;

/* Compiled-in upper bound for checkpoint hash-table capacity.
   A checkpoint whose cap exceeds this is treated as corrupt. */
#define CFR_MAX_CHECKPOINT_CAP ((size_t)1u << 26)

/* Forward declarations */
static size_t next_pow2(size_t x);

static void cfr_storage_free_entries(cfr_storage_t *s)
{
    if (!s || !s->tab)
        return;
    for (size_t i = 0; i < s->cap; ++i)
    {
        if (s->tab[i].used)
        {
            free(s->tab[i].regret);
            free(s->tab[i].avg);
            free(s->tab[i].locked);
            s->tab[i].regret = NULL;
            s->tab[i].avg = NULL;
            s->tab[i].locked = NULL;
            s->tab[i].used = 0;
        }
    }
}

static int cfr_storage_resize(cfr_storage_t *s, size_t new_cap)
{
    if (!s)
    {
        errno = EINVAL;
        return -1;
    }
    if (new_cap == 0)
        new_cap = 1;
    new_cap = next_pow2(new_cap);
    if (new_cap == 0)
    {
        errno = EINVAL;
        return -1;
    }
    if (s->tab && s->cap == new_cap)
    {
        cfr_storage_free_entries(s);
        memset(s->tab, 0, s->cap * sizeof(entry_t));
        s->used_count = 0;
        return 0;
    }
    cfr_storage_free_entries(s);
    free(s->tab);
    s->tab = (entry_t *)calloc(new_cap, sizeof(entry_t));
    if (!s->tab)
    {
        s->cap = 0;
        return -1;
    }
    s->cap = new_cap;
    s->used_count = 0;
    return 0;
}

/* Grow the table to new_cap (power of two) and reinsert every live entry.
 * Unlike cfr_storage_resize this preserves the loaded entries. */
static int cfr_storage_rehash(cfr_storage_t *s, size_t new_cap)
{
    if (!s || !s->tab || s->cap == 0)
    {
        errno = EINVAL;
        return -1;
    }
    new_cap = next_pow2(new_cap);
    if (new_cap <= s->cap)
    {
        errno = EINVAL;
        return -1;
    }
    entry_t *new_tab = (entry_t *)calloc(new_cap, sizeof(entry_t));
    if (!new_tab)
        return -1;

    size_t new_mask = new_cap - 1;
    for (size_t i = 0; i < s->cap; ++i)
    {
        if (!s->tab[i].used)
            continue;
        /* Copy the whole entry (including regret/avg pointers) into the new tab */
        entry_t move = s->tab[i];
        size_t j = (size_t)(move.key * 11400714819323198485ull) & new_mask;
        while (new_tab[j].used)
            j = (j + 1) & new_mask;
        new_tab[j] = move;
        /* Mark the source entry as dead so cfr_storage_free_entries never
           touches the regret/avg buffers now owned by the new slot. */
        s->tab[i].regret = NULL;
        s->tab[i].avg = NULL;
        s->tab[i].used = 0;
    }
    free(s->tab);
    s->tab = new_tab;
    s->cap = new_cap;
    return 0;
}

/* Enforce a load factor: grow the table (x2) once used_count exceeds cap*0.7.
 * Returns 0 on success or when no growth is needed, -1 on allocation failure. */
static int cfr_storage_ensure_capacity(cfr_storage_t *s)
{
    if (!s || s->cap == 0)
        return -1;
    if (s->used_count <= s->cap / 2 + s->cap / 5) /* ~0.7 load factor */
        return 0;
    return cfr_storage_rehash(s, s->cap * 2);
}

static size_t cfr_storage_entry_count(cfr_storage_t *s)
{
    if (!s || !s->tab)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < s->cap; ++i)
        if (s->tab[i].used)
            count++;
    return count;
}

static size_t next_pow2(size_t x)
{
    /* No power of two >= x fits in size_t once x exceeds 2^(bits-1):
       return 0 to signal overflow instead of wrapping to 0 and looping
       forever. */
    if (x > (SIZE_MAX >> 1) + 1)
        return 0;
    size_t p = 1;
    while (p < x)
        p <<= 1;
    return p;
}

void cfr_storage_set_strategy_mode_for(cfr_storage_t *s, int use_ecfr, double ecfr_lambda)
{
    if (!s)
        return;
    s->use_ecfr = use_ecfr ? 1 : 0;
    s->ecfr_lambda = (ecfr_lambda <= 0.0) ? 1.0 : ecfr_lambda;
}

/* EXT-01: the process-wide setter is gone. Keeping a static here would restore
   exactly the non-reentrancy this ticket removes, so the shim does nothing and
   the header marks it deprecated to say so at compile time. */
void cfr_storage_set_strategy_mode(int use_ecfr, double ecfr_lambda)
{
    (void)use_ecfr;
    (void)ecfr_lambda;
}

cfr_storage_t *cfr_storage_create(void)
{
    cfr_storage_t *s = (cfr_storage_t *)calloc(1, sizeof(cfr_storage_t));
    if (!s)
        return NULL;
    s->cap = 1 << 16; /* 65536 */
    /* calloc leaves ecfr_lambda at 0.0, which would turn exp(lambda * r) into
       a constant and silently produce a uniform policy. The neutral
       temperature is 1.0, so it is set rather than assumed. */
    s->use_ecfr = 0;
    s->ecfr_lambda = 1.0;
    s->num_threads = 1;
    s->tab = (entry_t *)calloc(s->cap, sizeof(entry_t));
    if (!s->tab)
    {
        free(s);
        return NULL;
    }
    return s;
}

void cfr_storage_set_memory_masks(cfr_storage_t *s, uint32_t avg_mask, uint32_t ev_mask)
{
    if (!s) return;
    s->keep_avg_strategy_mask = avg_mask;
    s->keep_ev_mask = ev_mask;
}

void cfr_storage_set_num_threads(cfr_storage_t *s, int num_threads)
{
    if (!s)
        return;
    s->num_threads = num_threads > 0 ? num_threads : 1;
}

void cfr_storage_destroy(cfr_storage_t *s)
{
    if (!s)
        return;
#ifdef PE_LEGACY_CFR_OPENMP
    if (s->cap <= (size_t)INT_MAX)
    {
        int i;
#pragma omp parallel for schedule(static) num_threads(s->num_threads)
        for (i = 0; i < (int)s->cap; ++i)
            if (s->tab[i].used)
            {
                free(s->tab[i].regret);
                free(s->tab[i].avg);
                free(s->tab[i].locked);
            }
    }
    else
#endif
    {
        for (size_t i = 0; i < s->cap; ++i)
            if (s->tab[i].used)
            {
                free(s->tab[i].regret);
                free(s->tab[i].avg);
                free(s->tab[i].locked);
            }
    }
    free(s->tab);
    free(s);
}

static entry_t *get_entry_at_street(cfr_storage_t *s, uint64_t key, int n, int street)
{
    if (!s || !s->tab || s->cap == 0 || n <= 0)
        return NULL;
    if (cfr_storage_ensure_capacity(s) != 0)
        return NULL;
    size_t m = s->cap - 1;
    size_t i = (size_t)(key * 11400714819323198485ull) & m;
    for (;;)
    {
        if (!s->tab[i].used)
        {
            double *regret = (double *)calloc((size_t)n, sizeof(double));
            double *avg = keep_for_street(s->keep_avg_strategy_mask, street)
                ? (double *)calloc((size_t)n, sizeof(double)) : NULL;
            if (!regret || (keep_for_street(s->keep_avg_strategy_mask, street) && !avg))
            {
                free(regret);
                free(avg);
                return NULL;
            }
            s->tab[i].used = 1;
            s->tab[i].key = key;
            s->tab[i].n = n;
            s->tab[i].regret = regret;
            s->tab[i].avg = avg;
            s->tab[i].ev_sum = 0.0;
            s->tab[i].ev_count = 0;
            s->used_count++;
            return &s->tab[i];
        }
        if (s->tab[i].key == key)
        {
            if (s->tab[i].n != n)
            { /* resize arrays if action count changed */
                /* Use temporaries so a failed realloc keeps the original
                   array alive (no leak, no NULL). */
                double *new_regret = (double *)realloc(s->tab[i].regret, (size_t)n * sizeof(double));
                double *new_avg = s->tab[i].avg
                    ? (double *)realloc(s->tab[i].avg, (size_t)n * sizeof(double))
                    : (keep_for_street(s->keep_avg_strategy_mask, street)
                       ? (double *)calloc((size_t)n, sizeof(double)) : NULL);
                double *new_locked = s->tab[i].locked
                    ? (double *)realloc(s->tab[i].locked, (size_t)n * sizeof(double))
                    : NULL;
                if (!new_regret ||
                    (keep_for_street(s->keep_avg_strategy_mask, street) && !new_avg) ||
                    (s->tab[i].locked && !new_locked))
                {
                    /* Commit whichever resizes succeeded; the others keep
                       their original blocks valid.  n is left unchanged, so
                       the entry stays consistent, and grow failures are
                       still recoverable on a later pass. */
                    if (new_regret)
                        s->tab[i].regret = new_regret;
                    if (new_avg)
                        s->tab[i].avg = new_avg;
                    if (new_locked)
                        s->tab[i].locked = new_locked;
                    return NULL;
                }
                s->tab[i].regret = new_regret;
                s->tab[i].avg = new_avg;
                s->tab[i].locked = new_locked;
                for (int k = s->tab[i].n; k < n; k++)
                {
                    s->tab[i].regret[k] = 0.0;
                    if (s->tab[i].avg) s->tab[i].avg[k] = 0.0;
                    if (s->tab[i].locked)
                        s->tab[i].locked[k] = 0.0;
                }
                s->tab[i].n = n;
            }
            return &s->tab[i];
        }
        i = (i + 1) & m;
    }
}

static entry_t *get_entry(cfr_storage_t *s, uint64_t key, int n)
{
    return get_entry_at_street(s, key, n, -1);
}

static entry_t *find_entry(cfr_storage_t *s, uint64_t key)
{
    if (!s || !s->tab || s->cap == 0)
        return NULL;
    size_t m = s->cap - 1;
    size_t i = (size_t)(key * 11400714819323198485ull) & m;
    for (;;)
    {
        entry_t *e = &s->tab[i];
        if (!e->used)
            return NULL;
        if (e->key == key)
            return e;
        i = (i + 1) & m;
    }
}

int cfr_storage_has_entry(cfr_storage_t *s, uint64_t key)
{
    return find_entry(s, key) ? 1 : 0;
}

int cfr_storage_action_count(cfr_storage_t *s, uint64_t key)
{
    entry_t *entry = find_entry(s, key);
    return entry ? entry->n : 0;
}

int cfr_storage_set_locked_strategy(cfr_storage_t *s, uint64_t infoset, const double *probs, int n)
{
    if (!s || !probs || n <= 0)
    {
        errno = EINVAL;
        return -1;
    }
    entry_t *e = get_entry(s, infoset, n);
    double *locked = (double *)calloc((size_t)n, sizeof(double));
    if (!locked)
        return -1;
    for (int i = 0; i < n; ++i)
        locked[i] = probs[i];
    free(e->locked);
    e->locked = locked;
    for (int i = 0; i < n; ++i)
    {
        e->regret[i] = 0.0;
        if (e->avg)
            e->avg[i] = probs[i];
    }
    /* Replacing a lock invalidates any previously recorded EV loss, which
     * belonged to the old target frequencies. */
    e->lock_ev_num = 0.0;
    e->lock_ev_den = 0.0;
    e->lock_br_num = 0.0;
    e->lock_ev_valid = 0;
    return 0;
}

int cfr_storage_get_locked_strategy(cfr_storage_t *s, uint64_t infoset, int action_count, const double **out_probs)
{
    if (out_probs)
        *out_probs = NULL;
    if (!s)
        return 0;
    entry_t *e = find_entry(s, infoset);
    if (!e || !e->locked || e->n != action_count)
        return 0;
    if (out_probs)
        *out_probs = e->locked;
    return 1;
}

void cfr_storage_begin_lock_ev_pass(cfr_storage_t *s)
{
    if (!s || !s->tab)
        return;
    for (size_t i = 0; i < s->cap; ++i)
    {
        entry_t *e = &s->tab[i];
        if (!e->used || !e->locked)
            continue;
        e->lock_ev_num = 0.0;
        e->lock_ev_den = 0.0;
        e->lock_br_num = 0.0;
        e->lock_ev_valid = 0;
    }
}

void cfr_storage_record_lock_ev_loss(cfr_storage_t *s, uint64_t infoset,
                                     double br_value, double forced_value, double reach_weight)
{
    if (!s)
        return;
    /* The infoset must already exist; look it up without forcing creation so
     * we only record loss for infosets that are actually locked. */
    entry_t *e = find_entry(s, infoset);
    if (!e || !e->locked)
        return;
    double w = reach_weight > 0.0 ? reach_weight : 0.0;
    e->lock_ev_den += w;
    e->lock_ev_num += w * (br_value - forced_value);
    e->lock_br_num += w * br_value;
    e->lock_ev_valid = 1;
}

int cfr_storage_get_lock_ev_loss(cfr_storage_t *s, uint64_t infoset,
                                 double *out_loss, double *out_br, double *out_forced)
{
    if (!s)
        return 0;
    entry_t *e = find_entry(s, infoset);
    if (!e || !e->lock_ev_valid || e->lock_ev_den <= 0.0)
        return 0;
    double loss = e->lock_ev_num / e->lock_ev_den;
    double br = e->lock_br_num / e->lock_ev_den;
    if (out_loss)
        *out_loss = loss;
    if (out_br)
        *out_br = br;
    if (out_forced)
        *out_forced = br - loss;
    return 1;
}

int cfr_storage_peek_avg_strategy(cfr_storage_t *s,
                                  uint64_t key,
                                  int action_count,
                                  double *probs)
{
    if (!s || !probs || action_count <= 0)
        return -1;
    entry_t *e = find_entry(s, key);
    if (!e || e->n <= 0)
        return -1;
    int n = e->n;
    if (action_count > n)
        action_count = n;
    double sum = 0.0;
    for (int i = 0; i < action_count; ++i)
        sum += e->avg[i];
    if (sum > 0.0)
    {
        for (int i = 0; i < action_count; ++i)
            probs[i] = e->avg[i] / sum;
    }
    else
    {
        for (int i = 0; i < action_count; ++i)
            probs[i] = 1.0 / action_count;
    }
    return 0;
}

void cfr_storage_get_strategy(cfr_storage_t *s, uint64_t infoset, int action_count, double *probs)
{
    entry_t *e = get_entry(s, infoset, action_count);
    if (!e)
    {
        for (int i = 0; i < action_count; i++)
            probs[i] = 1.0 / action_count;
        return;
    }
    if (!s->use_ecfr)
    {
        double sum_pos = cfr_sum_positive(e->regret, action_count);
        if (sum_pos > 0)
        {
            for (int i = 0; i < action_count; i++)
            {
                double r = e->regret[i];
                probs[i] = (r > 0) ? (r / sum_pos) : 0.0;
            }
            return;
        }
    }
    else
    {
        double max_pos = 0.0;
        for (int i = 0; i < action_count; i++)
        {
            double rp = e->regret[i];
            if (rp > 0.0)
            {
                if (rp > max_pos)
                    max_pos = rp;
            }
        }
        if (max_pos > 0.0)
        {
            double sum_w = 0.0;
            for (int i = 0; i < action_count; i++)
            {
                double rp = e->regret[i];
                if (rp > 0.0)
                {
                    double w = exp(s->ecfr_lambda * (rp - max_pos));
                    probs[i] = w;
                    sum_w += w;
                }
                else
                {
                    probs[i] = 0.0;
                }
            }
            if (sum_w > 0.0)
            {
                for (int i = 0; i < action_count; i++)
                    probs[i] /= sum_w;
                return;
            }
        }
    }

    for (int i = 0; i < action_count; i++)
    {
        probs[i] = 1.0 / action_count;
    }
}

void cfr_storage_get_strategy_at_street(cfr_storage_t *s, uint64_t key, int n, int street, double *probs)
{
    entry_t *e = get_entry_at_street(s, key, n, street);
    if (!e) { for (int i = 0; i < n; ++i) probs[i] = 1.0 / n; return; }
    if (e->locked) { for (int i = 0; i < n; ++i) probs[i] = e->locked[i]; return; }
    if (s->use_ecfr) {
        double max_pos = 0.0, sum_w = 0.0;
        for (int i = 0; i < n; ++i)
            if (e->regret[i] > max_pos) max_pos = e->regret[i];
        if (max_pos > 0.0) {
            for (int i = 0; i < n; ++i) {
                probs[i] = e->regret[i] > 0.0
                    ? exp(s->ecfr_lambda * (e->regret[i] - max_pos)) : 0.0;
                sum_w += probs[i];
            }
            if (sum_w > 0.0) {
                for (int i = 0; i < n; ++i) probs[i] /= sum_w;
                return;
            }
        }
    } else {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) if (e->regret[i] > 0.0) sum += e->regret[i];
        if (sum > 0.0) {
            for (int i = 0; i < n; ++i) probs[i] = e->regret[i] > 0.0 ? e->regret[i] / sum : 0.0;
            return;
        }
    }
    for (int i = 0; i < n; ++i) probs[i] = 1.0 / n;
}

void cfr_storage_get_avg_strategy(cfr_storage_t *s, uint64_t infoset, int action_count, double *probs)
{
    entry_t *e = get_entry(s, infoset, action_count);
    if (!e)
    {
        for (int i = 0; i < action_count; i++)
            probs[i] = 1.0 / action_count;
        return;
    }
    if (!e->avg) { cfr_storage_get_strategy_at_street(s, infoset, action_count, -1, probs); return; }
    double sum = 0.0;
    for (int i = 0; i < action_count; i++)
    {
        sum += e->avg[i];
    }
    if (sum > 0)
    {
        for (int i = 0; i < action_count; i++)
        {
            probs[i] = e->avg[i] / sum;
        }
    }
    else
    {
        // If no average strategy has been accumulated yet, use uniform distribution
        for (int i = 0; i < action_count; i++)
        {
            probs[i] = 1.0 / action_count;
        }
    }
}

void cfr_storage_get_avg_strategy_at_street(cfr_storage_t *s, uint64_t key, int n, int street, double *probs)
{
    if (s && !keep_for_street(s->keep_avg_strategy_mask, street)) {
        cfr_storage_get_strategy_at_street(s, key, n, street, probs);
        return;
    }
    entry_t *e = get_entry_at_street(s, key, n, street);
    if (!e || !e->avg) { cfr_storage_get_strategy_at_street(s, key, n, street, probs); return; }
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += e->avg[i];
    if (sum > 0.0) for (int i = 0; i < n; ++i) probs[i] = e->avg[i] / sum;
    else for (int i = 0; i < n; ++i) probs[i] = 1.0 / n;
}

void cfr_storage_update_regret(cfr_storage_t *s, uint64_t infoset, int action_count, const double *regret_delta, double discount)
{
    entry_t *e = get_entry(s, infoset, action_count);
    if (!e)
        return;
    for (int i = 0; i < action_count; i++)
    {
        e->regret[i] = e->regret[i] * discount + regret_delta[i];
    }
}

void cfr_storage_update_regret_at_street(cfr_storage_t *s, uint64_t key, int n, int street, const double *d, double discount)
{ entry_t *e = get_entry_at_street(s, key, n, street); if (!e) return; for (int i=0;i<n;++i) e->regret[i] = e->regret[i]*discount+d[i]; }

void cfr_storage_update_avg(cfr_storage_t *s, uint64_t infoset, int action_count, const double *strategy, double weight)
{
    entry_t *e = get_entry(s, infoset, action_count);
    if (!e)
        return;
    if (!e->avg) return;
    for (int i = 0; i < action_count; i++)
    {
        e->avg[i] += strategy[i] * weight;
    }
}

void cfr_storage_update_avg_at_street(cfr_storage_t *s, uint64_t key, int n, int street, const double *strategy, double weight)
{ if (!s || !keep_for_street(s->keep_avg_strategy_mask, street)) return; entry_t *e = get_entry_at_street(s, key, n, street); if (!e || !e->avg) return; for (int i=0;i<n;++i) e->avg[i] += strategy[i]*weight; }

/* Regret-matched descent strategy ignoring any lock (FEAT-11). Used by the
 * periodic relock engine on non-relock iterations so a locked infoset is allowed
 * to drift under normal CFR while its export is re-asserted to target only on
 * relock iterations. */
void cfr_storage_get_regret_strategy_at_street(cfr_storage_t *s, uint64_t key, int n, int street, double *probs)
{
    entry_t *e = get_entry_at_street(s, key, n, street);
    if (!e) { for (int i = 0; i < n; ++i) probs[i] = 1.0 / n; return; }
    if (s->use_ecfr) {
        double max_pos = 0.0, sum_w = 0.0;
        for (int i = 0; i < n; ++i)
            if (e->regret[i] > max_pos) max_pos = e->regret[i];
        if (max_pos > 0.0) {
            for (int i = 0; i < n; ++i) {
                probs[i] = e->regret[i] > 0.0 ? exp(s->ecfr_lambda * (e->regret[i] - max_pos)) : 0.0;
                sum_w += probs[i];
            }
            if (sum_w > 0.0) { for (int i = 0; i < n; ++i) probs[i] /= sum_w; return; }
        }
    } else {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) if (e->regret[i] > 0.0) sum += e->regret[i];
        if (sum > 0.0) {
            for (int i = 0; i < n; ++i) probs[i] = e->regret[i] > 0.0 ? e->regret[i] / sum : 0.0;
            return;
        }
    }
    for (int i = 0; i < n; ++i) probs[i] = 1.0 / n;
}

/* Overwrite the average strategy of an infoset with the given target
 * frequencies (FEAT-11). Unlike cfr_storage_update_avg_at_street this replaces
 * the accumulated average rather than adding to it, which is how the periodic
 * relock engine re-asserts a locked node's target frequencies instantly. */
void cfr_storage_overwrite_avg_at_street(cfr_storage_t *s, uint64_t key, int n, int street, const double *target)
{
    if (!s || !keep_for_street(s->keep_avg_strategy_mask, street))
        return;
    entry_t *e = get_entry_at_street(s, key, n, street);
    if (!e)
        return;
    if (!e->avg)
    {
        e->avg = (double *)calloc((size_t)n, sizeof(double));
        if (!e->avg)
            return;
    }
    for (int i = 0; i < n; ++i)
        e->avg[i] = target[i];
}

/* Dump average strategies to CSV file (key, n, avg0..avgN-1) */
void cfr_storage_dump_avg(cfr_storage_t *s, FILE *f)
{
    if (!s || !f || !s->tab)
        return;
    fprintf(f, "infoset,n");
    for (int i = 0; i < 8; i++)
        fprintf(f, ",avg%d", i);
    fprintf(f, "\n");
    for (size_t i = 0; i < s->cap; i++)
        if (s->tab[i].used)
        {
            entry_t *e = &s->tab[i];
            double *avg = e->avg;
            double *fallback = NULL;
            if (!avg) {
                fallback = (double *)malloc(sizeof(double) * (size_t)e->n);
                if (!fallback) continue;
                cfr_storage_get_strategy_at_street(s, e->key, e->n, -1, fallback);
                avg = fallback;
            }
            fprintf(f, "%llu,%d", (unsigned long long)e->key, e->n);
            double sum = 0.0;
            for (int k = 0; k < e->n; k++)
                sum += avg[k];
            for (int k = 0; k < e->n && k < 8; k++)
            {
                double p = (sum > 0) ? (avg[k] / sum) : (1.0 / e->n);
                fprintf(f, ",%.6f", p);
            }
            fprintf(f, "\n");
            free(fallback);
        }
}

size_t cfr_storage_count_infosets(cfr_storage_t *s)
{
    if (!s || !s->tab)
        return 0;
    size_t cnt = 0;
    for (size_t i = 0; i < s->cap; i++)
        if (s->tab[i].used)
            cnt++;
    return cnt;
}

/*
 * Scale every accumulated regret by `factor` (EXT-07).
 *
 * Discounted CFR discounts the cumulative regret once per iteration, applied
 * to what was there before this iteration's deltas land on it:
 *
 *     R_t = R_(t-1) * d(t) + r_t
 *
 * The v2 solver instead passed `d` to every per-node regret update, and a
 * poker infoset is reached many times in one iteration, so it accumulated
 * d^N. This is the operation that lets the discount happen exactly once.
 */
/*
 * Writable spans of an entry's arrays (STO-04).
 *
 * The storage port hands callers a span rather than a copy, and the v2 storage
 * had no way to give one out. These create the entry if it is absent, exactly
 * like the update functions do.
 *
 * The pointer is invalidated by anything that grows the table or the entry, so
 * it is good until the next call that touches this storage — the same contract
 * the port already states.
 */
double *cfr_storage_regret_span(cfr_storage_t *s, uint64_t key, int n_actions)
{
    entry_t *e = get_entry(s, key, n_actions);
    return e ? e->regret : NULL;
}

double *cfr_storage_avg_span(cfr_storage_t *s, uint64_t key, int n_actions)
{
    entry_t *e = get_entry(s, key, n_actions);
    /* avg is absent when selective memory dropped this street. */
    return e ? e->avg : NULL;
}

void cfr_storage_scale_regrets(cfr_storage_t *s, double factor)
{
    if (!s || !s->tab)
        return;
    /* Nothing to do, and worth skipping: a full sweep of the table costs the
       same whether or not the factor is neutral. */
    if (factor == 1.0)
        return;

#ifdef PE_LEGACY_CFR_OPENMP
    if (s->cap <= (size_t)INT_MAX)
    {
        int i;
#pragma omp parallel for schedule(static) num_threads(s->num_threads)
        for (i = 0; i < (int)s->cap; ++i)
        {
            entry_t *e = &s->tab[i];
            if (!e->used || !e->regret)
                continue;
            for (int a = 0; a < e->n; ++a)
                e->regret[a] *= factor;
        }
    }
    else
#endif
    {
        for (size_t i = 0; i < s->cap; ++i)
        {
            entry_t *e = &s->tab[i];
            if (!e->used || !e->regret)
                continue;
            for (int a = 0; a < e->n; ++a)
                e->regret[a] *= factor;
        }
    }
}

typedef struct {
    cfr_storage_t *destination;
    double regret_scale;
    double average_scale;
    int failed;
} cfr_storage_merge_context_t;

static void cfr_storage_merge_callback(uint64_t key, int n_actions,
                                       const double *regret,
                                       const double *average,
                                       void *user_data)
{
    cfr_storage_merge_context_t *ctx =
        (cfr_storage_merge_context_t *)user_data;
    double *scaled_regret;
    double *scaled_average;
    int i;

    if (!ctx || ctx->failed || !regret || !average || n_actions <= 0)
        return;
    scaled_regret = (double *)malloc((size_t)n_actions * sizeof(double));
    scaled_average = (double *)malloc((size_t)n_actions * sizeof(double));
    if (!scaled_regret || !scaled_average)
    {
        free(scaled_regret);
        free(scaled_average);
        ctx->failed = 1;
        return;
    }
    for (i = 0; i < n_actions; ++i)
    {
        scaled_regret[i] = regret[i] * ctx->regret_scale;
        scaled_average[i] = average[i] * ctx->average_scale;
    }
    cfr_storage_update_regret(ctx->destination, key, n_actions,
                              scaled_regret, 1.0);
    cfr_storage_update_avg(ctx->destination, key, n_actions,
                           scaled_average, 1.0);
    free(scaled_regret);
    free(scaled_average);
}

int cfr_storage_merge_scaled(cfr_storage_t *destination,
                             const cfr_storage_t *source,
                             double regret_scale,
                             double average_scale)
{
    cfr_storage_merge_context_t ctx;
    if (!destination || !source || !source->tab ||
        !pe_finite_double(regret_scale) || !pe_finite_double(average_scale))
    {
        errno = EINVAL;
        return -1;
    }
    ctx.destination = destination;
    ctx.regret_scale = regret_scale;
    ctx.average_scale = average_scale;
    ctx.failed = 0;
    cfr_storage_iterate(source, cfr_storage_merge_callback, &ctx);
    return ctx.failed ? -1 : 0;
}

/* A storage delta is deliberately a value snapshot rather than a dump of the
 * private hash table.  Workers can therefore be built against a different
 * table size and the coordinator still has one stable merge format. */
#define CFR_DELTA_MAGIC "CFRDELTA"
#define CFR_DELTA_VERSION 1u
#define CFR_DELTA_HEADER_SIZE 16u
#define CFR_DELTA_RECORD_HEADER_SIZE 16u

typedef struct {
    uint64_t key;
    int n;
    const double *regret;
    const double *average;
} cfr_delta_entry_t;

typedef struct {
    cfr_delta_entry_t *entries;
    size_t count;
    size_t capacity;
    int failed;
} cfr_delta_collect_t;

static void cfr_delta_put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24u);
    out[1] = (uint8_t)(value >> 16u);
    out[2] = (uint8_t)(value >> 8u);
    out[3] = (uint8_t)value;
}

static void cfr_delta_put_u64(uint8_t *out, uint64_t value)
{
    size_t i;
    for (i = 0u; i < 8u; ++i)
        out[i] = (uint8_t)(value >> (56u - 8u * i));
}

static uint32_t cfr_delta_get_u32(const uint8_t *in)
{
    return ((uint32_t)in[0] << 24u) | ((uint32_t)in[1] << 16u) |
           ((uint32_t)in[2] << 8u) | (uint32_t)in[3];
}

static uint64_t cfr_delta_get_u64(const uint8_t *in)
{
    size_t i;
    uint64_t value = 0u;
    for (i = 0u; i < 8u; ++i)
        value = (value << 8u) | (uint64_t)in[i];
    return value;
}

static void cfr_delta_put_double(uint8_t *out, double value)
{
    union {
        double value;
        uint64_t bits;
    } converted;
    converted.value = value;
    cfr_delta_put_u64(out, converted.bits);
}

static double cfr_delta_get_double(const uint8_t *in)
{
    union {
        double value;
        uint64_t bits;
    } converted;
    converted.bits = cfr_delta_get_u64(in);
    return converted.value;
}

static int cfr_delta_entry_before(const void *left_ptr, const void *right_ptr)
{
    const cfr_delta_entry_t *left = (const cfr_delta_entry_t *)left_ptr;
    const cfr_delta_entry_t *right = (const cfr_delta_entry_t *)right_ptr;
    if (left->key != right->key)
        return left->key < right->key ? -1 : 1;
    if (left->n != right->n)
        return left->n < right->n ? -1 : 1;
    return 0;
}

static void cfr_delta_collect_callback(uint64_t key, int n_actions,
                                       const double *regret,
                                       const double *average,
                                       void *user_data)
{
    cfr_delta_collect_t *collect = (cfr_delta_collect_t *)user_data;
    if (!collect || collect->failed || !regret || !average || n_actions <= 0)
        return;
    if (collect->count == collect->capacity) {
        size_t next = collect->capacity ? collect->capacity * 2u : 64u;
        cfr_delta_entry_t *grown;
        if (next < collect->capacity ||
            next > SIZE_MAX / sizeof(*grown)) {
            collect->failed = 1;
            return;
        }
        grown = (cfr_delta_entry_t *)realloc(collect->entries,
                                             next * sizeof(*grown));
        if (!grown) {
            collect->failed = 1;
            return;
        }
        collect->entries = grown;
        collect->capacity = next;
    }
    collect->entries[collect->count].key = key;
    collect->entries[collect->count].n = n_actions;
    collect->entries[collect->count].regret = regret;
    collect->entries[collect->count].average = average;
    ++collect->count;
}

int cfr_storage_export_delta(const cfr_storage_t *storage,
                             uint8_t **out_blob,
                             size_t *out_size)
{
    cfr_delta_collect_t collect = {0};
    size_t total = CFR_DELTA_HEADER_SIZE;
    size_t i;
    uint8_t *blob;
    uint8_t *cursor;

    if (!storage || !out_blob || !out_size) {
        errno = EINVAL;
        return -1;
    }
    *out_blob = NULL;
    *out_size = 0u;
    cfr_storage_iterate(storage, cfr_delta_collect_callback, &collect);
    if (collect.failed) {
        free(collect.entries);
        return -1;
    }
    if (collect.count > UINT32_MAX) {
        free(collect.entries);
        return -1;
    }
    qsort(collect.entries, collect.count, sizeof(*collect.entries),
          cfr_delta_entry_before);
    if (collect.count > (SIZE_MAX - total) / 16u) {
        free(collect.entries);
        return -1;
    }
    for (i = 0u; i < collect.count; ++i) {
        size_t n = (size_t)collect.entries[i].n;
        size_t values;
        if (n > (SIZE_MAX - CFR_DELTA_RECORD_HEADER_SIZE) /
                 (2u * sizeof(double))) {
            free(collect.entries);
            return -1;
        }
        values = CFR_DELTA_RECORD_HEADER_SIZE + 2u * n * sizeof(double);
        if (total > SIZE_MAX - values) {
            free(collect.entries);
            return -1;
        }
        total += values;
    }
    blob = (uint8_t *)malloc(total);
    if (!blob) {
        free(collect.entries);
        return -1;
    }
    for (i = 0u; i < 8u; ++i)
        blob[i] = CFR_DELTA_MAGIC[i];
    cfr_delta_put_u32(blob + 8u, CFR_DELTA_VERSION);
    cfr_delta_put_u32(blob + 12u, (uint32_t)collect.count);
    cursor = blob + CFR_DELTA_HEADER_SIZE;
    for (i = 0u; i < collect.count; ++i) {
        const cfr_delta_entry_t *entry = &collect.entries[i];
        size_t action;
        cfr_delta_put_u64(cursor, entry->key);
        cfr_delta_put_u32(cursor + 8u, (uint32_t)entry->n);
        cfr_delta_put_u32(cursor + 12u, 0u);
        cursor += CFR_DELTA_RECORD_HEADER_SIZE;
        for (action = 0u; action < (size_t)entry->n; ++action) {
            cfr_delta_put_double(cursor, entry->regret[action]);
            cursor += sizeof(double);
        }
        for (action = 0u; action < (size_t)entry->n; ++action) {
            cfr_delta_put_double(cursor, entry->average[action]);
            cursor += sizeof(double);
        }
    }
    free(collect.entries);
    *out_blob = blob;
    *out_size = total;
    return 0;
}

int cfr_storage_apply_delta(cfr_storage_t *storage,
                            const uint8_t *blob,
                            size_t blob_size,
                            double scale)
{
    size_t cursor = CFR_DELTA_HEADER_SIZE;
    uint32_t count;
    uint32_t version;
    uint32_t index;
    double *regret = NULL;
    double *average = NULL;

    if (!storage || !blob || blob_size < CFR_DELTA_HEADER_SIZE ||
        !pe_finite_double(scale) || memcmp(blob, CFR_DELTA_MAGIC, 8u) != 0)
        return -1;
    version = cfr_delta_get_u32(blob + 8u);
    count = cfr_delta_get_u32(blob + 12u);
    if (version != CFR_DELTA_VERSION)
        return -1;
    for (index = 0u; index < count; ++index) {
        uint64_t key;
        uint32_t n32;
        size_t n;
        size_t bytes;
        size_t action;
        if (blob_size - cursor < CFR_DELTA_RECORD_HEADER_SIZE)
            goto fail;
        key = cfr_delta_get_u64(blob + cursor);
        n32 = cfr_delta_get_u32(blob + cursor + 8u);
        if (cfr_delta_get_u32(blob + cursor + 12u) != 0u || n32 == 0u)
            goto fail;
        n = (size_t)n32;
        if ((uint64_t)n != (uint64_t)n32)
            goto fail;
        if (n > (SIZE_MAX - CFR_DELTA_RECORD_HEADER_SIZE) /
                 (2u * sizeof(double)))
            goto fail;
        bytes = CFR_DELTA_RECORD_HEADER_SIZE + 2u * n * sizeof(double);
        if (bytes > blob_size - cursor)
            goto fail;
        regret = (double *)malloc(n * sizeof(double));
        average = (double *)malloc(n * sizeof(double));
        if (!regret || !average)
            goto fail;
        cursor += CFR_DELTA_RECORD_HEADER_SIZE;
        for (action = 0u; action < n; ++action) {
            regret[action] = cfr_delta_get_double(blob + cursor);
            cursor += sizeof(double);
            if (!pe_finite_double(regret[action]))
                goto fail;
        }
        for (action = 0u; action < n; ++action) {
            average[action] = cfr_delta_get_double(blob + cursor);
            cursor += sizeof(double);
            if (!pe_finite_double(average[action]))
                goto fail;
        }
        for (action = 0u; action < n; ++action) {
            regret[action] *= scale;
            average[action] *= scale;
        }
        cfr_storage_update_regret(storage, key, (int)n, regret, 1.0);
        cfr_storage_update_avg(storage, key, (int)n, average, 1.0);
        free(regret);
        free(average);
        regret = NULL;
        average = NULL;
    }
    if (cursor != blob_size)
        return -1;
    return 0;

fail:
    free(regret);
    free(average);
    return -1;
}

static void cfr_storage_strategy_from_entry(const cfr_storage_t *s,
                                             const entry_t *e,
                                             double *probs)
{
    int i;
    int n;
    if (!s || !e || !probs || e->n <= 0)
        return;
    n = e->n;
    if (e->locked)
    {
        for (i = 0; i < n; ++i)
            probs[i] = e->locked[i];
        return;
    }
    if (s->use_ecfr)
    {
        double max_pos = 0.0;
        double sum = 0.0;
        for (i = 0; i < n; ++i)
            if (e->regret[i] > max_pos)
                max_pos = e->regret[i];
        if (max_pos > 0.0)
        {
            for (i = 0; i < n; ++i)
            {
                probs[i] = e->regret[i] > 0.0
                    ? exp(s->ecfr_lambda * (e->regret[i] - max_pos)) : 0.0;
                sum += probs[i];
            }
            if (sum > 0.0)
            {
                for (i = 0; i < n; ++i)
                    probs[i] /= sum;
                return;
            }
        }
    }
    else
    {
        double sum = 0.0;
        for (i = 0; i < n; ++i)
            if (e->regret[i] > 0.0)
                sum += e->regret[i];
        if (sum > 0.0)
        {
            for (i = 0; i < n; ++i)
                probs[i] = e->regret[i] > 0.0 ? e->regret[i] / sum : 0.0;
            return;
        }
    }
    for (i = 0; i < n; ++i)
        probs[i] = 1.0 / n;
}

void cfr_storage_iterate(const cfr_storage_t *s,
                         cfr_iterate_callback fn,
                         void *user)
{
    if (!s || !fn || !s->tab)
        return;
    for (size_t i = 0; i < s->cap; i++)
        if (s->tab[i].used)
        {
            const entry_t *e = &s->tab[i];
            const double *avg = e->avg;
            double *fallback = NULL;
            if (!avg) {
                fallback = (double *)malloc(sizeof(double) * (size_t)e->n);
                if (!fallback) continue;
                cfr_storage_strategy_from_entry(s, e, fallback);
                avg = fallback;
            }
            fn(e->key, e->n, e->regret, avg, user);
            free(fallback);
        }
}

void cfr_storage_accumulate_ev(cfr_storage_t *s, uint64_t infoset, double node_ev)
{
    if (!s)
        return;
    if (s->cap == 0 || !s->tab)
        return;
    if (cfr_storage_ensure_capacity(s) != 0)
        return;
    /* We don't know n here; use get_entry with n=1 to find/create entry, then ignore n change unless later updated */
    size_t m = s->cap - 1;
    size_t i = (size_t)(infoset * 11400714819323198485ull) & m;
    for (;;)
    {
        entry_t *e = &s->tab[i];
        if (!e->used)
        {
            /* Create minimal entry */
            double *regret = (double *)calloc(1, sizeof(double));
            double *avg = s->keep_ev_mask == 0
                ? (double *)calloc(1, sizeof(double)) : NULL;
            if (!regret || (s->keep_ev_mask == 0 && !avg))
            {
                free(regret);
                free(avg);
                return;
            }
            e->used = 1;
            e->key = infoset;
            e->n = 1;
            e->regret = regret;
            e->avg = avg;
            e->ev_sum = 0.0;
            e->ev_sq_sum = 0.0;
            e->ev_count = 0;
            s->used_count++;
            break;
        }
        if (e->key == infoset)
            break;
        i = (i + 1) & m;
    }
    s->tab[i].ev_sum += node_ev;
    s->tab[i].ev_sq_sum += node_ev * node_ev;
    s->tab[i].ev_count += 1;
}

void cfr_storage_accumulate_ev_at_street(cfr_storage_t *s, uint64_t key, int street, double node_ev)
{
    if (!s || !keep_for_street(s->keep_ev_mask, street)) return;
    entry_t *e = find_entry(s, key);
    if (!e)
        e = get_entry_at_street(s, key, 1, street);
    if (!e) return;
    e->ev_sum += node_ev;
    e->ev_sq_sum += node_ev * node_ev;
    e->ev_count += 1;
}

void cfr_storage_iterate_stats(cfr_storage_t *s, cfr_storage_iter_stats_fn fn, void *user)
{
    if (!s || !fn)
        return;
    for (size_t i = 0; i < s->cap; i++)
        if (s->tab[i].used)
        {
            entry_t *e = &s->tab[i];
            double *avg = e->avg;
            double *fallback = NULL;
            if (!avg) {
                fallback = (double *)malloc(sizeof(double) * (size_t)e->n);
                if (!fallback) continue;
                cfr_storage_get_strategy_at_street(s, e->key, e->n, -1, fallback);
                avg = fallback;
            }
            fn(e->key,
               e->n,
               e->regret,
               avg,
               e->ev_sum,
               e->ev_sq_sum,
               e->ev_count,
               user);
            free(fallback);
        }
}

int cfr_storage_get_ev_stats(cfr_storage_t *s,
                             uint64_t key,
                             double *out_mean,
                             double *out_stddev,
                             uint64_t *out_count)
{
    entry_t *e = find_entry(s, key);
    if (!e)
        return -1;
    if (out_count)
        *out_count = e->ev_count;
    double mean = 0.0;
    double stddev = 0.0;
    if (e->ev_count > 0)
    {
        mean = e->ev_sum / (double)e->ev_count;
        double variance = (e->ev_sq_sum / (double)e->ev_count) - mean * mean;
        if (variance < 0.0)
            variance = 0.0;
        stddev = sqrt(variance);
    }
    if (out_mean)
        *out_mean = mean;
    if (out_stddev)
        *out_stddev = stddev;
    return 0;
}

int cfr_storage_save_checkpoint(cfr_storage_t *s, const char *path, uint64_t iteration)
{
    if (!s || !path || !*path)
    {
        errno = EINVAL;
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    cfr_checkpoint_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "CFRCHKPT", 8);
    hdr.version = 3;
    hdr.cap = s->cap;
    hdr.entry_count = cfr_storage_entry_count(s);
    hdr.iteration = iteration;

    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1)
    {
        int err = errno;
        fclose(f);
        errno = err;
        return -1;
    }

    for (size_t i = 0; i < s->cap; ++i)
    {
        entry_t *e = &s->tab[i];
        if (!e->used)
            continue;
        uint32_t n = (uint32_t)e->n;
        double *locked_out = e->locked;
        double *avg_out = e->avg;
        int free_avg_out = 0;
        if (!avg_out)
        {
            avg_out = (double *)calloc((size_t)e->n, sizeof(double));
            if (!avg_out) { fclose(f); return -1; }
            free_avg_out = 1;
        }
        if (!locked_out)
        {
            locked_out = (double *)calloc((size_t)e->n, sizeof(double));
            if (!locked_out)
            {
                int err = errno;
                fclose(f);
                errno = err;
                return -1;
            }
        }
        int lock_ok = fwrite(&e->key, sizeof(uint64_t), 1, f) == 1 &&
            fwrite(&n, sizeof(uint32_t), 1, f) == 1 &&
            fwrite(&e->ev_sum, sizeof(double), 1, f) == 1 &&
            fwrite(&e->ev_sq_sum, sizeof(double), 1, f) == 1 &&
            fwrite(&e->ev_count, sizeof(uint64_t), 1, f) == 1 &&
            fwrite(e->regret, sizeof(double), e->n, f) == (size_t)e->n &&
            fwrite(avg_out, sizeof(double), e->n, f) == (size_t)e->n &&
            fwrite(locked_out, sizeof(double), e->n, f) == (size_t)e->n;
        if (!e->locked)
            free(locked_out);
        if (free_avg_out)
            free(avg_out);
        if (!lock_ok)
        {
            int err = errno;
            fclose(f);
            errno = err;
            return -1;
        }
    }

    if (fclose(f) != 0)
        return -1;
    return 0;
}

int cfr_storage_load_checkpoint(cfr_storage_t *s, const char *path, uint64_t *out_iteration)
{
    if (!s || !path || !*path)
    {
        errno = EINVAL;
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    cfr_checkpoint_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1)
    {
        int err = errno;
        fclose(f);
        errno = err;
        return -1;
    }
    if (memcmp(hdr.magic, "CFRCHKPT", 8) != 0 || (hdr.version != 1 && hdr.version != 2 && hdr.version != 3))
    {
        fclose(f);
        errno = EINVAL;
        return -1;
    }
    /* Validate cap before any allocation: must be a power of two (as
       written by cfr_storage_save_checkpoint), >= 1, and small enough
       that next_pow2 of it cannot overflow (BUG-04). */
    if (hdr.cap == 0 || (hdr.cap & (hdr.cap - 1)) != 0 ||
        hdr.cap > (SIZE_MAX >> 1) + 1)
    {
        fclose(f);
        errno = EINVAL;
        return -1;
    }
    /* Validate the header before doing any work (BUG-05): treated as
       untrusted input.  cap must be a sensible power of two (saves always
       write a power of two), and entry_count must fit inside it or the
       table fills up and get_entry never finds a free slot. */
    if (hdr.cap == 0 || hdr.cap > CFR_MAX_CHECKPOINT_CAP ||
        (hdr.cap & (hdr.cap - 1)) != 0 ||
        hdr.entry_count > hdr.cap)
    {
        fclose(f);
        errno = EINVAL;
        return -1;
    }

    if (cfr_storage_resize(s, hdr.cap) != 0)
    {
        int err = errno;
        fclose(f);
        errno = err;
        return -1;
    }
    if (s->cap != hdr.cap)
    {
        fclose(f);
        errno = EINVAL;
        return -1;
    }

    for (size_t idx = 0; idx < hdr.entry_count; ++idx)
    {
        uint64_t key = 0;
        uint32_t n = 0;
        double ev_sum = 0.0;
        double ev_sq_sum = 0.0;
        uint64_t ev_count = 0;
        if (fread(&key, sizeof(uint64_t), 1, f) != 1 ||
            fread(&n, sizeof(uint32_t), 1, f) != 1 ||
            fread(&ev_sum, sizeof(double), 1, f) != 1)
        {
            int err = errno;
            fclose(f);
            errno = err;
            return -1;
        }
        if (hdr.version >= 2)
        {
            if (fread(&ev_sq_sum, sizeof(double), 1, f) != 1)
            {
                int err = errno;
                fclose(f);
                errno = err;
                return -1;
            }
        }
        if (fread(&ev_count, sizeof(uint64_t), 1, f) != 1)
        {
            int err = errno;
            fclose(f);
            errno = err;
            return -1;
        }
        if (n == 0 || n > 4096)
        {
            fclose(f);
            errno = EINVAL;
            return -1;
        }
        entry_t *e = get_entry(s, key, (int)n);
        if (!e || !e->regret)
        {
            int err = errno;
            fclose(f);
            errno = err;
            return -1;
        }
        e->ev_sum = ev_sum;
        e->ev_sq_sum = ev_sq_sum;
        e->ev_count = ev_count;
        if (fread(e->regret, sizeof(double), e->n, f) != (size_t)e->n)
        {
            int err = errno;
            fclose(f);
            errno = err;
            return -1;
        }
        if (e->avg) {
            if (fread(e->avg, sizeof(double), e->n, f) != (size_t)e->n) {
                int err = errno; fclose(f); errno = err; return -1;
            }
        } else {
            double *discard = (double *)malloc(sizeof(double) * e->n);
            if (!discard || fread(discard, sizeof(double), e->n, f) != (size_t)e->n) {
                free(discard); int err = errno; fclose(f); errno = err; return -1;
            }
            free(discard);
        }
        if (hdr.version >= 3)
        {
            double *locked_in = (double *)calloc((size_t)e->n, sizeof(double));
            if (!locked_in)
            {
                int err = errno;
                fclose(f);
                errno = err;
                return -1;
            }
            if (fread(locked_in, sizeof(double), e->n, f) != (size_t)e->n)
            {
                int err = errno;
                free(locked_in);
                fclose(f);
                errno = err;
                return -1;
            }
            int any = 0;
            for (int k = 0; k < e->n; ++k)
                if (locked_in[k] != 0.0)
                    any = 1;
            if (any)
            {
                free(e->locked);
                e->locked = locked_in;
            }
            else
            {
                free(locked_in);
            }
        }
    }

    if (out_iteration)
        *out_iteration = hdr.iteration;

    if (fclose(f) != 0)
        return -1;
    return 0;
}
