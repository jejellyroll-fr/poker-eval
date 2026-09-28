/* br_sampling.c - confidence-guided Monte Carlo decisions (issue #257).
   See <poker_eval/solver/pe_br_sampling.h>. */

#include <poker_eval/solver/pe_br_sampling.h>

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "finite_double.h"

#define BR_DEFAULT_MIN 4u
#define BR_DEFAULT_CHECK 4u
#define BR_DEFAULT_CONFIDENCE 0.95
#define BR_MAX_SAMPLES_CEILING (1u << 20)

/* "Left at zero" without == on a double; false for NaN. */
static int is_unset(double value) { return value >= 0.0 && value <= 0.0; }

int pe_br_sampling_resolve(const pe_br_sampling_config_t *in,
                           pe_br_sampling_config_t *out)
{
    pe_br_sampling_config_t r;
    if (!out)
        return -1;
    if (in)
        r = *in;
    else
        memset(&r, 0, sizeof(r));
    if (r.max_samples == 0u)
    {
        *out = r; /* off */
        return 0;
    }
    if (r.min_samples == 0u)
        r.min_samples = BR_DEFAULT_MIN;
    if (r.min_samples < 2u)
        r.min_samples = 2u; /* a spread needs two draws */
    if (r.check_interval == 0u)
        r.check_interval = BR_DEFAULT_CHECK;
    if (is_unset(r.confidence))
        r.confidence = BR_DEFAULT_CONFIDENCE;
    if (!(r.confidence > 0.0 && r.confidence < 1.0) ||
        !pe_finite_double(r.absolute_tolerance) || r.absolute_tolerance < 0.0 ||
        !pe_finite_double(r.relative_tolerance) || r.relative_tolerance < 0.0 ||
        r.max_samples > BR_MAX_SAMPLES_CEILING ||
        r.min_samples > r.max_samples)
        return -1;
    /* A confidence so close to 1 that the union-bound level rounds to 1
       would pass here and then fail every decision. The z grows with the
       action count, so the widest decision covers every other. */
    if (!pe_finite_double(pe_br_sequential_z(&r, (uint16_t)PE_BR_SAMPLING_MAX_ACTIONS)))
        return -1;
    *out = r;
    return 0;
}

double pe_br_sequential_z(const pe_br_sampling_config_t *resolved,
                          uint16_t actions)
{
    double looks, alpha;
    if (!resolved || actions == 0u || resolved->max_samples == 0u)
        return NAN;
    /* The first look after min_samples, then one per check_interval until
       max_samples: a finite schedule, so a union bound covers it. */
    looks = 1.0;
    if (resolved->max_samples > resolved->min_samples &&
        resolved->check_interval > 0u)
        looks += ceil((double)(resolved->max_samples - resolved->min_samples) /
                      (double)resolved->check_interval);
    alpha = (1.0 - resolved->confidence) / (looks * (double)actions);
    return pe_confidence_z(1.0 - alpha);
}

static double half_width(const pe_online_stats_t *s, double z)
{
    if (s->n < 2u)
        return INFINITY;
    return pe_online_stats_half_width(s, z);
}

int pe_br_resolve_decision(uint16_t actions, pe_br_sample_fn sample,
                           void *user, const pe_br_sampling_config_t *config,
                           pe_br_decision_t *out,
                           pe_br_sampling_stats_t *stats)
{
    pe_online_stats_t est[PE_BR_SAMPLING_MAX_ACTIONS];
    int alive[PE_BR_SAMPLING_MAX_ACTIONS];
    double z;
    uint32_t round;
    uint16_t best = 0u;
    pe_br_decision_end_t end = PE_BR_DECISION_MAX_BUDGET;

    if (!sample || !config || !out || actions == 0u ||
        actions > PE_BR_SAMPLING_MAX_ACTIONS || config->max_samples == 0u ||
        config->min_samples < 2u || config->min_samples > config->max_samples)
        return -1;
    memset(out, 0, sizeof(*out));

    if (actions == 1u)
    {
        /* Nothing to decide: estimate the only action with the minimum. */
        pe_online_stats_t s;
        pe_online_stats_reset(&s);
        for (uint32_t i = 0; i < config->min_samples; ++i)
        {
            double v = sample(user, 0u);
            if (!pe_finite_double(v))
                return -1;
            pe_online_stats_add(&s, v);
        }
        out->value = s.mean;
        out->samples = s.n;
        out->end = PE_BR_DECISION_SINGLE;
        if (stats)
        {
            stats->decisions++;
            stats->samples += s.n;
            stats->histogram[0]++;
        }
        return 0;
    }

    z = pe_br_sequential_z(config, actions);
    if (!pe_finite_double(z))
        return -1;
    for (uint16_t a = 0; a < actions; ++a)
    {
        pe_online_stats_reset(&est[a]);
        alive[a] = 1;
    }

    /* Round 0 draws min_samples per action; later rounds check_interval per
       survivor. Draws go round by round, action by action. */
    for (round = 0;; ++round)
    {
        uint32_t per = round == 0 ? config->min_samples : config->check_interval;
        int drew = 0;
        /* No survivor can take more than its remaining budget, so a round
           never needs more passes than the largest one. */
        {
            uint32_t room = 0u;
            for (uint16_t a = 0; a < actions; ++a)
                if (alive[a] && config->max_samples - (uint32_t)est[a].n > room)
                    room = config->max_samples - (uint32_t)est[a].n;
            if (per > room)
                per = room;
        }
        for (uint32_t k = 0; k < per; ++k)
            for (uint16_t a = 0; a < actions; ++a)
            {
                double v;
                if (!alive[a] || est[a].n >= config->max_samples)
                    continue;
                v = sample(user, a);
                if (!pe_finite_double(v))
                    return -1;
                pe_online_stats_add(&est[a], v);
                drew = 1;
            }

        /* Leader among survivors. */
        best = actions;
        for (uint16_t a = 0; a < actions; ++a)
            if (alive[a] && (best == actions || est[a].mean > est[best].mean))
                best = a;

        /* Eliminate what the leader's lower bound clears. */
        {
            double lower = est[best].mean - half_width(&est[best], z);
            double upper_rest = -INFINITY;
            int survivors = 0;
            for (uint16_t a = 0; a < actions; ++a)
            {
                double upper;
                if (!alive[a] || a == best)
                    continue;
                upper = est[a].mean + half_width(&est[a], z);
                if (upper < lower)
                {
                    alive[a] = 0;
                    out->eliminated++;
                    continue;
                }
                survivors++;
                if (upper > upper_rest)
                    upper_rest = upper;
            }
            if (survivors == 0)
            {
                end = PE_BR_DECISION_SEPARATED;
                break;
            }
            {
                double tolerance = config->relative_tolerance * fabs(est[best].mean);
                if (config->absolute_tolerance > tolerance)
                    tolerance = config->absolute_tolerance;
                if (tolerance > 0.0 && upper_rest - lower <= tolerance)
                {
                    end = PE_BR_DECISION_TOLERANCE;
                    break;
                }
            }
        }
        if (!drew)
        {
            end = PE_BR_DECISION_MAX_BUDGET; /* every survivor at the cap */
            break;
        }
        {
            int room = 0;
            for (uint16_t a = 0; a < actions; ++a)
                if (alive[a] && est[a].n < config->max_samples)
                    room = 1;
            if (!room)
            {
                end = PE_BR_DECISION_MAX_BUDGET;
                break;
            }
        }
    }

    {
        uint16_t runner = actions;
        uint64_t most = 0u;
        for (uint16_t a = 0; a < actions; ++a)
        {
            out->samples += est[a].n;
            if (est[a].n > most)
                most = est[a].n;
            if (a != best && (runner == actions || est[a].mean > est[runner].mean))
                runner = a;
        }
        out->best = best;
        out->selection_value = est[best].mean;
        out->end = end;
        if (runner < actions)
        {
            out->gap = est[best].mean - est[runner].mean;
            out->gap_half_width =
                half_width(&est[best], z) + half_width(&est[runner], z);
        }
        /* Estimate the chosen action from draws that did not choose it. */
        {
            pe_online_stats_t fresh;
            pe_online_stats_reset(&fresh);
            for (uint32_t i = 0; i < config->min_samples; ++i)
            {
                double v = sample(user, best);
                if (!pe_finite_double(v))
                    return -1;
                pe_online_stats_add(&fresh, v);
            }
            out->value = fresh.mean;
            out->samples += fresh.n;
        }
        if (stats)
        {
            unsigned bucket = 0u;
            uint64_t limit = config->min_samples;
            while (bucket + 1u < PE_BR_SAMPLING_HISTOGRAM && most > limit)
            {
                limit *= 2u;
                bucket++;
            }
            stats->decisions++;
            stats->samples += out->samples;
            stats->eliminated_actions += out->eliminated;
            stats->histogram[bucket]++;
            if (end == PE_BR_DECISION_SEPARATED)
                stats->separated++;
            else if (end == PE_BR_DECISION_TOLERANCE)
                stats->tolerance_stops++;
            else
                stats->max_budget_hits++;
            if (pe_finite_double(out->gap))
                stats->gap_sum += out->gap;
            if (pe_finite_double(out->gap_half_width))
                stats->gap_half_width_sum += out->gap_half_width;
        }
    }
    return 0;
}

static int parse_u32(const char *text, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (!text || !*text || *text == '-')
        return -1;
    errno = 0;
    v = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || v > UINT32_MAX)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_real(const char *text, double *out)
{
    char *end = NULL;
    double v;
    if (!text || !*text)
        return -1;
    errno = 0;
    v = strtod(text, &end);
    if (errno != 0 || !end || *end != '\0' || !pe_finite_double(v))
        return -1;
    *out = v;
    return 0;
}

int pe_br_sampling_parse_option(pe_br_sampling_config_t *config,
                                const char *key, const char *value)
{
    if (!config || !key)
        return -1;
    if (strcmp(key, "min-samples") == 0)
        return parse_u32(value, &config->min_samples);
    if (strcmp(key, "max-samples") == 0)
        return parse_u32(value, &config->max_samples);
    if (strcmp(key, "check-interval") == 0)
        return parse_u32(value, &config->check_interval);
    if (strcmp(key, "confidence") == 0)
        return parse_real(value, &config->confidence);
    if (strcmp(key, "absolute-tolerance") == 0)
        return parse_real(value, &config->absolute_tolerance);
    if (strcmp(key, "relative-tolerance") == 0)
        return parse_real(value, &config->relative_tolerance);
    return -1;
}
