/*
 * registry.c - Capability names and text form (architecture v3, CTR-02)
 *
 * Copyright (C) 2026 poker-eval contributors
 *
 * CTR-02 populated this module with the capability table and its text form;
 * CTR-06 adds the preset table and the axis names. The two belong together
 * because validating a preset is exactly a question about capabilities, and
 * because refusing a combination means being able to name what conflicts.
 *
 * The table is the single place a capability's name is written. pe_cap_name,
 * pe_cap_from_name, pe_caps_to_string and pe_caps_parse all read it, so a name
 * cannot drift between the four.
 */

#include <poker_eval/solver/pe_capabilities.h>
#include <poker_eval/solver/pe_solver_plan.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* GPU capabilities are opt-in only after the terminal parity test has passed.
   This process-local latch prevents a backend from advertising itself merely
   because its library was linked successfully. */
static int g_gpu_terminal_eval_gate_open;
static int g_gpu_regret_update_gate_open;

int pe_gpu_terminal_eval_gate_is_open(void)
{
    return g_gpu_terminal_eval_gate_open;
}

void pe_gpu_terminal_eval_gate_open(void)
{
    g_gpu_terminal_eval_gate_open = 1;
}

int pe_gpu_regret_update_gate_is_open(void)
{
    return g_gpu_regret_update_gate_open;
}

void pe_gpu_regret_update_gate_open(void)
{
    g_gpu_regret_update_gate_open = 1;
}

/* ------------------------------------------------------------------ *
 * The table
 * ------------------------------------------------------------------ */

typedef struct
{
    uint64_t bit;
    const char *name;
} pe_cap_entry_t;

/* Declaration order is the render order, so a printed plan always lists its
   capabilities from game model to operations rather than in bit order. */
static const pe_cap_entry_t k_cap_table[] = {
    { PE_CAP_VECTOR_FORM,            "VECTOR_FORM"            },
    { PE_CAP_PRIVATE_RANGES,         "PRIVATE_RANGES"         },
    { PE_CAP_FLOP_CHANCE,            "FLOP_CHANCE"            },
    { PE_CAP_DRAW_CHANCE,            "DRAW_CHANCE"            },
    { PE_CAP_MULTIWAY,               "MULTIWAY"               },
    { PE_CAP_ZERO_SUM_GUARANTEE,     "ZERO_SUM_GUARANTEE"     },
    { PE_CAP_NON_ZERO_SUM,           "NON_ZERO_SUM"           },
    { PE_CAP_NONLINEAR_UTILITY,      "NONLINEAR_UTILITY"      },
    { PE_CAP_ENUMERATED_CHANCE,      "ENUMERATED_CHANCE"      },
    { PE_CAP_DIRECT_CHANCE_SAMPLING, "DIRECT_CHANCE_SAMPLING" },
    { PE_CAP_ABSTRACTION,            "ABSTRACTION"            },
    { PE_CAP_SUIT_ISOMORPHISM,       "SUIT_ISOMORPHISM"       },
    { PE_CAP_BATCH_UPDATES,          "BATCH_UPDATES"          },
    { PE_CAP_CPU_PARALLEL,           "CPU_PARALLEL"           },
    { PE_CAP_GPU_TERMINAL_EVAL,      "GPU_TERMINAL_EVAL"      },
    { PE_CAP_GPU_VECTOR_SHOWDOWN,    "GPU_VECTOR_SHOWDOWN"    },
    { PE_CAP_GPU_REGRET_UPDATE,      "GPU_REGRET_UPDATE"      },
    { PE_CAP_GPU_TRAVERSAL,          "GPU_TRAVERSAL"          },
    { PE_CAP_RBP,                    "RBP"                    },
    { PE_CAP_LOCKED_STRATEGY,        "LOCKED_STRATEGY"        },
    { PE_CAP_PERIODIC_RELOCK,        "PERIODIC_RELOCK"        },
    { PE_CAP_SUBGAME_RESOLVE,        "SUBGAME_RESOLVE"        },
    { PE_CAP_CHECKPOINT,             "CHECKPOINT"             },
    { PE_CAP_DETERMINISTIC,          "DETERMINISTIC"          },
    { PE_CAP_IMPERFECT_INFO_BR,      "IMPERFECT_INFO_BR"      }
};

/* Keep PE_CAP_COUNT and the table from drifting apart. A mismatch is a build
   error rather than a silently short iteration. C99 has no static_assert, so
   this is the negative-array-size idiom. */
typedef char pe_cap_table_size_check[
    (sizeof(k_cap_table) / sizeof(k_cap_table[0]) == PE_CAP_COUNT) ? 1 : -1];

/* ------------------------------------------------------------------ *
 * Small helpers
 * ------------------------------------------------------------------ */

static int pe_ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

static int pe_name_equal_ci(const char *a, const char *b, size_t b_len)
{
    size_t i;
    for (i = 0; i < b_len; ++i)
    {
        if (a[i] == '\0')
            return 0;
        if (pe_ascii_lower((unsigned char)a[i]) != pe_ascii_lower((unsigned char)b[i]))
            return 0;
    }
    return a[i] == '\0';
}

static int pe_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/*
 * Append `text` at `*pos` of a snprintf-style buffer. `*pos` keeps counting
 * past the end of the buffer so the caller learns the length it would have
 * needed, which is what makes truncation detectable rather than silent.
 */
static void pe_append(char *buf, size_t buflen, size_t *pos, const char *text)
{
    size_t i;
    for (i = 0; text[i] != '\0'; ++i)
    {
        if (buf != NULL && *pos + 1 < buflen)
            buf[*pos] = text[i];
        (*pos)++;
    }
}

/* ------------------------------------------------------------------ *
 * Names
 * ------------------------------------------------------------------ */

const char *pe_cap_name(uint64_t cap)
{
    size_t i;

    /* Exactly one bit, or there is no single name to give. */
    if (cap == 0 || (cap & (cap - 1)) != 0)
        return NULL;

    for (i = 0; i < PE_CAP_COUNT; ++i)
    {
        if (k_cap_table[i].bit == cap)
            return k_cap_table[i].name;
    }
    return NULL;
}

uint64_t pe_cap_from_name(const char *name)
{
    size_t i;
    size_t len;

    if (name == NULL)
        return 0;

    len = strlen(name);
    if (len == 0)
        return 0;

    for (i = 0; i < PE_CAP_COUNT; ++i)
    {
        if (pe_name_equal_ci(k_cap_table[i].name, name, len))
            return k_cap_table[i].bit;
    }
    return 0;
}

uint64_t pe_cap_at(size_t index)
{
    if (index >= PE_CAP_COUNT)
        return 0;
    return k_cap_table[index].bit;
}

/* ------------------------------------------------------------------ *
 * Text form
 * ------------------------------------------------------------------ */

size_t pe_caps_to_string(uint64_t caps, char *buf, size_t buflen)
{
    size_t pos = 0;
    size_t i;
    int wrote_any = 0;
    uint64_t unnamed;

    if (buf == NULL && buflen != 0)
        return 0;

    if (caps == 0)
    {
        pe_append(buf, buflen, &pos, PE_CAPS_NONE_TOKEN);
    }
    else
    {
        for (i = 0; i < PE_CAP_COUNT; ++i)
        {
            if ((caps & k_cap_table[i].bit) == 0)
                continue;
            if (wrote_any)
                pe_append(buf, buflen, &pos, "|");
            pe_append(buf, buflen, &pos, k_cap_table[i].name);
            wrote_any = 1;
        }

        /* Bits this build has no name for are carried through as hex rather
           than dropped, so a plan written by a newer build survives a
           round-trip here instead of losing capabilities in silence. */
        unnamed = caps & ~(uint64_t)PE_CAP_ALL;
        if (unnamed != 0)
        {
            char hex[19]; /* "0x" + 16 digits + NUL */
            int digit;
            size_t at = 0;
            int started = 0;
            int shift;

            hex[at++] = '0';
            hex[at++] = 'x';
            for (shift = 60; shift >= 0; shift -= 4)
            {
                digit = (int)((unnamed >> shift) & 0xF);
                if (digit == 0 && !started && shift != 0)
                    continue;
                started = 1;
                hex[at++] = (char)((digit < 10) ? ('0' + digit) : ('a' + digit - 10));
            }
            hex[at] = '\0';

            if (wrote_any)
                pe_append(buf, buflen, &pos, "|");
            pe_append(buf, buflen, &pos, hex);
        }
    }

    if (buf != NULL && buflen != 0)
        buf[(pos < buflen) ? pos : (buflen - 1)] = '\0';

    return pos;
}

int pe_caps_parse(const char *text, uint64_t *out_caps)
{
    uint64_t caps = 0;
    const char *p;
    int token_index = 0;

    if (text == NULL || out_caps == NULL)
        return -1;

    p = text;
    for (;;)
    {
        const char *start;
        const char *end;
        size_t len;
        uint64_t bit;

        while (pe_is_space(*p))
            p++;

        start = p;
        while (*p != '\0' && *p != '|')
            p++;
        end = p;

        /* Trim trailing spaces of the token. */
        while (end > start && pe_is_space(end[-1]))
            end--;

        len = (size_t)(end - start);
        token_index++;

        if (len == 0)
        {
            /* An empty token is only acceptable when the whole text is empty,
               which means the empty capability set. Anything else — a leading,
               trailing or doubled '|' — is a malformed token, reported by
               position. */
            if (token_index == 1 && *p == '\0')
            {
                *out_caps = 0;
                return 0;
            }
            return token_index;
        }

        if (pe_name_equal_ci(PE_CAPS_NONE_TOKEN, start, len))
        {
            /* NONE is only meaningful on its own. */
            if (token_index != 1 || *p != '\0')
                return token_index;
            *out_caps = 0;
            return 0;
        }

        if (len > 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X'))
        {
            char hex[17];
            char *stop = NULL;
            unsigned long long value;

            if (len - 2 > sizeof(hex) - 1)
                return token_index;
            memcpy(hex, start + 2, len - 2);
            hex[len - 2] = '\0';

            value = strtoull(hex, &stop, 16);
            if (stop == hex || *stop != '\0')
                return token_index;
            caps |= (uint64_t)value;
        }
        else
        {
            char name[64];

            if (len > sizeof(name) - 1)
                return token_index;
            memcpy(name, start, len);
            name[len] = '\0';

            bit = pe_cap_from_name(name);
            if (bit == 0)
                return token_index;
            caps |= bit;
        }

        if (*p == '\0')
            break;
        p++; /* skip the '|' */
    }

    *out_caps = caps;
    return 0;
}

/* ================================================================== *
 * CTR-06 — presets and axis names
 *
 * The preset table is the whole of "presets are surface": a name maps to a
 * combination of axes and nothing else. Expanding one never bypasses the
 * validation in plan.c.
 * ================================================================== */


typedef struct
{
    pe_algorithm_preset_t preset;
    const char *name;
    pe_traversal_mode_t traversal;
    pe_regret_mode_t regret;
    pe_policy_mode_t policy;
    pe_averaging_mode_t averaging;
    int experimental;
} pe_algo_preset_entry_t;

/* Architecture v3 §5, "Matrice d'algorithmes initiale", row for row.
 *
 * Sampling presets use the explicit importance-corrected averaging mode.
 * External DCFR retains POWER because its temporal discount is part of the
 * preset contract; the traversal applies the sampling correction separately.
 */
static const pe_algo_preset_entry_t k_preset_table[] = {
    { PE_PRESET_CUSTOM, "custom",
      PE_TRAVERSAL_FULL_SCALAR, PE_REGRET_VANILLA,
      PE_POLICY_REGRET_MATCHING, PE_AVG_UNIFORM, 0 },

    { PE_PRESET_CFR, "cfr",
      PE_TRAVERSAL_FULL_SCALAR, PE_REGRET_VANILLA,
      PE_POLICY_REGRET_MATCHING, PE_AVG_UNIFORM, 0 },

    { PE_PRESET_CFR_VECTOR, "cfr-vector",
      PE_TRAVERSAL_FULL_VECTOR, PE_REGRET_VANILLA,
      PE_POLICY_REGRET_MATCHING, PE_AVG_UNIFORM, 0 },

    { PE_PRESET_CFR_PLUS, "cfr+",
      PE_TRAVERSAL_FULL_VECTOR, PE_REGRET_PLUS,
      PE_POLICY_REGRET_MATCHING, PE_AVG_DELAYED_LINEAR, 0 },

    { PE_PRESET_DCFR, "dcfr",
      PE_TRAVERSAL_FULL_VECTOR, PE_REGRET_DCFR,
      PE_POLICY_REGRET_MATCHING, PE_AVG_POWER, 0 },

    { PE_PRESET_CFR_PLUS_CHANCE, "cfr+-chance",
      PE_TRAVERSAL_CHANCE_VECTOR, PE_REGRET_PLUS,
      PE_POLICY_REGRET_MATCHING, PE_AVG_DELAYED_LINEAR, 0 },

    { PE_PRESET_EXTERNAL_MCCFR, "external-mccfr",
      PE_TRAVERSAL_EXTERNAL_SAMPLING, PE_REGRET_VANILLA,
      PE_POLICY_REGRET_MATCHING, PE_AVG_IMPORTANCE, 0 },

    { PE_PRESET_EXTERNAL_DCFR, "external-dcfr",
      PE_TRAVERSAL_EXTERNAL_SAMPLING, PE_REGRET_DCFR,
      PE_POLICY_REGRET_MATCHING, PE_AVG_POWER, 0 },

    { PE_PRESET_OUTCOME_MCCFR, "outcome-mccfr",
      PE_TRAVERSAL_OUTCOME_SAMPLING, PE_REGRET_VANILLA,
      PE_POLICY_REGRET_MATCHING, PE_AVG_IMPORTANCE, 0 },

    { PE_PRESET_ECFR, "ecfr",
      PE_TRAVERSAL_FULL_SCALAR, PE_REGRET_LEGACY_EXP,
      PE_POLICY_EXPONENTIAL, PE_AVG_UNIFORM, 1 }
};

typedef char pe_preset_table_size_check[
    (sizeof(k_preset_table) / sizeof(k_preset_table[0]) == PE_PRESET_COUNT) ? 1 : -1];

static const pe_algo_preset_entry_t *pe_preset_entry(pe_algorithm_preset_t preset)
{
    size_t i;
    for (i = 0; i < PE_PRESET_COUNT; ++i)
    {
        if (k_preset_table[i].preset == preset)
            return &k_preset_table[i];
    }
    return NULL;
}

const char *pe_preset_name(pe_algorithm_preset_t preset)
{
    const pe_algo_preset_entry_t *e = pe_preset_entry(preset);
    return (e != NULL) ? e->name : NULL;
}

int pe_preset_is_experimental(pe_algorithm_preset_t preset)
{
    const pe_algo_preset_entry_t *e = pe_preset_entry(preset);
    return (e != NULL) ? e->experimental : 0;
}

pe_algorithm_preset_t pe_preset_from_name(const char *name)
{
    size_t i;
    size_t len;

    if (name == NULL)
        return PE_PRESET_COUNT;

    len = strlen(name);
    if (len == 0)
        return PE_PRESET_COUNT;

    for (i = 0; i < PE_PRESET_COUNT; ++i)
    {
        if (pe_name_equal_ci(k_preset_table[i].name, name, len))
            return k_preset_table[i].preset;
    }
    return PE_PRESET_COUNT;
}

int pe_preset_expand(pe_algorithm_preset_t preset, pe_algorithm_config_t *inout)
{
    const pe_algo_preset_entry_t *e;

    if (inout == NULL)
        return -1;

    /* CUSTOM means the axes already in the configuration are what the caller
       wants; overwriting them here would make the preset field impossible to
       leave alone. */
    if (preset == PE_PRESET_CUSTOM)
        return 0;

    e = pe_preset_entry(preset);
    if (e == NULL)
        return -1;

    /* Pruning and the numeric parameters are deliberately untouched: a preset
       says which algorithm, not how it is tuned, so `--algorithm dcfr
       --alpha 2.0` works without the preset stamping alpha back. */
    inout->traversal = e->traversal;
    inout->regret = e->regret;
    inout->policy = e->policy;
    inout->averaging = e->averaging;
    return 0;
}

/* ------------------------------------------------------------------ *
 * Axis names
 * ------------------------------------------------------------------ */

#define PE_NAME_OF(value, table)                                        \
    (((size_t)(value) < sizeof(table) / sizeof((table)[0]))             \
         ? (table)[(size_t)(value)]                                     \
         : NULL)

const char *pe_traversal_name(pe_traversal_mode_t mode)
{
    static const char *const names[] = {
        "full-vector", "chance-vector", "full-scalar",
        "external-sampling", "outcome-sampling"
    };
    return PE_NAME_OF(mode, names);
}

pe_traversal_mode_t pe_traversal_from_name(const char *name)
{
    static const char *const names[] = {
        "full-vector", "chance-vector", "full-scalar",
        "external-sampling", "outcome-sampling"
    };
    size_t length;
    size_t i;

    if (name == NULL || *name == '\0')
        return PE_TRAVERSAL_COUNT;
    length = strlen(name);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (pe_name_equal_ci(names[i], name, length))
            return (pe_traversal_mode_t)i;
    return PE_TRAVERSAL_COUNT;
}

const char *pe_regret_name(pe_regret_mode_t mode)
{
    static const char *const names[] = { "vanilla", "plus", "dcfr", "legacy-exp" };
    return PE_NAME_OF(mode, names);
}

pe_regret_mode_t pe_regret_from_name(const char *name)
{
    static const char *const names[] = { "vanilla", "plus", "dcfr", "legacy-exp" };
    size_t length;
    size_t i;

    if (name == NULL || *name == '\0')
        return PE_REGRET_COUNT;
    length = strlen(name);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (pe_name_equal_ci(names[i], name, length))
            return (pe_regret_mode_t)i;
    return PE_REGRET_COUNT;
}

const char *pe_policy_name(pe_policy_mode_t mode)
{
    static const char *const names[] = { "regret-matching", "exponential" };
    return PE_NAME_OF(mode, names);
}

const char *pe_averaging_name(pe_averaging_mode_t mode)
{
    static const char *const names[] = {
        "uniform", "linear", "power", "delayed-linear", "importance"
    };
    return PE_NAME_OF(mode, names);
}

pe_averaging_mode_t pe_averaging_from_name(const char *name)
{
    static const char *const names[] = {
        "uniform", "linear", "power", "delayed-linear", "importance"
    };
    size_t length;
    size_t i;

    if (name == NULL || *name == '\0')
        return PE_AVG_COUNT;
    length = strlen(name);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (pe_name_equal_ci(names[i], name, length))
            return (pe_averaging_mode_t)i;
    return PE_AVG_COUNT;
}

const char *pe_pruning_name(pe_pruning_mode_t mode)
{
    static const char *const names[] = { "none", "rbp" };
    return PE_NAME_OF(mode, names);
}

const char *pe_precision_name(pe_precision_mode_t mode)
{
    static const char *const names[] = { "f64", "f32", "mixed", "fixed16" };
    return PE_NAME_OF(mode, names);
}

pe_precision_mode_t pe_precision_from_name(const char *name)
{
    static const char *const names[] = { "f64", "f32", "mixed", "fixed16" };
    size_t length;
    size_t i;

    if (name == NULL || *name == '\0')
        return PE_PREC_COUNT;
    length = strlen(name);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (pe_name_equal_ci(names[i], name, length))
            return (pe_precision_mode_t)i;
    return PE_PREC_COUNT;
}

const char *pe_compute_kind_name(pe_compute_kind_t kind)
{
    static const char *const names[] = { "auto", "cpu_ref", "cpu_par", "cuda", "opencl" };
    return PE_NAME_OF(kind, names);
}

pe_compute_kind_t pe_compute_kind_from_name(const char *name)
{
    static const char *const names[] = { "auto", "cpu_ref", "cpu_par", "cuda", "opencl" };
    size_t length;
    size_t i;

    if (name == NULL || *name == '\0')
        return PE_COMPUTE_COUNT;
    length = strlen(name);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        if (pe_name_equal_ci(names[i], name, length))
            return (pe_compute_kind_t)i;
    }
    /* CLI shorthand retained for the documented --backend cpu spelling. */
    if (pe_name_equal_ci("cpu", name, length))
        return PE_COMPUTE_CPU_REF;
    return PE_COMPUTE_COUNT;
}

const char *pe_valid_severity_name(pe_valid_severity_t severity)
{
    static const char *const names[] = { "ok", "warning", "fallback", "error" };
    return PE_NAME_OF(severity, names);
}
