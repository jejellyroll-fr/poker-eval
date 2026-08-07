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

#if defined(_MSC_VER)
#define CFR_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define CFR_THREAD_LOCAL __thread
#else
#define CFR_THREAD_LOCAL _Thread_local
#endif

static CFR_THREAD_LOCAL int g_use_ecfr = 0;
static CFR_THREAD_LOCAL double g_ecfr_lambda = 1.0;

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t reserved;
    uint64_t cap;
    uint64_t entry_count;
    uint64_t iteration;
} cfr_checkpoint_header_t;

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
            s->tab[i].regret = NULL;
            s->tab[i].avg = NULL;
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
    if (s->tab && s->cap == new_cap)
    {
        cfr_storage_free_entries(s);
        memset(s->tab, 0, s->cap * sizeof(entry_t));
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
    return 0;
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
    size_t p = 1;
    while (p < x)
        p <<= 1;
    return p;
}

void cfr_storage_set_strategy_mode(int use_ecfr, double ecfr_lambda)
{
    g_use_ecfr = use_ecfr ? 1 : 0;
    if (ecfr_lambda <= 0.0)
        g_ecfr_lambda = 1.0;
    else
        g_ecfr_lambda = ecfr_lambda;
}

cfr_storage_t *cfr_storage_create(void)
{
    cfr_storage_t *s = (cfr_storage_t *)calloc(1, sizeof(cfr_storage_t));
    if (!s)
        return NULL;
    s->cap = 1 << 16; /* 65536 */
    s->tab = (entry_t *)calloc(s->cap, sizeof(entry_t));
    if (!s->tab)
    {
        free(s);
        return NULL;
    }
    return s;
}

void cfr_storage_destroy(cfr_storage_t *s)
{
    if (!s)
        return;
    for (size_t i = 0; i < s->cap; i++)
        if (s->tab[i].used)
        {
            free(s->tab[i].regret);
            free(s->tab[i].avg);
        }
    free(s->tab);
    free(s);
}

static entry_t *get_entry(cfr_storage_t *s, uint64_t key, int n)
{
    size_t m = s->cap - 1;
    size_t i = (size_t)(key * 11400714819323198485ull) & m;
    for (;;)
    {
        if (!s->tab[i].used)
        {
            s->tab[i].used = 1;
            s->tab[i].key = key;
            s->tab[i].n = n;
            s->tab[i].regret = (double *)calloc(n, sizeof(double));
            s->tab[i].avg = (double *)calloc(n, sizeof(double));
            s->tab[i].ev_sum = 0.0;
            s->tab[i].ev_count = 0;
            return &s->tab[i];
        }
        if (s->tab[i].key == key)
        {
            if (s->tab[i].n != n)
            { /* resize arrays if action count changed */
                s->tab[i].regret = (double *)realloc(s->tab[i].regret, n * sizeof(double));
                s->tab[i].avg = (double *)realloc(s->tab[i].avg, n * sizeof(double));
                for (int k = s->tab[i].n; k < n; k++)
                {
                    s->tab[i].regret[k] = 0.0;
                    s->tab[i].avg[k] = 0.0;
                }
                s->tab[i].n = n;
            }
            return &s->tab[i];
        }
        i = (i + 1) & m;
    }
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
    if (!g_use_ecfr)
    {
        double sum_pos = 0.0;
        for (int i = 0; i < action_count; i++)
        {
            double r = e->regret[i];
            if (r > 0)
                sum_pos += r;
        }
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
                    double w = exp(g_ecfr_lambda * (rp - max_pos));
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

void cfr_storage_get_avg_strategy(cfr_storage_t *s, uint64_t infoset, int action_count, double *probs)
{
    entry_t *e = get_entry(s, infoset, action_count);
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

void cfr_storage_update_regret(cfr_storage_t *s, uint64_t infoset, int action_count, const double *regret_delta, double discount)
{
    entry_t *e = get_entry(s, infoset, action_count);
    for (int i = 0; i < action_count; i++)
    {
        e->regret[i] = e->regret[i] * discount + regret_delta[i];
    }
}

void cfr_storage_update_avg(cfr_storage_t *s, uint64_t infoset, int action_count, const double *strategy, double weight)
{
    entry_t *e = get_entry(s, infoset, action_count);
    for (int i = 0; i < action_count; i++)
    {
        e->avg[i] += strategy[i] * weight;
    }
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
            fprintf(f, "%llu,%d", (unsigned long long)e->key, e->n);
            double sum = 0.0;
            for (int k = 0; k < e->n; k++)
                sum += e->avg[k];
            for (int k = 0; k < e->n && k < 8; k++)
            {
                double p = (sum > 0) ? (e->avg[k] / sum) : (1.0 / e->n);
                fprintf(f, ",%.6f", p);
            }
            fprintf(f, "\n");
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

void cfr_storage_iterate(cfr_storage_t *s, cfr_iterate_callback fn, void *user)
{
    if (!s || !fn || !s->tab)
        return;
    for (size_t i = 0; i < s->cap; i++)
        if (s->tab[i].used)
        {
            entry_t *e = &s->tab[i];
            fn(e->key, e->n, e->regret, e->avg, user);
        }
}

void cfr_storage_accumulate_ev(cfr_storage_t *s, uint64_t infoset, double node_ev)
{
    if (!s)
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
            e->used = 1;
            e->key = infoset;
            e->n = 1;
            e->regret = (double *)calloc(1, sizeof(double));
            e->avg = (double *)calloc(1, sizeof(double));
            e->ev_sum = 0.0;
            e->ev_sq_sum = 0.0;
            e->ev_count = 0;
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

void cfr_storage_iterate_stats(cfr_storage_t *s, cfr_storage_iter_stats_fn fn, void *user)
{
    if (!s || !fn)
        return;
    for (size_t i = 0; i < s->cap; i++)
        if (s->tab[i].used)
        {
            entry_t *e = &s->tab[i];
            fn(e->key,
               e->n,
               e->regret,
               e->avg,
               e->ev_sum,
               e->ev_sq_sum,
               e->ev_count,
               user);
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
    hdr.version = 2;
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
        if (fwrite(&e->key, sizeof(uint64_t), 1, f) != 1 ||
            fwrite(&n, sizeof(uint32_t), 1, f) != 1 ||
            fwrite(&e->ev_sum, sizeof(double), 1, f) != 1 ||
            fwrite(&e->ev_sq_sum, sizeof(double), 1, f) != 1 ||
            fwrite(&e->ev_count, sizeof(uint64_t), 1, f) != 1 ||
            fwrite(e->regret, sizeof(double), e->n, f) != (size_t)e->n ||
            fwrite(e->avg, sizeof(double), e->n, f) != (size_t)e->n)
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
    if (memcmp(hdr.magic, "CFRCHKPT", 8) != 0 || (hdr.version != 1 && hdr.version != 2))
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
        e->ev_sum = ev_sum;
        e->ev_sq_sum = ev_sq_sum;
        e->ev_count = ev_count;
        if (fread(e->regret, sizeof(double), e->n, f) != (size_t)e->n ||
            fread(e->avg, sizeof(double), e->n, f) != (size_t)e->n)
        {
            int err = errno;
            fclose(f);
            errno = err;
            return -1;
        }
    }

    if (out_iteration)
        *out_iteration = hdr.iteration;

    if (fclose(f) != 0)
        return -1;
    return 0;
}
