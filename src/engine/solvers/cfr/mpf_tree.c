#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#include "jsmn.h"

static int mpf_debug_enabled(void)
{
    static int cached = -1;
    if (cached == -1)
    {
        const char *env = getenv("POKER_DEBUG_MPF");
        cached = (env && *env) ? 1 : 0;
    }
    return cached;
}

#define MPF_DEBUG(...)                       \
    do                                       \
    {                                        \
        if (mpf_debug_enabled())             \
            fprintf(stderr, __VA_ARGS__);    \
    } while (0)

#define MPF_TREE_VERSION_CURRENT 1
#define MPF_TREE_MAX_TOKENS 8192

typedef struct
{
    const char *json;
    jsmntok_t *tokens;
    int count;
} mpf_json_doc_t;

typedef struct
{
    char *data;
    size_t len;
    size_t cap;
} mpf_buf_t;

static const char *mpf_street_to_string(mpf_street_t street)
{
    switch (street)
    {
    case MPF_STREET_PREFLOP:
        return "PREFLOP";
    case MPF_STREET_FLOP:
        return "FLOP";
    case MPF_STREET_TURN:
        return "TURN";
    case MPF_STREET_RIVER:
        return "RIVER";
    case MPF_STREET_SHOWDOWN:
        return "SHOWDOWN";
    default:
        return "PREFLOP";
    }
}

static char *mpf_strndup(const char *src, size_t len)
{
    char *dst = (char *)malloc(len + 1);
    if (!dst)
        return NULL;
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

static void mpf_tree_error(mpf_tree_error_t *err, const char *msg)
{
    if (!err)
        return;
    err->line = 0;
    err->column = 0;
    strncpy(err->message, msg, sizeof(err->message) - 1);
    err->message[sizeof(err->message) - 1] = '\0';
}

static int mpf_json_next(const jsmntok_t *tokens, int idx)
{
    const jsmntok_t *tok = &tokens[idx];
    if (tok->type == JSMN_OBJECT || tok->type == JSMN_ARRAY)
    {
        int j = idx + 1;
        for (int i = 0; i < tok->size; ++i)
            j = mpf_json_next(tokens, j);
        return j;
    }
    return idx + 1;
}

static int mpf_token_streq(const char *json, const jsmntok_t *tok, const char *s)
{
    size_t len = (size_t)(tok->end - tok->start);
    return strlen(s) == len && strncmp(json + tok->start, s, len) == 0;
}

static int mpf_token_to_int(const char *json, const jsmntok_t *tok, int *out)
{
    char buf[64];
    size_t len = (size_t)(tok->end - tok->start);
    if (len == 0 || len >= sizeof(buf))
        return 0;
    memcpy(buf, json + tok->start, len);
    buf[len] = '\0';
    char *endptr = NULL;
    long v = strtol(buf, &endptr, 10);
    if (endptr == buf || *endptr != '\0')
        return 0;
    *out = (int)v;
    return 1;
}

static int mpf_token_to_double(const char *json, const jsmntok_t *tok, double *out)
{
    char buf[64];
    size_t len = (size_t)(tok->end - tok->start);
    if (len == 0 || len >= sizeof(buf))
        return 0;
    memcpy(buf, json + tok->start, len);
    buf[len] = '\0';
    char *endptr = NULL;
    double v = strtod(buf, &endptr);
    if (endptr == buf || *endptr != '\0')
        return 0;
    *out = v;
    return 1;
}

static int mpf_token_to_bool(const char *json, const jsmntok_t *tok, int *out)
{
    size_t len = (size_t)(tok->end - tok->start);
    if (len == 4 && strncmp(json + tok->start, "true", 4) == 0)
    {
        *out = 1;
        return 1;
    }
    if (len == 5 && strncmp(json + tok->start, "false", 5) == 0)
    {
        *out = 0;
        return 1;
    }
    return 0;
}

static int mpf_parse_street(const char *json, const jsmntok_t *tok, mpf_street_t *out)
{
    size_t len = (size_t)(tok->end - tok->start);
    char buf[16];
    if (len == 0 || len >= sizeof(buf))
        return 0;
    for (size_t i = 0; i < len; ++i)
        buf[i] = (char)toupper((unsigned char)json[tok->start + (int)i]);
    buf[len] = '\0';
    if (strcmp(buf, "PREFLOP") == 0)
    {
        *out = MPF_STREET_PREFLOP;
        return 1;
    }
    if (strcmp(buf, "FLOP") == 0)
    {
        *out = MPF_STREET_FLOP;
        return 1;
    }
    if (strcmp(buf, "TURN") == 0)
    {
        *out = MPF_STREET_TURN;
        return 1;
    }
    if (strcmp(buf, "RIVER") == 0)
    {
        *out = MPF_STREET_RIVER;
        return 1;
    }
    if (strcmp(buf, "SHOWDOWN") == 0)
    {
        *out = MPF_STREET_SHOWDOWN;
        return 1;
    }
    return 0;
}

static void mpf_tree_profile_free(mpf_tree_bet_profile_t *profile)
{
    if (!profile)
        return;
    MPF_DEBUG("MPF: freeing bet profile %s\n", profile->id ? profile->id : "<unnamed>");
    free(profile->id);
    free(profile->bet_sizes);
}

static void mpf_tree_range_combo_reset(mpf_tree_range_combo_t *combo)
{
    if (!combo)
        return;
    MPF_DEBUG("MPF: resetting combo %p\n", (void *)combo);
    free(combo->hand);
    combo->hand = NULL;
    combo->weight = 0.0;
}

static void mpf_tree_range_profile_free(mpf_tree_range_profile_t *profile)
{
    if (!profile)
        return;
    MPF_DEBUG("MPF: freeing range profile %s (aliases=%d, combos=%d)\n",
              profile->id ? profile->id : "<unnamed>",
              profile->alias_count,
              profile->combo_count);
    free(profile->id);
    profile->id = NULL;
    if (profile->aliases)
    {
        for (int i = 0; i < profile->alias_count; ++i)
        {
            free(profile->aliases[i]);
            profile->aliases[i] = NULL;
        }
        free(profile->aliases);
        profile->aliases = NULL;
    }
    if (profile->combos)
    {
        for (int i = 0; i < profile->combo_count; ++i)
            mpf_tree_range_combo_reset(&profile->combos[i]);
        free(profile->combos);
        profile->combos = NULL;
    }
    if (profile->spot_rules)
    {
        for (int i = 0; i < profile->spot_rule_count; ++i)
            free(profile->spot_rules[i].hand);
        free(profile->spot_rules);
        profile->spot_rules = NULL;
    }
    free(profile->cb_range_id);
    profile->cb_range_id = NULL;
    profile->combo_count = 0;
    profile->spot_rule_count = 0;
    profile->has_cb = 0;
    profile->alias_count = 0;
    profile->player = -1;
    profile->street = MPF_STREET_PREFLOP;
    profile->street_defined = 0;
}

static void mpf_tree_action_reset(mpf_tree_action_t *action)
{
    if (!action)
        return;
    MPF_DEBUG("MPF: resetting action %p type=%d\n", (void *)action, action->type);
    free(action->next_id);
}

static void mpf_tree_node_free(mpf_tree_node_t *node)
{
    if (!node)
        return;
    MPF_DEBUG("MPF: freeing node %p type=%d actions=%d\n",
              (void *)node, node->type, node->action_count);
#if !defined(_WIN32)
    pthread_mutex_destroy(&node->cache_lock);
    for (int s = 0; s < MPF_NODE_CACHE_SLOTS; ++s)
    {
        if (node->cache_slots[s].state)
        {
            mpf_state_cleanup_cached(node->cache_slots[s].state);
            free(node->cache_slots[s].state);
            node->cache_slots[s].state = NULL;
        }
        node->cache_slots[s].owner = 0;
    }
#endif
    free(node->id);
    free(node->bet_profile_id);
    free(node->range_profile_id);
    free(node->bet_sizes);
    free(node->locked_strategy);
    if (node->actions)
    {
        for (int i = 0; i < node->action_count; ++i)
            mpf_tree_action_reset(&node->actions[i]);
        free(node->actions);
    }
}

static void mpf_snapshot_init(mpf_tree_snapshot_t *snap);
static int mpf_parse_double_array(const char *json,
                                  const jsmntok_t *tokens,
                                  int array_index,
                                  double *out,
                                  int max,
                                  mpf_tree_error_t *err,
                                  const char *label);
static int mpf_parse_int_array(const char *json,
                               const jsmntok_t *tokens,
                               int array_index,
                               int *out,
                               int max,
                               mpf_tree_error_t *err,
                               const char *label);

static int mpf_parse_snapshot(const char *json,
                              const jsmntok_t *tokens,
                              int obj_index,
                              mpf_tree_snapshot_t *snap,
                              mpf_tree_error_t *err)
{
    const jsmntok_t *obj = &tokens[obj_index];
    if (obj->type != JSMN_OBJECT)
    {
        mpf_tree_error(err, "snapshot must be an object");
        return 0;
    }
    mpf_snapshot_init(snap);
    int board_len = 0;
    int idx = obj_index + 1;
    for (int i = 0; i < obj->size; ++i)
    {
        const jsmntok_t *key = &tokens[idx];
        int next_idx;
        if (key->type == JSMN_STRING)
        {
            int value_idx = idx + 1;
            if (mpf_token_streq(json, key, "street"))
            {
                if (!mpf_parse_street(json, &tokens[value_idx], &snap->street))
                {
                    mpf_tree_error(err, "snapshot.street invalid");
                    return 0;
                }
                snap->has_street = 1;
            }
            else if (mpf_token_streq(json, key, "num_players"))
            {
                if (!mpf_token_to_int(json, &tokens[value_idx], &snap->num_players))
                {
                    mpf_tree_error(err, "snapshot.num_players must be integer");
                    return 0;
                }
                snap->has_num_players = 1;
            }
            else if (mpf_token_streq(json, key, "to_act"))
            {
                if (!mpf_token_to_int(json, &tokens[value_idx], &snap->to_act))
                {
                    mpf_tree_error(err, "snapshot.to_act must be integer");
                    return 0;
                }
                snap->has_to_act = 1;
            }
            else if (mpf_token_streq(json, key, "first_to_act"))
            {
                if (!mpf_token_to_int(json, &tokens[value_idx], &snap->first_to_act))
                {
                    mpf_tree_error(err, "snapshot.first_to_act must be integer");
                    return 0;
                }
                snap->has_first_to_act = 1;
            }
            else if (mpf_token_streq(json, key, "pot"))
            {
                if (!mpf_token_to_double(json, &tokens[value_idx], &snap->pot))
                {
                    mpf_tree_error(err, "snapshot.pot must be numeric");
                    return 0;
                }
                snap->has_pot = 1;
            }
            else if (mpf_token_streq(json, key, "to_call"))
            {
                if (!mpf_token_to_double(json, &tokens[value_idx], &snap->to_call))
                {
                    mpf_tree_error(err, "snapshot.to_call must be numeric");
                    return 0;
                }
                snap->has_to_call = 1;
            }
            else if (mpf_token_streq(json, key, "current_bet"))
            {
                if (!mpf_token_to_double(json, &tokens[value_idx], &snap->current_bet))
                {
                    mpf_tree_error(err, "snapshot.current_bet must be numeric");
                    return 0;
                }
                snap->has_current_bet = 1;
            }
            else if (mpf_token_streq(json, key, "raises_made"))
            {
                if (!mpf_token_to_int(json, &tokens[value_idx], &snap->raises_made))
                {
                    mpf_tree_error(err, "snapshot.raises_made must be integer");
                    return 0;
                }
                snap->has_raises_made = 1;
            }
            else if (mpf_token_streq(json, key, "board"))
            {
                if (!mpf_parse_int_array(json, tokens, value_idx, snap->board_cards, 5, err, "snapshot.board"))
                    return 0;
                board_len = tokens[value_idx].size;
                if (board_len > 5)
                    board_len = 5;
                snap->board_len = board_len;
                snap->has_board = 1;
            }
            else if (mpf_token_streq(json, key, "board_revealed"))
            {
                if (!mpf_token_to_int(json, &tokens[value_idx], &snap->board_revealed))
                {
                    mpf_tree_error(err, "snapshot.board_revealed must be integer");
                    return 0;
                }
                snap->has_board_revealed = 1;
            }
            else if (mpf_token_streq(json, key, "stacks"))
            {
                if (!mpf_parse_double_array(json, tokens, value_idx, snap->stacks, MPF_MAX_PLAYERS, err, "snapshot.stacks"))
                    return 0;
                snap->stacks_len = tokens[value_idx].size;
                if (snap->stacks_len > MPF_MAX_PLAYERS)
                    snap->stacks_len = MPF_MAX_PLAYERS;
                snap->has_stacks = 1;
            }
            else if (mpf_token_streq(json, key, "invested"))
            {
                if (!mpf_parse_double_array(json, tokens, value_idx, snap->invested, MPF_MAX_PLAYERS, err, "snapshot.invested"))
                    return 0;
                snap->invested_len = tokens[value_idx].size;
                if (snap->invested_len > MPF_MAX_PLAYERS)
                    snap->invested_len = MPF_MAX_PLAYERS;
                snap->has_invested = 1;
            }
            else if (mpf_token_streq(json, key, "round_contrib"))
            {
                if (!mpf_parse_double_array(json, tokens, value_idx, snap->round_contrib, MPF_MAX_PLAYERS, err, "snapshot.round_contrib"))
                    return 0;
                snap->round_contrib_len = tokens[value_idx].size;
                if (snap->round_contrib_len > MPF_MAX_PLAYERS)
                    snap->round_contrib_len = MPF_MAX_PLAYERS;
                snap->has_round_contrib = 1;
            }
            else if (mpf_token_streq(json, key, "active"))
            {
                if (!mpf_parse_int_array(json, tokens, value_idx, snap->active, MPF_MAX_PLAYERS, err, "snapshot.active"))
                    return 0;
                snap->active_len = tokens[value_idx].size;
                if (snap->active_len > MPF_MAX_PLAYERS)
                    snap->active_len = MPF_MAX_PLAYERS;
                snap->has_active = 1;
            }
            else if (mpf_token_streq(json, key, "acted"))
            {
                if (!mpf_parse_int_array(json, tokens, value_idx, snap->acted, MPF_MAX_PLAYERS, err, "snapshot.acted"))
                    return 0;
                snap->acted_len = tokens[value_idx].size;
                if (snap->acted_len > MPF_MAX_PLAYERS)
                    snap->acted_len = MPF_MAX_PLAYERS;
                snap->has_acted = 1;
            }
            next_idx = mpf_json_next(tokens, value_idx);
        }
        else
        {
            next_idx = mpf_json_next(tokens, idx);
        }
        idx = next_idx;
    }
    if (snap->has_board && !snap->has_board_revealed)
    {
        snap->board_revealed = board_len;
        snap->has_board_revealed = 1;
    }
    if (snap->has_board_revealed)
    {
        if (snap->board_revealed < 0)
            snap->board_revealed = 0;
        if (snap->board_revealed > 5)
            snap->board_revealed = 5;
    }
    snap->defined = 1;
    return 1;
}

static void mpf_snapshot_init(mpf_tree_snapshot_t *snap)
{
    if (!snap)
        return;
    memset(snap, 0, sizeof(*snap));
    snap->street = MPF_STREET_PREFLOP;
    snap->num_players = 0;
    snap->to_act = -1;
    snap->first_to_act = -1;
    snap->board_revealed = 0;
    for (int i = 0; i < MPF_MAX_PLAYERS; ++i)
    {
        snap->stacks[i] = 0.0;
        snap->invested[i] = 0.0;
        snap->round_contrib[i] = 0.0;
        snap->active[i] = 1;
        snap->acted[i] = 0;
    }
    for (int i = 0; i < 5; ++i)
        snap->board_cards[i] = -1;
}

static int mpf_parse_double_array(const char *json,
                                  const jsmntok_t *tokens,
                                  int array_index,
                                  double *out,
                                  int max,
                                  mpf_tree_error_t *err,
                                  const char *label)
{
    const jsmntok_t *arr = &tokens[array_index];
    if (arr->type != JSMN_ARRAY)
    {
        if (err && label)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s must be an array", label);
            mpf_tree_error(err, buf);
        }
        return 0;
    }
    for (int i = 0; i < max; ++i)
        out[i] = 0.0;
    int token_idx = array_index + 1;
    for (int i = 0; i < arr->size; ++i)
    {
        if (i < max)
        {
            double val = 0.0;
            if (!mpf_token_to_double(json, &tokens[token_idx], &val))
            {
                if (err && label)
                {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "%s must contain numeric values", label);
                    mpf_tree_error(err, buf);
                }
                return 0;
            }
            out[i] = val;
        }
        token_idx = mpf_json_next(tokens, token_idx);
    }
    return 1;
}

static int mpf_parse_int_array(const char *json,
                               const jsmntok_t *tokens,
                               int array_index,
                               int *out,
                               int max,
                               mpf_tree_error_t *err,
                               const char *label)
{
    const jsmntok_t *arr = &tokens[array_index];
    if (arr->type != JSMN_ARRAY)
    {
        if (err && label)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s must be an array", label);
            mpf_tree_error(err, buf);
        }
        return 0;
    }
    int token_idx = array_index + 1;
    for (int i = 0; i < arr->size; ++i)
    {
        if (i < max)
        {
            int val = 0;
            if (!mpf_token_to_int(json, &tokens[token_idx], &val))
            {
                if (err && label)
                {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "%s must contain integer values", label);
                    mpf_tree_error(err, buf);
                }
                return 0;
            }
            out[i] = val;
        }
        token_idx = mpf_json_next(tokens, token_idx);
    }
    return 1;
}

static int mpf_parse_string_array(const char *json,
                                  const jsmntok_t *tokens,
                                  int array_index,
                                  char ***out,
                                  int *out_count,
                                  mpf_tree_error_t *err,
                                  const char *label)
{
    const jsmntok_t *arr = &tokens[array_index];
    if (arr->type != JSMN_ARRAY)
    {
        if (err && label)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s must be an array", label);
            mpf_tree_error(err, buf);
        }
        return 0;
    }
    char **values = NULL;
    if (arr->size > 0)
    {
        values = (char **)calloc((size_t)arr->size, sizeof(char *));
        if (!values)
        {
            if (err && label)
            {
                char buf[96];
                snprintf(buf, sizeof(buf), "allocation failure for %s", label);
                mpf_tree_error(err, buf);
            }
            return 0;
        }
    }
    int idx = array_index + 1;
    for (int i = 0; i < arr->size; ++i)
    {
        const jsmntok_t *tok = &tokens[idx];
        if (tok->type != JSMN_STRING)
        {
            if (err && label)
            {
                char buf[96];
                snprintf(buf, sizeof(buf), "%s entries must be strings", label);
                mpf_tree_error(err, buf);
            }
            if (values)
            {
                for (int j = 0; j < i; ++j)
                    free(values[j]);
                free(values);
            }
            return 0;
        }
        values[i] = mpf_strndup(json + tok->start, (size_t)(tok->end - tok->start));
        if (!values[i])
        {
            if (err && label)
            {
                char buf[96];
                snprintf(buf, sizeof(buf), "allocation failure for %s entry", label);
                mpf_tree_error(err, buf);
            }
            for (int j = 0; j < i; ++j)
                free(values[j]);
            free(values);
            return 0;
        }
        idx = mpf_json_next(tokens, idx);
    }
    if (out)
        *out = values;
    else if (values)
        free(values);
    if (out_count)
        *out_count = arr->size;
    return 1;
}

/* Parse spot-filter / action-morphing syntax out of a single combo hand
   string (FEAT-07, #143). Recognized tokens: $cb, SPR>x, SPR<x, POS=IP,
   POS=OOP, BET, AUTO. The first supported token sets the rule; an optional
   trailing hand (e.g. "SPR>3:AA") is kept as the residual `out_hand`. Returns
   1 on success (including when no spot token is present), 0 on malformed input.
   When `out_rule` is non-NULL and a spot token was found, *out_rule is filled
   and *out_found is set to 1. */
static int mpf_parse_spot_from_combo(const char *hand, size_t hand_len,
                                     mpf_tree_spot_rule_t *out_rule,
                                     int *out_found,
                                     char *out_hand, size_t out_hand_size,
                                     size_t *out_hand_len,
                                     mpf_tree_error_t *err)
{
    if (out_found)
        *out_found = 0;
    if (out_hand && out_hand_size > 0)
        out_hand[0] = '\0';

    if (!hand)
        return 0;

    size_t len = hand_len;
    const char *p = hand;

    /* $cb c-bet spot filter */
    if (len >= 3 && p[0] == '$' && tolower((unsigned char)p[1]) == 'c' &&
        tolower((unsigned char)p[2]) == 'b')
    {
        if (out_rule)
        {
            memset(out_rule, 0, sizeof(*out_rule));
            out_rule->kind = MPF_SPOT_CB;
            out_rule->is_cb = 1;
        }
        if (out_found)
            *out_found = 1;
        p += 3;
    }
    /* SPR>x / SPR<x */
    else if (len >= 4 && tolower((unsigned char)p[0]) == 's' &&
             tolower((unsigned char)p[1]) == 'p' && tolower((unsigned char)p[2]) == 'r')
    {
        char op = p[3];
        if (op != '>' && op != '<')
            return 0;
        char *endptr = NULL;
        double value = strtod(&p[4], &endptr);
        if (endptr == &p[4])
        {
            if (err)
                mpf_tree_error(err, "SPR rule missing numeric threshold");
            return 0;
        }
        if (out_rule)
        {
            memset(out_rule, 0, sizeof(*out_rule));
            out_rule->kind = (op == '>') ? MPF_SPOT_SPR_GT : MPF_SPOT_SPR_LT;
            out_rule->value = value;
        }
        if (out_found)
            *out_found = 1;
        p = endptr;
    }
    /* POS=IP / POS=OOP */
    else if (len >= 4 && tolower((unsigned char)p[0]) == 'p' &&
             tolower((unsigned char)p[1]) == 'o' && tolower((unsigned char)p[2]) == 's' &&
             p[3] == '=')
    {
        if (len >= 6 && tolower((unsigned char)p[4]) == 'i' && tolower((unsigned char)p[5]) == 'p')
        {
            if (out_rule)
            {
                memset(out_rule, 0, sizeof(*out_rule));
                out_rule->kind = MPF_SPOT_POS;
                out_rule->pos = MPF_SPOT_POS_IP;
            }
            p += 6;
        }
        else if (len >= 7 && tolower((unsigned char)p[4]) == 'o' &&
                 tolower((unsigned char)p[5]) == 'o' && tolower((unsigned char)p[6]) == 'p')
        {
            if (out_rule)
            {
                memset(out_rule, 0, sizeof(*out_rule));
                out_rule->kind = MPF_SPOT_POS;
                out_rule->pos = MPF_SPOT_POS_OOP;
            }
            p += 7;
        }
        else
        {
            if (err)
                mpf_tree_error(err, "POS rule must be IP or OOP");
            return 0;
        }
        if (out_found)
            *out_found = 1;
    }
    /* BET / AUTO standalone keywords */
    else if (len >= 3)
    {
        if (tolower((unsigned char)p[0]) == 'b' && tolower((unsigned char)p[1]) == 'e' &&
            tolower((unsigned char)p[2]) == 't')
        {
            if (out_rule)
            {
                memset(out_rule, 0, sizeof(*out_rule));
                out_rule->kind = MPF_SPOT_BET;
            }
            if (out_found)
                *out_found = 1;
            p += 3;
        }
        else if (len >= 4 && tolower((unsigned char)p[0]) == 'a' &&
                 tolower((unsigned char)p[1]) == 'u' && tolower((unsigned char)p[2]) == 't' &&
                 tolower((unsigned char)p[3]) == 'o')
        {
            if (out_rule)
            {
                memset(out_rule, 0, sizeof(*out_rule));
                out_rule->kind = MPF_SPOT_AUTO;
            }
            if (out_found)
                *out_found = 1;
            p += 4;
        }
    }

    /* Skip an optional ':' separating the spot token from a residual hand. */
    if (*p == ':')
        p++;

    if (out_hand_len)
        *out_hand_len = 0;
    if (out_hand && out_hand_size > 0 && *p != '\0')
    {
        /* Copy the residual hand using the known token length (no reliance on a
           NUL terminator): clamp to the remaining token bytes and the buffer. */
        const char *end = hand + hand_len;
        size_t avail = (size_t)(end - p);
        if ((ptrdiff_t)avail < 0)
            avail = 0;
        size_t n = avail < out_hand_size - 1 ? avail : out_hand_size - 1;
        if (n > 0)
            memcpy(out_hand, p, n);
        out_hand[n] = '\0';
        if (out_hand_len)
            *out_hand_len = n;
    }
    return 1;
}

static int mpf_parse_range_combos(const char *json,
                                  const jsmntok_t *tokens,
                                  int array_index,
                                  mpf_tree_range_profile_t *profile,
                                  mpf_tree_error_t *err)
{
    const jsmntok_t *arr = &tokens[array_index];
    if (arr->type != JSMN_ARRAY)
    {
        mpf_tree_error(err, "rangeProfile.combos must be an array");
        return 0;
    }
    if (arr->size <= 0)
    {
        profile->combos = NULL;
        profile->combo_count = 0;
        return 1;
    }
    profile->combos = (mpf_tree_range_combo_t *)calloc((size_t)arr->size, sizeof(mpf_tree_range_combo_t));
    if (!profile->combos)
    {
        mpf_tree_error(err, "allocation failure for rangeProfile combos");
        return 0;
    }
    profile->combo_count = arr->size;
    int idx = array_index + 1;
    for (int i = 0; i < arr->size; ++i)
    {
        const jsmntok_t *obj = &tokens[idx];
        if (obj->type != JSMN_OBJECT)
        {
            mpf_tree_error(err, "rangeProfile.combo entries must be objects");
            return 0;
        }
        mpf_tree_range_combo_t *combo = &profile->combos[i];
        combo->weight = 1.0;
        size_t combo_hand_len = 0;
        int prop_idx = idx + 1;
        for (int j = 0; j < obj->size; ++j)
        {
            const jsmntok_t *key = &tokens[prop_idx];
            int next_prop;
            if (key->type == JSMN_STRING)
            {
                int value_idx = prop_idx + 1;
                if (mpf_token_streq(json, key, "hand"))
                {
                    int value_idx = prop_idx + 1;
                    size_t hand_len = (size_t)(tokens[value_idx].end - tokens[value_idx].start);
                    free(combo->hand);
                    combo->hand = mpf_strndup(json + tokens[value_idx].start, hand_len);
                    if (!combo->hand)
                    {
                        mpf_tree_error(err, "allocation failure for combo hand");
                        return 0;
                    }
                    combo_hand_len = hand_len;
                }
                else if (mpf_token_streq(json, key, "weight"))
                {
                    double w = 0.0;
                    if (!mpf_token_to_double(json, &tokens[value_idx], &w))
                    {
                        mpf_tree_error(err, "combo weight must be numeric");
                        return 0;
                    }
                    combo->weight = w;
                }
                next_prop = mpf_json_next(tokens, value_idx);
            }
            else
            {
                next_prop = mpf_json_next(tokens, prop_idx);
            }
            prop_idx = next_prop;
        }

        /* Parse FEAT-07 spot-filter / action-morphing syntax from the combo. */
        mpf_tree_spot_rule_t rule;
        int found = 0;
        char residual[128];
        size_t residual_len = 0;
        if (!mpf_parse_spot_from_combo(combo->hand, combo_hand_len, &rule, &found,
                                      residual, sizeof(residual), &residual_len,
                                      err))
            return 0;

        if (found)
        {
            mpf_tree_spot_rule_t *grown = realloc(profile->spot_rules,
                                                 (size_t)(profile->spot_rule_count + 1) * sizeof(mpf_tree_spot_rule_t));
            if (!grown)
            {
                mpf_tree_error(err, "allocation failure for spot rules");
                return 0;
            }
            grown[profile->spot_rule_count++] = rule;
            profile->spot_rules = grown;

            if (rule.kind == MPF_SPOT_CB)
                profile->has_cb = 1;

            /* Replace the combo hand with the residual (the hand part after the
               spot token), if any. A pure spot token leaves an empty hand, which
               the validator tolerates for spot-only combos. */
            free(combo->hand);
            if (residual[0] != '\0')
            {
                combo->hand = mpf_strndup(residual, residual_len);
                if (!combo->hand)
                {
                    mpf_tree_error(err, "allocation failure for residual combo hand");
                    return 0;
                }
            }
            else
            {
                combo->hand = NULL;
            }
        }

        if (!combo->hand && profile->spot_rule_count == 0)
        {
            mpf_tree_error(err, "combo hand missing");
            return 0;
        }

        idx = mpf_json_next(tokens, idx);
    }
    return 1;
}

static int mpf_parse_range_profile(const char *json,
                                   const jsmntok_t *tokens,
                                   int obj_index,
                                   mpf_tree_range_profile_t *profile,
                                   mpf_tree_error_t *err)
{
    const jsmntok_t *obj = &tokens[obj_index];
    if (obj->type != JSMN_OBJECT)
    {
        mpf_tree_error(err, "rangeProfile must be an object");
        return 0;
    }
    profile->player = -1;
    profile->street = MPF_STREET_PREFLOP;
    profile->street_defined = 0;
    profile->combos = NULL;
    profile->combo_count = 0;
    profile->aliases = NULL;
    profile->alias_count = 0;

    int idx = obj_index + 1;
    for (int i = 0; i < obj->size; ++i)
    {
        const jsmntok_t *key = &tokens[idx];
        int next_idx;
        if (key->type == JSMN_STRING)
        {
            int value_idx = idx + 1;
            if (mpf_token_streq(json, key, "id"))
            {
                free(profile->id);
                profile->id = mpf_strndup(json + tokens[value_idx].start,
                                          (size_t)(tokens[value_idx].end - tokens[value_idx].start));
                if (!profile->id)
                {
                    mpf_tree_error(err, "allocation failure for rangeProfile id");
                    return 0;
                }
            }
            else if (mpf_token_streq(json, key, "player"))
            {
                if (!mpf_token_to_int(json, &tokens[value_idx], &profile->player))
                {
                    mpf_tree_error(err, "rangeProfile.player must be integer");
                    return 0;
                }
            }
            else if (mpf_token_streq(json, key, "street"))
            {
                if (!mpf_parse_street(json, &tokens[value_idx], &profile->street))
                {
                    mpf_tree_error(err, "rangeProfile.street invalid");
                    return 0;
                }
                profile->street_defined = 1;
            }
            else if (mpf_token_streq(json, key, "combos"))
            {
                if (!mpf_parse_range_combos(json, tokens, value_idx, profile, err))
                    return 0;
            }
            else if (mpf_token_streq(json, key, "aliases"))
            {
                if (!mpf_parse_string_array(json, tokens, value_idx,
                                            &profile->aliases,
                                            &profile->alias_count,
                                            err,
                                            "rangeProfile.aliases"))
                    return 0;
            }
            next_idx = mpf_json_next(tokens, value_idx);
        }
        else
        {
            next_idx = mpf_json_next(tokens, idx);
        }
        idx = next_idx;
    }
    if (!profile->id)
    {
        mpf_tree_error(err, "rangeProfile missing id");
        return 0;
    }
    return 1;
}

static int mpf_parse_range_profiles(mpf_tree_def_t *tree,
                                    const char *json,
                                    const jsmntok_t *tokens,
                                    int array_index,
                                    mpf_tree_error_t *err)
{
    const jsmntok_t *arr = &tokens[array_index];
    if (arr->type != JSMN_ARRAY)
    {
        mpf_tree_error(err, "rangeProfiles must be an array");
        return 0;
    }
    if (arr->size <= 0)
    {
        tree->range_profiles = NULL;
        tree->range_profile_count = 0;
        return 1;
    }
    tree->range_profiles = (mpf_tree_range_profile_t *)calloc((size_t)arr->size, sizeof(mpf_tree_range_profile_t));
    if (!tree->range_profiles)
    {
        mpf_tree_error(err, "allocation failure for rangeProfiles");
        return 0;
    }
    tree->range_profile_count = arr->size;
    int idx = array_index + 1;
    for (int i = 0; i < arr->size; ++i)
    {
        if (!mpf_parse_range_profile(json, tokens, idx, &tree->range_profiles[i], err))
            return 0;
        idx = mpf_json_next(tokens, idx);
    }
    return 1;
}

void mpf_tree_free(mpf_tree_def_t *tree)
{
    if (!tree)
        return;
    free(tree->root_id);
    if (tree->profiles)
    {
        for (int i = 0; i < tree->profile_count; ++i)
            mpf_tree_profile_free(&tree->profiles[i]);
        free(tree->profiles);
    }
    if (tree->range_profiles)
    {
        for (int i = 0; i < tree->range_profile_count; ++i)
            mpf_tree_range_profile_free(&tree->range_profiles[i]);
        free(tree->range_profiles);
    }
    if (tree->nodes)
    {
        for (int i = 0; i < tree->node_count; ++i)
            mpf_tree_node_free(&tree->nodes[i]);
        free(tree->nodes);
    }
    free(tree);
}

static int mpf_parse_bet_sizes(const char *json,
                               const jsmntok_t *tokens,
                               int array_index,
                               double **out_sizes,
                               int *out_count,
                               mpf_tree_error_t *err)
{
    const jsmntok_t *arr = &tokens[array_index];
    if (arr->type != JSMN_ARRAY)
    {
        mpf_tree_error(err, "bet_sizes must be an array");
        return 0;
    }
    if (arr->size > MPF_MAX_BET_SIZES)
    {
        mpf_tree_error(err, "bet_sizes exceed MPF_MAX_BET_SIZES");
        return 0;
    }
    double *values = NULL;
    if (arr->size > 0)
    {
        values = (double *)calloc((size_t)arr->size, sizeof(double));
        if (!values)
        {
            mpf_tree_error(err, "allocation failure for bet_sizes");
            return 0;
        }
    }
    int token_idx = array_index + 1;
    for (int i = 0; i < arr->size; ++i)
    {
        double val = 0.0;
        if (!mpf_token_to_double(json, &tokens[token_idx], &val))
        {
            free(values);
            mpf_tree_error(err, "bet_sizes elements must be numeric");
            return 0;
        }
        values[i] = val;
        token_idx = mpf_json_next(tokens, token_idx);
    }
    *out_sizes = values;
    *out_count = arr->size;
    return 1;
}

static int mpf_parse_profile(const char *json,
                             const jsmntok_t *tokens,
                             int obj_index,
                             mpf_tree_bet_profile_t *profile,
                             mpf_tree_error_t *err)
{
    const jsmntok_t *obj = &tokens[obj_index];
    if (obj->type != JSMN_OBJECT)
    {
        mpf_tree_error(err, "profile entry must be an object");
        return 0;
    }
    profile->id = NULL;
    profile->bet_sizes = NULL;
    profile->bet_size_count = 0;
    profile->use_pot_sizing = 0;

    int idx = obj_index + 1;
    for (int i = 0; i < obj->size; ++i)
    {
        const jsmntok_t *key = &tokens[idx];
        int next_idx;
        if (key->type == JSMN_STRING)
        {
            int value_idx = idx + 1;
            if (mpf_token_streq(json, key, "id"))
            {
                free(profile->id);
                profile->id = mpf_strndup(json + tokens[value_idx].start,
                                         (size_t)(tokens[value_idx].end - tokens[value_idx].start));
                if (!profile->id)
                {
                    mpf_tree_error(err, "allocation failure on profile id");
                    return 0;
                }
            }
            else if (mpf_token_streq(json, key, "sizes"))
            {
                free(profile->bet_sizes);
                profile->bet_sizes = NULL;
                profile->bet_size_count = 0;
                if (!mpf_parse_bet_sizes(json, tokens, value_idx,
                                         &profile->bet_sizes,
                                         &profile->bet_size_count,
                                         err))
                    return 0;
            }
            else if (mpf_token_streq(json, key, "unit"))
            {
                if (mpf_token_streq(json, &tokens[value_idx], "pot"))
                    profile->use_pot_sizing = 1;
                else
                    profile->use_pot_sizing = 0;
            }
            else if (mpf_token_streq(json, key, "pot_sizing"))
            {
                int flag = 0;
                if (!mpf_token_to_bool(json, &tokens[value_idx], &flag))
                {
                    mpf_tree_error(err, "pot_sizing must be boolean");
                    return 0;
                }
                profile->use_pot_sizing = flag ? 1 : 0;
            }
            next_idx = mpf_json_next(tokens, value_idx);
        }
        else
        {
            next_idx = mpf_json_next(tokens, idx);
        }
        idx = next_idx;
    }
    if (!profile->id)
    {
        mpf_tree_error(err, "profile missing id");
        return 0;
    }
    return 1;
}

static int mpf_parse_profiles(mpf_tree_def_t *tree,
                              const char *json,
                              const jsmntok_t *tokens,
                              int array_index,
                              mpf_tree_error_t *err)
{
    const jsmntok_t *arr = &tokens[array_index];
    if (arr->type != JSMN_ARRAY)
    {
        mpf_tree_error(err, "betProfiles must be an array");
        return 0;
    }
    if (arr->size == 0)
    {
        tree->profile_count = 0;
        tree->profiles = NULL;
        return 1;
    }
    tree->profiles = (mpf_tree_bet_profile_t *)calloc((size_t)arr->size, sizeof(mpf_tree_bet_profile_t));
    if (!tree->profiles)
    {
        mpf_tree_error(err, "allocation failure for betProfiles");
        return 0;
    }
    tree->profile_count = arr->size;
    int idx = array_index + 1;
    for (int i = 0; i < arr->size; ++i)
    {
        if (!mpf_parse_profile(json, tokens, idx, &tree->profiles[i], err))
            return 0;
        idx = mpf_json_next(tokens, idx);
    }
    return 1;
}

static mpf_tree_action_type_t mpf_parse_action_type(const char *json, const jsmntok_t *tok)
{
    size_t len = (size_t)(tok->end - tok->start);
    if (len == 0)
        return MPF_TREE_ACTION_TERMINAL;
    if (strncmp(json + tok->start, "fold", len) == 0)
        return MPF_TREE_ACTION_FOLD;
    if (strncmp(json + tok->start, "call", len) == 0 ||
        strncmp(json + tok->start, "check", len) == 0)
        return MPF_TREE_ACTION_CALL;
    if (strncmp(json + tok->start, "raise", len) == 0)
        return MPF_TREE_ACTION_RAISE;
    if (strncmp(json + tok->start, "chance", len) == 0)
        return MPF_TREE_ACTION_CHANCE;
    if (strncmp(json + tok->start, "terminal", len) == 0)
        return MPF_TREE_ACTION_TERMINAL;
    return MPF_TREE_ACTION_TERMINAL;
}

static int mpf_append_bet_size(mpf_tree_node_t *node, double value, mpf_tree_error_t *err)
{
    if (node->bet_size_count >= MPF_MAX_BET_SIZES)
    {
        mpf_tree_error(err, "node bet_sizes exceed MPF_MAX_BET_SIZES");
        return 0;
    }
    double *resized = (double *)realloc(node->bet_sizes,
                                        (size_t)(node->bet_size_count + 1) * sizeof(double));
    if (!resized)
    {
        mpf_tree_error(err, "allocation failure for node bet_sizes");
        return 0;
    }
    node->bet_sizes = resized;
    node->bet_sizes[node->bet_size_count] = value;
    node->bet_size_count += 1;
    return 1;
}

static int mpf_parse_actions(const char *json,
                             const jsmntok_t *tokens,
                             int array_index,
                             mpf_tree_node_t *node,
                             mpf_tree_error_t *err)
{
    const jsmntok_t *arr = &tokens[array_index];
    if (arr->type != JSMN_ARRAY)
    {
        mpf_tree_error(err, "actions must be an array");
        return 0;
    }
    if (arr->size > MPF_TREE_ACTION_MAX)
    {
        mpf_tree_error(err, "actions exceed MPF_TREE_ACTION_MAX");
        return 0;
    }
    if (arr->size == 0)
    {
        node->actions = NULL;
        node->action_count = 0;
        return 1;
    }
    node->actions = (mpf_tree_action_t *)calloc((size_t)arr->size, sizeof(mpf_tree_action_t));
    if (!node->actions)
    {
        mpf_tree_error(err, "allocation failure for actions");
        return 0;
    }
    node->action_count = arr->size;
    for (int i = 0; i < arr->size; ++i)
    {
        node->actions[i].type = MPF_TREE_ACTION_TERMINAL;
        node->actions[i].size_index = -1;
        node->actions[i].weight = 0.0;
        node->actions[i].next_index = -1;
        node->actions[i].next_id = NULL;
    }
    int idx = array_index + 1;
    for (int i = 0; i < arr->size; ++i)
    {
        const jsmntok_t *obj = &tokens[idx];
        if (obj->type != JSMN_OBJECT)
        {
            mpf_tree_error(err, "action entries must be objects");
            return 0;
        }
        mpf_tree_action_t *action = &node->actions[i];
        int prop_idx = idx + 1;
        for (int j = 0; j < obj->size; ++j)
        {
            const jsmntok_t *key = &tokens[prop_idx];
            int next_prop;
            if (key->type == JSMN_STRING)
            {
                int value_idx = prop_idx + 1;
                if (mpf_token_streq(json, key, "type"))
                {
                    action->type = mpf_parse_action_type(json, &tokens[value_idx]);
                }
                else if (mpf_token_streq(json, key, "next"))
                {
                    free(action->next_id);
                    action->next_id = mpf_strndup(json + tokens[value_idx].start,
                                                  (size_t)(tokens[value_idx].end - tokens[value_idx].start));
                    if (!action->next_id)
                    {
                        mpf_tree_error(err, "allocation failure for action next id");
                        return 0;
                    }
                }
                else if (mpf_token_streq(json, key, "size_index"))
                {
                    int idx_val = 0;
                    if (!mpf_token_to_int(json, &tokens[value_idx], &idx_val))
                    {
                        mpf_tree_error(err, "size_index must be integer");
                        return 0;
                    }
                    action->size_index = idx_val;
                }
                else if (mpf_token_streq(json, key, "bet_size"))
                {
                    double val = 0.0;
                    if (!mpf_token_to_double(json, &tokens[value_idx], &val))
                    {
                        mpf_tree_error(err, "bet_size must be numeric");
                        return 0;
                    }
                    if (!mpf_append_bet_size(node, val, err))
                        return 0;
                    action->size_index = node->bet_size_count - 1;
                }
                else if (mpf_token_streq(json, key, "weight"))
                {
                    double w = 0.0;
                    if (!mpf_token_to_double(json, &tokens[value_idx], &w))
                    {
                        mpf_tree_error(err, "weight must be numeric");
                        return 0;
                    }
                    action->weight = w;
                }
                next_prop = mpf_json_next(tokens, value_idx);
            }
            else
            {
                next_prop = mpf_json_next(tokens, prop_idx);
            }
            prop_idx = next_prop;
        }
        if (action->type == MPF_TREE_ACTION_RAISE && action->size_index < 0)
        {
            mpf_tree_error(err, "raise action missing size_index or bet_size");
            return 0;
        }
        idx = mpf_json_next(tokens, idx);
    }
    return 1;
}

static int mpf_parse_node(const char *json,
                          const jsmntok_t *tokens,
                          int obj_index,
                          mpf_tree_node_t *node,
                          mpf_tree_error_t *err)
{
    const jsmntok_t *obj = &tokens[obj_index];
    if (obj->type != JSMN_OBJECT)
    {
        mpf_tree_error(err, "node entry must be an object");
        return 0;
    }
    node->id = NULL;
    node->type = MPF_TREE_NODE_PLAYER;
    node->street = MPF_STREET_PREFLOP;
    node->acting_player = -1;
    node->bet_sizes = NULL;
    node->bet_size_count = 0;
    node->use_pot_sizing = -1;
    node->actions = NULL;
    node->action_count = 0;
    node->bet_profile_id = NULL;
    node->is_locked = 0;
    node->locked_strategy = NULL;
    node->locked_strategy_count = 0;
    mpf_snapshot_init(&node->snapshot);
    node->has_snapshot = 0;
    node->state_cache = NULL;
    node->state_key = 0;
    node->lock_wired = 0;
#if !defined(_WIN32)
    pthread_mutex_init(&node->cache_lock, NULL);
    for (int s = 0; s < MPF_NODE_CACHE_SLOTS; ++s)
    {
        node->cache_slots[s].owner = 0;
        node->cache_slots[s].state = NULL;
        node->cache_slots[s].lock_wired = 0;
    }
#endif

    int idx = obj_index + 1;
    for (int i = 0; i < obj->size; ++i)
    {
        const jsmntok_t *key = &tokens[idx];
        int next_idx;
        if (key->type == JSMN_STRING)
        {
            int value_idx = idx + 1;
            if (mpf_token_streq(json, key, "id"))
            {
                free(node->id);
                node->id = mpf_strndup(json + tokens[value_idx].start,
                                       (size_t)(tokens[value_idx].end - tokens[value_idx].start));
                if (!node->id)
                {
                    mpf_tree_error(err, "allocation failure for node id");
                    return 0;
                }
            }
            else if (mpf_token_streq(json, key, "type"))
            {
                size_t len = (size_t)(tokens[value_idx].end - tokens[value_idx].start);
                if (strncmp(json + tokens[value_idx].start, "player", len) == 0)
                    node->type = MPF_TREE_NODE_PLAYER;
                else if (strncmp(json + tokens[value_idx].start, "terminal", len) == 0)
                    node->type = MPF_TREE_NODE_TERMINAL;
                else if (strncmp(json + tokens[value_idx].start, "chance", len) == 0)
                    node->type = MPF_TREE_NODE_CHANCE;
                else
                {
                    mpf_tree_error(err, "unsupported node type");
                    return 0;
                }
            }
            else if (mpf_token_streq(json, key, "player"))
            {
                int val = 0;
                if (!mpf_token_to_int(json, &tokens[value_idx], &val))
                {
                    mpf_tree_error(err, "player must be integer");
                    return 0;
                }
                node->acting_player = val;
            }
            else if (mpf_token_streq(json, key, "street"))
            {
                if (!mpf_parse_street(json, &tokens[value_idx], &node->street))
                {
                    mpf_tree_error(err, "invalid street value");
                    return 0;
                }
            }
            else if (mpf_token_streq(json, key, "bet_profile"))
            {
                free(node->bet_profile_id);
                node->bet_profile_id = mpf_strndup(json + tokens[value_idx].start,
                                                   (size_t)(tokens[value_idx].end - tokens[value_idx].start));
                if (!node->bet_profile_id)
                {
                    mpf_tree_error(err, "allocation failure for bet_profile id");
                    return 0;
                }
            }
            else if (mpf_token_streq(json, key, "range_profile"))
            {
                free(node->range_profile_id);
                node->range_profile_id = mpf_strndup(json + tokens[value_idx].start,
                                                     (size_t)(tokens[value_idx].end - tokens[value_idx].start));
                if (!node->range_profile_id)
                {
                    mpf_tree_error(err, "allocation failure for range_profile id");
                    return 0;
                }
            }
            else if (mpf_token_streq(json, key, "bet_sizes"))
            {
                free(node->bet_sizes);
                node->bet_sizes = NULL;
                node->bet_size_count = 0;
                if (!mpf_parse_bet_sizes(json, tokens, value_idx,
                                         &node->bet_sizes,
                                         &node->bet_size_count,
                                         err))
                    return 0;
            }
            else if (mpf_token_streq(json, key, "pot_sizing"))
            {
                int flag = 0;
                if (!mpf_token_to_bool(json, &tokens[value_idx], &flag))
                {
                    mpf_tree_error(err, "pot_sizing must be boolean");
                    return 0;
                }
                node->use_pot_sizing = flag ? 1 : 0;
            }
            else if (mpf_token_streq(json, key, "actions"))
            {
                if (!mpf_parse_actions(json, tokens, value_idx, node, err))
                    return 0;
            }
            else if (mpf_token_streq(json, key, "snapshot"))
            {
                if (!mpf_parse_snapshot(json, tokens, value_idx, &node->snapshot, err))
                    return 0;
                node->has_snapshot = node->snapshot.defined;
            }
            else if (mpf_token_streq(json, key, "is_locked"))
            {
                int flag = 0;
                if (!mpf_token_to_bool(json, &tokens[value_idx], &flag))
                {
                    mpf_tree_error(err, "is_locked must be boolean");
                    return 0;
                }
                node->is_locked = flag ? 1 : 0;
            }
            else if (mpf_token_streq(json, key, "locked_strategy"))
            {
                const jsmntok_t *arr = &tokens[value_idx];
                if (arr->type != JSMN_ARRAY)
                {
                    mpf_tree_error(err, "locked_strategy must be an array");
                    return 0;
                }
                int count = arr->size;
                if (count <= 0 || count > MPF_TREE_ACTION_MAX)
                {
                    mpf_tree_error(err, "locked_strategy must be a non-empty array");
                    return 0;
                }
                double tmp[MPF_TREE_ACTION_MAX];
                if (!mpf_parse_double_array(json, tokens, value_idx, tmp,
                                            MPF_TREE_ACTION_MAX, err, "locked_strategy"))
                    return 0;
                free(node->locked_strategy);
                node->locked_strategy = (double *)calloc((size_t)count, sizeof(double));
                if (!node->locked_strategy)
                {
                    mpf_tree_error(err, "allocation failure for locked_strategy");
                    return 0;
                }
                for (int k = 0; k < count; ++k)
                    node->locked_strategy[k] = tmp[k];
                node->locked_strategy_count = count;
                node->is_locked = 1;
            }
            next_idx = mpf_json_next(tokens, value_idx);
        }
        else
        {
            next_idx = mpf_json_next(tokens, idx);
        }
        idx = next_idx;
    }

    if (!node->id)
    {
        mpf_tree_error(err, "node missing id");
        return 0;
    }
    if (node->type == MPF_TREE_NODE_PLAYER && node->acting_player < 0)
    {
        mpf_tree_error(err, "player node missing player index");
        return 0;
    }
    if (node->type == MPF_TREE_NODE_PLAYER && node->action_count == 0)
    {
        mpf_tree_error(err, "player node missing actions");
        return 0;
    }
    if (node->locked_strategy_count > 0 && node->locked_strategy_count != node->action_count)
    {
        mpf_tree_error(err, "locked_strategy count must match node actions");
        return 0;
    }
    return 1;
}

static int mpf_parse_nodes(mpf_tree_def_t *tree,
                           const char *json,
                           const jsmntok_t *tokens,
                           int array_index,
                           mpf_tree_error_t *err)
{
    const jsmntok_t *arr = &tokens[array_index];
    if (arr->type != JSMN_ARRAY)
    {
        mpf_tree_error(err, "nodes must be an array");
        return 0;
    }
    if (arr->size == 0)
    {
        mpf_tree_error(err, "nodes array cannot be empty");
        return 0;
    }
    tree->nodes = (mpf_tree_node_t *)calloc((size_t)arr->size, sizeof(mpf_tree_node_t));
    if (!tree->nodes)
    {
        mpf_tree_error(err, "allocation failure for nodes");
        return 0;
    }
    tree->node_count = arr->size;
    int idx = array_index + 1;
    for (int i = 0; i < arr->size; ++i)
    {
        if (!mpf_parse_node(json, tokens, idx, &tree->nodes[i], err))
            return 0;
        idx = mpf_json_next(tokens, idx);
    }
    return 1;
}

static mpf_tree_bet_profile_t *mpf_find_profile(mpf_tree_def_t *tree, const char *id)
{
    if (!tree || !id)
        return NULL;
    for (int i = 0; i < tree->profile_count; ++i)
        if (tree->profiles[i].id && strcmp(tree->profiles[i].id, id) == 0)
            return &tree->profiles[i];
    return NULL;
}

static mpf_tree_range_profile_t *mpf_find_range_profile(mpf_tree_def_t *tree, const char *id)
{
    if (!tree || !id)
        return NULL;
    for (int i = 0; i < tree->range_profile_count; ++i)
    {
        mpf_tree_range_profile_t *profile = &tree->range_profiles[i];
        if (profile->id && strcmp(profile->id, id) == 0)
            return profile;
        for (int j = 0; j < profile->alias_count; ++j)
        {
            if (profile->aliases[j] && strcmp(profile->aliases[j], id) == 0)
                return profile;
        }
    }
    return NULL;
}

static int mpf_tree_apply_profiles(mpf_tree_def_t *tree, mpf_tree_error_t *err)
{
    for (int i = 0; i < tree->node_count; ++i)
    {
        mpf_tree_node_t *node = &tree->nodes[i];
        if (node->bet_profile_id)
        {
            mpf_tree_bet_profile_t *profile = mpf_find_profile(tree, node->bet_profile_id);
            if (!profile)
            {
                mpf_tree_error(err, "node references unknown bet_profile");
                return 0;
            }
            if (node->bet_size_count == 0 && profile->bet_size_count > 0)
            {
                node->bet_sizes = (double *)calloc((size_t)profile->bet_size_count, sizeof(double));
                if (!node->bet_sizes)
                {
                    mpf_tree_error(err, "allocation failure applying bet_profile");
                    return 0;
                }
                for (int j = 0; j < profile->bet_size_count; ++j)
                    node->bet_sizes[j] = profile->bet_sizes[j];
                node->bet_size_count = profile->bet_size_count;
            }
            if (node->use_pot_sizing < 0)
                node->use_pot_sizing = profile->use_pot_sizing ? 1 : 0;
        }
    }
    return 1;
}

/* FEAT-07 spot-filter helpers (defined below, declared here so the profile
   application pass can call them). */
static double mpf_tree_compute_spr(double pot, double eff_stack);
static int mpf_tree_is_in_position(const mpf_tree_snapshot_t *snap, int acting_player);
static int mpf_tree_evaluate_spot_rules(const mpf_tree_spot_rule_t *rules, int count,
                                        double spr, int is_ip);
static const mpf_tree_range_profile_t *mpf_tree_resolve_cb_range(
    const mpf_tree_range_profile_t *profiles, int profile_count,
    int aggressor_player, mpf_street_t prev_street, const char *cb_range_id);

static int mpf_tree_apply_range_profiles(mpf_tree_def_t *tree, mpf_tree_error_t *err)
{
    for (int i = 0; i < tree->node_count; ++i)
        tree->nodes[i].range_profile = NULL;

    for (int i = 0; i < tree->node_count; ++i)
    {
        mpf_tree_node_t *node = &tree->nodes[i];
        const char *lookup = node->range_profile_id;
        if (!lookup && node->type == MPF_TREE_NODE_PLAYER)
            lookup = node->id;
        if (!lookup)
            continue;
        mpf_tree_range_profile_t *profile = mpf_find_range_profile(tree, lookup);
        if (!profile)
        {
            if (node->range_profile_id)
            {
                char buf[128];
                snprintf(buf,
                         sizeof(buf),
                         "node %s references unknown range_profile '%s'",
                         node->id ? node->id : "<unnamed>",
                         node->range_profile_id);
                mpf_tree_error(err, buf);
                return 0;
            }
            continue;
        }
        if (profile->player >= 0 && node->type == MPF_TREE_NODE_PLAYER &&
            profile->player != node->acting_player)
        {
            char buf[128];
            snprintf(buf,
                     sizeof(buf),
                     "range_profile '%s' expects player %d but node %s uses player %d",
                     profile->id ? profile->id : "<unnamed>",
                     profile->player,
                     node->id ? node->id : "<unnamed>",
                     node->acting_player);
            mpf_tree_error(err, buf);
            return 0;
        }
        if (profile->street_defined && node->street != profile->street)
        {
            char buf[128];
            snprintf(buf,
                     sizeof(buf),
                     "range_profile '%s' expects street %s but node %s uses %s",
                     profile->id ? profile->id : "<unnamed>",
                     mpf_street_to_string(profile->street),
                     node->id ? node->id : "<unnamed>",
                     mpf_street_to_string(node->street));
            mpf_tree_error(err, buf);
            return 0;
        }
        node->range_profile = profile;

        /* FEAT-07: resolve a $cb c-bet range to the previous street's
           aggressor active range. We infer the aggressor as the player who
           acted first on the previous street (first_to_act of this node's
           snapshot when available), and the previous street as the one before
           the node's own street. */
        if (profile->has_cb)
        {
            mpf_street_t prev = node->street;
            if (prev > MPF_STREET_PREFLOP)
                prev = (mpf_street_t)(prev - 1);
            int aggressor = (node->has_snapshot && node->snapshot.first_to_act >= 0)
                               ? node->snapshot.first_to_act
                               : node->acting_player;
            node->cb_range = mpf_tree_resolve_cb_range(tree->range_profiles,
                                                      tree->range_profile_count,
                                                      aggressor, prev,
                                                      profile->cb_range_id);
        }

        /* FEAT-07: evaluate the profile's spot rules against this node's
           runtime context (SPR + position). The result is cached on the node
           so the solver can skip (or morph) the range when the gate fails. */
        if (node->has_snapshot && profile->spot_rule_count > 0)
        {
            double spr = mpf_tree_compute_spr(node->snapshot.pot,
                                              node->snapshot.stacks[node->acting_player]);
            int is_ip = mpf_tree_is_in_position(&node->snapshot, node->acting_player);
            node->spot_rules_pass =
                mpf_tree_evaluate_spot_rules(profile->spot_rules,
                                             profile->spot_rule_count, spr, is_ip);
        }
        else
        {
            node->spot_rules_pass = 1;
        }
    }
    return 1;
}

/* ===== Spot Filter / Action Morphing (FEAT-07, #143) ===== */

static double mpf_tree_compute_spr(double pot, double eff_stack)
{
    if (pot <= 0.0)
        return 0.0;
    if (eff_stack < 0.0)
        eff_stack = 0.0;
    return eff_stack / pot;
}

static int mpf_tree_is_in_position(const mpf_tree_snapshot_t *snap, int acting_player)
{
    if (!snap || !snap->has_snapshot)
        return 0;
    if (acting_player < 0 || acting_player >= snap->num_players)
        return 0;
    if (snap->num_players < 2)
        return 0;

    /* The acting player is IP when they are the last active player to act in
       the betting order. With the snapshot's acted[] flags we approximate this
       as: the acting player is IP when all other active players have already
       acted this round (i.e. they close the action). */
    for (int p = 0; p < snap->num_players; ++p)
    {
        if (p == acting_player)
            continue;
        if (snap->active[p] && !snap->acted[p])
            return 0; /* someone else still has to act -> OOP */
    }
    return 1;
}

static int mpf_tree_evaluate_spot_rules(const mpf_tree_spot_rule_t *rules, int count,
                                 double spr, int is_ip)
{
    if (count <= 0 || !rules)
        return 1; /* No gating rules -> always passes */

    for (int i = 0; i < count; ++i)
    {
        const mpf_tree_spot_rule_t *r = &rules[i];
        switch (r->kind)
        {
        case MPF_SPOT_SPR_GT:
            if (!(spr > r->value))
                return 0;
            break;
        case MPF_SPOT_SPR_LT:
            if (!(spr < r->value))
                return 0;
            break;
        case MPF_SPOT_POS:
            if (r->pos == MPF_SPOT_POS_IP && !is_ip)
                return 0;
            if (r->pos == MPF_SPOT_POS_OOP && is_ip)
                return 0;
            break;
        /* $cb, BET and AUTO are applied by the tree builder, not gating. */
        case MPF_SPOT_CB:
        case MPF_SPOT_BET:
        case MPF_SPOT_AUTO:
        case MPF_SPOT_NONE:
        default:
            break;
        }
    }
    return 1;
}

static const mpf_tree_range_profile_t *mpf_tree_resolve_cb_range(
    const mpf_tree_range_profile_t *profiles, int profile_count,
    int aggressor_player, mpf_street_t prev_street, const char *cb_range_id)
{
    if (!profiles || profile_count <= 0)
        return NULL;

    /* Explicit target wins: $cb:<profile_id> */
    if (cb_range_id)
    {
        for (int i = 0; i < profile_count; ++i)
        {
            if (profiles[i].id && strcmp(profiles[i].id, cb_range_id) == 0)
                return &profiles[i];
            for (int j = 0; j < profiles[i].alias_count; ++j)
            {
                if (profiles[i].aliases[j] &&
                    strcmp(profiles[i].aliases[j], cb_range_id) == 0)
                    return &profiles[i];
            }
        }
    }

    /* Otherwise the c-bet range is the active range of the previous street's
       aggressor: the profile for `aggressor_player` on `prev_street`. */
    for (int i = 0; i < profile_count; ++i)
    {
        const mpf_tree_range_profile_t *p = &profiles[i];
        if (p->player == aggressor_player && p->street == prev_street)
            return p;
    }
    return NULL;
}

static int mpf_find_node_index(const mpf_tree_def_t *tree, const char *id)
{
    if (!tree || !id)
        return -1;
    for (int i = 0; i < tree->node_count; ++i)
        if (tree->nodes[i].id && strcmp(tree->nodes[i].id, id) == 0)
            return i;
    return -1;
}

static int mpf_tree_resolve_links(mpf_tree_def_t *tree, mpf_tree_error_t *err)
{
    for (int i = 0; i < tree->node_count; ++i)
    {
        mpf_tree_node_t *node = &tree->nodes[i];
        for (int j = 0; j < node->action_count; ++j)
        {
            mpf_tree_action_t *action = &node->actions[j];
            if (action->next_id)
            {
                int idx = mpf_find_node_index(tree, action->next_id);
                if (idx < 0)
                {
                    mpf_tree_error(err, "action references unknown next node");
                    return 0;
                }
                action->next_index = idx;
            }
            else
            {
                action->next_index = -1;
            }
        }
    }

    if (tree->root_id)
    {
        tree->root_index = mpf_find_node_index(tree, tree->root_id);
        if (tree->root_index < 0)
        {
            mpf_tree_error(err, "root node id not found in nodes");
            return 0;
        }
    }
    else
    {
        tree->root_index = 0;
    }
    return 1;
}

int mpf_tree_validate(const mpf_tree_def_t *tree, mpf_tree_error_t *err)
{
    if (!tree)
    {
        mpf_tree_error(err, "tree is NULL");
        return 0;
    }
    if (tree->node_count <= 0 || !tree->nodes)
    {
        mpf_tree_error(err, "tree contains no nodes");
        return 0;
    }
    if (tree->root_index < 0 || tree->root_index >= tree->node_count)
    {
        mpf_tree_error(err, "root index out of range");
        return 0;
    }
    for (int i = 0; i < tree->range_profile_count; ++i)
    {
        const mpf_tree_range_profile_t *profile = &tree->range_profiles[i];
        if (!profile->id || profile->id[0] == '\0')
        {
            mpf_tree_error(err, "range profile missing id");
            return 0;
        }
        if (profile->player >= MPF_MAX_PLAYERS)
        {
            mpf_tree_error(err, "range profile player index out of range");
            return 0;
        }
        for (int j = 0; j < profile->combo_count; ++j)
        {
            const mpf_tree_range_combo_t *combo = &profile->combos[j];
            /* A combo without a hand is valid when it carries a spot filter
               (e.g. "$cb" or a bare "POS=IP"); the spot rule supplies the
               range/action instead of a literal hand. */
            if ((!combo->hand || combo->hand[0] == '\0') && profile->spot_rule_count == 0)
            {
                mpf_tree_error(err, "range profile combo missing hand");
                return 0;
            }
            if (combo->weight < 0.0)
            {
                mpf_tree_error(err, "range profile combo weight must be non-negative");
                return 0;
            }
        }
    }
    for (int i = 0; i < tree->node_count; ++i)
    {
        const mpf_tree_node_t *node = &tree->nodes[i];
        if (!node->id)
        {
            mpf_tree_error(err, "node missing id");
            return 0;
        }
        if (node->type == MPF_TREE_NODE_PLAYER)
        {
            if (node->acting_player < 0)
            {
                mpf_tree_error(err, "player node missing acting player");
                return 0;
            }
            if (node->action_count <= 0)
            {
                mpf_tree_error(err, "player node has no actions");
                return 0;
            }
            for (int j = 0; j < node->action_count; ++j)
            {
                const mpf_tree_action_t *action = &node->actions[j];
                if (action->type == MPF_TREE_ACTION_RAISE)
                {
                    if (action->size_index < 0 || action->size_index >= node->bet_size_count)
                    {
                        mpf_tree_error(err, "raise action references invalid size_index");
                        return 0;
                    }
                }
            }
        }
        else if (node->type == MPF_TREE_NODE_CHANCE)
        {
            if (node->action_count != 1)
            {
                mpf_tree_error(err, "chance node must define exactly one transition");
                return 0;
            }
            for (int j = 0; j < node->action_count; ++j)
            {
                const mpf_tree_action_t *action = &node->actions[j];
                if (action->type != MPF_TREE_ACTION_CHANCE)
                {
                    mpf_tree_error(err, "chance node contains non-chance action");
                    return 0;
                }
                if (action->next_index < 0)
                {
                    mpf_tree_error(err, "chance action missing next node");
                    return 0;
                }
            }
        }
        else if (node->type == MPF_TREE_NODE_TERMINAL)
        {
            if (node->action_count > 0)
            {
                mpf_tree_error(err, "terminal node must not define actions");
                return 0;
            }
        }
    }
    return 1;
}

static int mpf_json_parse(const char *json, size_t len, mpf_json_doc_t *out_doc, mpf_tree_error_t *err)
{
    jsmn_parser parser;
    jsmn_init(&parser);
    int token_capacity = 256;
    jsmntok_t *tokens = NULL;
    int ret = JSMN_ERROR_NOMEM;
    while (ret == JSMN_ERROR_NOMEM && token_capacity <= MPF_TREE_MAX_TOKENS)
    {
        free(tokens);
        tokens = (jsmntok_t *)calloc((size_t)token_capacity, sizeof(jsmntok_t));
        if (!tokens)
        {
            mpf_tree_error(err, "allocation failure while parsing JSON");
            return 0;
        }
        jsmn_init(&parser);
        ret = jsmn_parse(&parser, json, (unsigned int)len, tokens, (unsigned int)token_capacity);
        token_capacity *= 2;
    }
    if (ret < 0)
    {
        free(tokens);
        if (err)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "json parse error (%d)", ret);
            mpf_tree_error(err, buf);
        }
        else
        {
            mpf_tree_error(err, "invalid JSON document");
        }
        return 0;
    }
    out_doc->json = json;
    out_doc->tokens = tokens;
    out_doc->count = ret;
    return 1;
}

static void mpf_json_doc_free(mpf_json_doc_t *doc)
{
    if (!doc)
        return;
    free(doc->tokens);
    doc->tokens = NULL;
    doc->count = 0;
    doc->json = NULL;
}

mpf_tree_def_t *mpf_tree_load_json(const char *json, size_t len, mpf_tree_error_t *err)
{
    if (!json || len == 0)
    {
        mpf_tree_error(err, "JSON buffer is empty");
        return NULL;
    }
    mpf_json_doc_t doc;
    if (!mpf_json_parse(json, len, &doc, err))
        return NULL;
    if (doc.count <= 0 || doc.tokens[0].type != JSMN_OBJECT)
    {
        mpf_json_doc_free(&doc);
        mpf_tree_error(err, "root JSON value must be an object");
        return NULL;
    }

    mpf_tree_def_t *tree = (mpf_tree_def_t *)calloc(1, sizeof(mpf_tree_def_t));
    if (!tree)
    {
        mpf_json_doc_free(&doc);
        mpf_tree_error(err, "allocation failure for tree");
        return NULL;
    }
    tree->version = MPF_TREE_VERSION_CURRENT;
    tree->root_index = 0;

    int nodes_found = 0;

    int idx = 1;
    for (int i = 0; i < doc.tokens[0].size; ++i)
    {
        const jsmntok_t *key = &doc.tokens[idx];
        int value_idx = idx + 1;
        if (key->type == JSMN_STRING)
        {
            if (mpf_token_streq(json, key, "version"))
            {
                int ver = 0;
                if (!mpf_token_to_int(json, &doc.tokens[value_idx], &ver))
                {
                    mpf_tree_error(err, "version must be integer");
                    goto fail;
                }
                tree->version = ver;
            }
            else if (mpf_token_streq(json, key, "root"))
            {
                free(tree->root_id);
                tree->root_id = mpf_strndup(json + doc.tokens[value_idx].start,
                                            (size_t)(doc.tokens[value_idx].end - doc.tokens[value_idx].start));
                if (!tree->root_id)
                {
                    mpf_tree_error(err, "allocation failure for root id");
                    goto fail;
                }
            }
            else if (mpf_token_streq(json, key, "betProfiles"))
            {
                if (!mpf_parse_profiles(tree, json, doc.tokens, value_idx, err))
                    goto fail;
            }
            else if (mpf_token_streq(json, key, "rangeProfiles"))
            {
                if (!mpf_parse_range_profiles(tree, json, doc.tokens, value_idx, err))
                    goto fail;
            }
            else if (mpf_token_streq(json, key, "nodes"))
            {
                nodes_found = 1;
                if (!mpf_parse_nodes(tree, json, doc.tokens, value_idx, err))
                    goto fail;
            }
        }
        idx = mpf_json_next(doc.tokens, value_idx);
    }

    if (!nodes_found)
    {
        mpf_tree_error(err, "nodes property missing");
        goto fail;
    }

    if (!tree->nodes || tree->node_count == 0)
    {
        mpf_tree_error(err, "no nodes defined in tree");
        goto fail;
    }
    if (!mpf_tree_apply_profiles(tree, err))
        goto fail;
    if (!mpf_tree_apply_range_profiles(tree, err))
        goto fail;
    if (!mpf_tree_resolve_links(tree, err))
        goto fail;
    if (!mpf_tree_validate(tree, err))
        goto fail;

    mpf_json_doc_free(&doc);
    return tree;

fail:
    mpf_json_doc_free(&doc);
    mpf_tree_free(tree);
    return NULL;
}

mpf_tree_def_t *mpf_tree_load_json_file(const char *path, mpf_tree_error_t *err)
{
    if (!path)
    {
        mpf_tree_error(err, "path is NULL");
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        mpf_tree_error(err, "unable to open tree JSON file");
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        mpf_tree_error(err, "failed to seek tree JSON file");
        return NULL;
    }
    long size = ftell(f);
    if (size < 0)
    {
        fclose(f);
        mpf_tree_error(err, "failed to determine JSON size");
        return NULL;
    }
    rewind(f);
    char *buffer = (char *)malloc((size_t)size + 1);
    if (!buffer)
    {
        fclose(f);
        mpf_tree_error(err, "allocation failure reading JSON");
        return NULL;
    }
    size_t read_bytes = fread(buffer, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size)
    {
        free(buffer);
        mpf_tree_error(err, "failed to read full JSON file");
        return NULL;
    }
    buffer[size] = '\0';
    mpf_tree_def_t *tree = mpf_tree_load_json(buffer, (size_t)size, err);
    free(buffer);
    return tree;
}

static int mpf_buf_reserve(mpf_buf_t *buf, size_t extra)
{
    if (buf->len + extra + 1 <= buf->cap)
        return 1;
    size_t new_cap = buf->cap ? buf->cap : 256;
    while (new_cap < buf->len + extra + 1)
        new_cap *= 2;
    char *resized = (char *)realloc(buf->data, new_cap);
    if (!resized)
        return 0;
    buf->data = resized;
    buf->cap = new_cap;
    return 1;
}

static int mpf_buf_append(mpf_buf_t *buf, const char *str, size_t len)
{
    if (!mpf_buf_reserve(buf, len))
        return 0;
    memcpy(buf->data + buf->len, str, len);
    buf->len += len;
    return 1;
}

static int mpf_buf_append_char(mpf_buf_t *buf, char c)
{
    if (!mpf_buf_reserve(buf, 1))
        return 0;
    buf->data[buf->len++] = c;
    return 1;
}

static int mpf_buf_append_str(mpf_buf_t *buf, const char *str)
{
    return mpf_buf_append(buf, str, strlen(str));
}

static int mpf_buf_append_int(mpf_buf_t *buf, int value)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%d", value);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return 0;
    return mpf_buf_append(buf, tmp, (size_t)n);
}

static int mpf_buf_append_double(mpf_buf_t *buf, double value)
{
    char tmp[64];
    int n = snprintf(tmp, sizeof(tmp), "%.6f", value);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return 0;
    /* Trim trailing zeros */
    while (n > 1 && tmp[n - 1] == '0' && tmp[n - 2] != '.')
        n--;
    if (n > 1 && tmp[n - 1] == '.')
        tmp[n++] = '0';
    return mpf_buf_append(buf, tmp, (size_t)n);
}

static int mpf_buf_append_quoted(mpf_buf_t *buf, const char *str)
{
    if (!str)
        str = "";
    if (!mpf_buf_append_char(buf, '\"'))
        return 0;
    for (const char *p = str; *p; ++p)
    {
        char c = *p;
        if (c == '\\' || c == '\"')
        {
            if (!mpf_buf_append(buf, "\\", 1))
                return 0;
        }
        if (!mpf_buf_append(buf, &c, 1))
            return 0;
    }
    return mpf_buf_append_char(buf, '\"');
}

static int mpf_buf_append_double_list(mpf_buf_t *buf, const double *vals, int count)
{
    if (!mpf_buf_append_char(buf, '['))
        return 0;
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            if (!mpf_buf_append(buf, ", ", 2))
                return 0;
        }
        if (!mpf_buf_append_double(buf, vals[i]))
            return 0;
    }
    return mpf_buf_append_char(buf, ']');
}

static int mpf_buf_append_int_list(mpf_buf_t *buf, const int *vals, int count)
{
    if (!mpf_buf_append_char(buf, '['))
        return 0;
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            if (!mpf_buf_append(buf, ", ", 2))
                return 0;
        }
        if (!mpf_buf_append_int(buf, vals[i]))
            return 0;
    }
    return mpf_buf_append_char(buf, ']');
}

static int mpf_buf_append_string_list(mpf_buf_t *buf, char *const *vals, int count)
{
    if (!mpf_buf_append_char(buf, '['))
        return 0;
    for (int i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            if (!mpf_buf_append(buf, ", ", 2))
                return 0;
        }
        if (!mpf_buf_append_quoted(buf, vals[i]))
            return 0;
    }
    return mpf_buf_append_char(buf, ']');
}

char *mpf_tree_serialize_json(const mpf_tree_def_t *tree, size_t *out_len)
{
    if (!tree)
        return NULL;

    mpf_buf_t buf = {0, 0, 0};
    if (!mpf_buf_append_str(&buf, "{"))
        goto fail;

    if (!mpf_buf_append_str(&buf, "\"version\":") ||
        !mpf_buf_append_int(&buf, tree->version))
        goto fail;

    if (!mpf_buf_append_str(&buf, ",\"root\":") ||
        !mpf_buf_append_quoted(&buf, tree->root_id ? tree->root_id : tree->nodes[tree->root_index].id))
        goto fail;

    if (!mpf_buf_append_str(&buf, ",\"betProfiles\":["))
        goto fail;
    for (int i = 0; i < tree->profile_count; ++i)
    {
        const mpf_tree_bet_profile_t *profile = &tree->profiles[i];
        if (!mpf_buf_append_str(&buf, (i == 0) ? "{" : ",{"))
            goto fail;
        if (!mpf_buf_append_str(&buf, "\"id\":") ||
            !mpf_buf_append_quoted(&buf, profile->id))
            goto fail;
        if (!mpf_buf_append_str(&buf, ",\"sizes\":") ||
            !mpf_buf_append_double_list(&buf, profile->bet_sizes, profile->bet_size_count))
            goto fail;
        if (!mpf_buf_append_str(&buf, ",\"pot_sizing\":") ||
            !mpf_buf_append_str(&buf, profile->use_pot_sizing ? "true" : "false"))
            goto fail;
        if (!mpf_buf_append_str(&buf, "}"))
            goto fail;
    }
    if (!mpf_buf_append_str(&buf, "]"))
        goto fail;

    if (!mpf_buf_append_str(&buf, ",\"rangeProfiles\":["))
        goto fail;
    for (int i = 0; i < tree->range_profile_count; ++i)
    {
        const mpf_tree_range_profile_t *profile = &tree->range_profiles[i];
        if (!mpf_buf_append_str(&buf, (i == 0) ? "{" : ",{"))
            goto fail;
        if (!mpf_buf_append_str(&buf, "\"id\":") ||
            !mpf_buf_append_quoted(&buf, profile->id))
            goto fail;
        if (profile->player >= 0)
        {
            if (!mpf_buf_append_str(&buf, ",\"player\":") ||
                !mpf_buf_append_int(&buf, profile->player))
                goto fail;
        }
        if (profile->street_defined)
        {
            if (!mpf_buf_append_str(&buf, ",\"street\":") ||
                !mpf_buf_append_quoted(&buf, mpf_street_to_string(profile->street)))
                goto fail;
        }
        if (profile->alias_count > 0)
        {
            if (!mpf_buf_append_str(&buf, ",\"aliases\":") ||
                !mpf_buf_append_string_list(&buf, profile->aliases, profile->alias_count))
                goto fail;
        }
        if (!mpf_buf_append_str(&buf, ",\"combos\":["))
            goto fail;
        for (int j = 0; j < profile->combo_count; ++j)
        {
            const mpf_tree_range_combo_t *combo = &profile->combos[j];
            if (!mpf_buf_append_str(&buf, (j == 0) ? "{" : ",{"))
                goto fail;
            if (!mpf_buf_append_str(&buf, "\"hand\":") ||
                !mpf_buf_append_quoted(&buf, combo->hand ? combo->hand : ""))
                goto fail;
            if (!mpf_buf_append_str(&buf, ",\"weight\":") ||
                !mpf_buf_append_double(&buf, combo->weight) ||
                !mpf_buf_append_str(&buf, "}"))
                goto fail;
        }
        if (!mpf_buf_append_str(&buf, "]"))
            goto fail;
        if (!mpf_buf_append_str(&buf, "}"))
            goto fail;
    }
    if (!mpf_buf_append_str(&buf, "]"))
        goto fail;

    if (!mpf_buf_append_str(&buf, ",\"nodes\":["))
        goto fail;
    for (int i = 0; i < tree->node_count; ++i)
    {
        const mpf_tree_node_t *node = &tree->nodes[i];
        if (!mpf_buf_append_str(&buf, (i == 0) ? "{" : ",{"))
            goto fail;
        if (!mpf_buf_append_str(&buf, "\"id\":") ||
            !mpf_buf_append_quoted(&buf, node->id))
            goto fail;
        const char *type_str = "player";
        if (node->type == MPF_TREE_NODE_CHANCE)
            type_str = "chance";
        else if (node->type == MPF_TREE_NODE_TERMINAL)
            type_str = "terminal";
        if (!mpf_buf_append_str(&buf, ",\"type\":") ||
            !mpf_buf_append_quoted(&buf, type_str))
            goto fail;
        if (!mpf_buf_append_str(&buf, ",\"street\":") ||
            !mpf_buf_append_quoted(&buf, mpf_street_to_string(node->street)))
            goto fail;
        if (node->type == MPF_TREE_NODE_PLAYER)
        {
            if (!mpf_buf_append_str(&buf, ",\"player\":") ||
                !mpf_buf_append_int(&buf, node->acting_player))
                goto fail;
        }
        if (node->bet_profile_id)
        {
            if (!mpf_buf_append_str(&buf, ",\"bet_profile\":") ||
                !mpf_buf_append_quoted(&buf, node->bet_profile_id))
                goto fail;
        }
        const char *range_id = node->range_profile_id;
        if (!range_id && node->range_profile && node->range_profile->id)
            range_id = node->range_profile->id;
        if (range_id && range_id[0])
        {
            if (!mpf_buf_append_str(&buf, ",\"range_profile\":") ||
                !mpf_buf_append_quoted(&buf, range_id))
                goto fail;
        }
        if (node->bet_size_count > 0)
        {
            if (!mpf_buf_append_str(&buf, ",\"bet_sizes\":") ||
                !mpf_buf_append_double_list(&buf, node->bet_sizes, node->bet_size_count))
                goto fail;
        }
        if (node->use_pot_sizing >= 0)
        {
            if (!mpf_buf_append_str(&buf, ",\"pot_sizing\":") ||
                !mpf_buf_append_str(&buf, node->use_pot_sizing ? "true" : "false"))
                goto fail;
        }
        if (node->is_locked)
        {
            if (!mpf_buf_append_str(&buf, ",\"is_locked\":true") ||
                !mpf_buf_append_str(&buf, ",\"locked_strategy\":") ||
                !mpf_buf_append_double_list(&buf, node->locked_strategy, node->locked_strategy_count))
                goto fail;
        }
        if (node->has_snapshot)
        {
            const mpf_tree_snapshot_t *snap = &node->snapshot;
            int snap_first = 1;
            if (!mpf_buf_append_str(&buf, ",\"snapshot\":{"))
                goto fail;
            if (snap->has_street)
            {
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"street\":") ||
                    !mpf_buf_append_quoted(&buf, mpf_street_to_string(snap->street)))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_num_players)
            {
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"num_players\":") ||
                    !mpf_buf_append_int(&buf, snap->num_players))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_to_act)
            {
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"to_act\":") ||
                    !mpf_buf_append_int(&buf, snap->to_act))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_first_to_act)
            {
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"first_to_act\":") ||
                    !mpf_buf_append_int(&buf, snap->first_to_act))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_pot)
            {
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"pot\":") ||
                    !mpf_buf_append_double(&buf, snap->pot))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_to_call)
            {
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"to_call\":") ||
                    !mpf_buf_append_double(&buf, snap->to_call))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_current_bet)
            {
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"current_bet\":") ||
                    !mpf_buf_append_double(&buf, snap->current_bet))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_raises_made)
            {
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"raises_made\":") ||
                    !mpf_buf_append_int(&buf, snap->raises_made))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_board)
            {
                int board_len = snap->board_len;
                if (board_len > 5)
                    board_len = 5;
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"board\":") ||
                    !mpf_buf_append_int_list(&buf, snap->board_cards, board_len))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_board_revealed)
            {
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"board_revealed\":") ||
                    !mpf_buf_append_int(&buf, snap->board_revealed))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_stacks)
            {
                int count = snap->stacks_len;
                if (count > MPF_MAX_PLAYERS)
                    count = MPF_MAX_PLAYERS;
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"stacks\":") ||
                    !mpf_buf_append_double_list(&buf, snap->stacks, count))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_invested)
            {
                int count = snap->invested_len;
                if (count > MPF_MAX_PLAYERS)
                    count = MPF_MAX_PLAYERS;
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"invested\":") ||
                    !mpf_buf_append_double_list(&buf, snap->invested, count))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_round_contrib)
            {
                int count = snap->round_contrib_len;
                if (count > MPF_MAX_PLAYERS)
                    count = MPF_MAX_PLAYERS;
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"round_contrib\":") ||
                    !mpf_buf_append_double_list(&buf, snap->round_contrib, count))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_active)
            {
                int count = snap->active_len;
                if (count > MPF_MAX_PLAYERS)
                    count = MPF_MAX_PLAYERS;
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"active\":") ||
                    !mpf_buf_append_int_list(&buf, snap->active, count))
                    goto fail;
                snap_first = 0;
            }
            if (snap->has_acted)
            {
                int count = snap->acted_len;
                if (count > MPF_MAX_PLAYERS)
                    count = MPF_MAX_PLAYERS;
                if (!mpf_buf_append_str(&buf, snap_first ? "" : ",") ||
                    !mpf_buf_append_str(&buf, "\"acted\":") ||
                    !mpf_buf_append_int_list(&buf, snap->acted, count))
                    goto fail;
                snap_first = 0;
            }
            if (!mpf_buf_append_str(&buf, "}"))
                goto fail;
        }
        if (node->action_count > 0)
        {
            if (!mpf_buf_append_str(&buf, ",\"actions\":["))
                goto fail;
            for (int j = 0; j < node->action_count; ++j)
            {
                const mpf_tree_action_t *action = &node->actions[j];
                if (!mpf_buf_append_str(&buf, (j == 0) ? "{" : ",{"))
                    goto fail;
                const char *atype = "terminal";
                if (action->type == MPF_TREE_ACTION_FOLD)
                    atype = "fold";
                else if (action->type == MPF_TREE_ACTION_CALL)
                    atype = "call";
                else if (action->type == MPF_TREE_ACTION_RAISE)
                    atype = "raise";
                else if (action->type == MPF_TREE_ACTION_CHANCE)
                    atype = "chance";
                if (!mpf_buf_append_str(&buf, "\"type\":") ||
                    !mpf_buf_append_quoted(&buf, atype))
                    goto fail;
                if (action->type == MPF_TREE_ACTION_RAISE)
                {
                    if (!mpf_buf_append_str(&buf, ",\"size_index\":") ||
                        !mpf_buf_append_int(&buf, action->size_index))
                        goto fail;
                }
                if (action->next_id || action->next_index >= 0)
                {
                    const char *next_label = action->next_id ? action->next_id : (tree->nodes[action->next_index].id);
                    if (!mpf_buf_append_str(&buf, ",\"next\":") ||
                        !mpf_buf_append_quoted(&buf, next_label))
                        goto fail;
                }
                if (!mpf_buf_append_str(&buf, "}"))
                    goto fail;
            }
            if (!mpf_buf_append_str(&buf, "]"))
                goto fail;
        }
        if (!mpf_buf_append_str(&buf, "}"))
            goto fail;
    }
    if (!mpf_buf_append_str(&buf, "]"))
        goto fail;

    if (!mpf_buf_append_str(&buf, "}"))
        goto fail;

    if (!mpf_buf_reserve(&buf, 1))
        goto fail;
    buf.data[buf.len] = '\0';
    if (out_len)
        *out_len = buf.len;
    return buf.data;

fail:
    free(buf.data);
    return NULL;
}
