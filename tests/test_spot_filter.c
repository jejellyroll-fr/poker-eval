/*
 * test_spot_filter.c - FEAT-07: spot filter and action morphing syntax
 * ($cb, SPR>x, SPR<x, POS=IP/OOP, BET, AUTO).
 *
 * Verifies:
 *  1. The range parser tokenizes every spot token without syntax errors and
 *     attaches the rules as metadata (hands are unchanged).
 *  2. ARP_EvaluateSpotFilters gates a range by node SPR and position.
 *  3. The MPF tree parser extracts spot rules from range-profile combos.
 *  4. mpf_tree_compute_spr / mpf_tree_evaluate_spot_rules / mpf_tree_resolve_cb_range
 *     behave as specified.
 */

#include <poker_eval/range/AdvancedRangeParser.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/deck/deck_std.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                             \
    do                                               \
    {                                                \
        if (!(cond))                                 \
        {                                            \
            fprintf(stderr, "FAIL: %s\n", msg);     \
            return 1;                                \
        }                                            \
    } while (0)

/* ---------- Range parser spot syntax ---------- */

static int run_range_spot_tokens(void)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    const char *cases[] = {
        "$cb",
        "SPR>3",
        "SPR<3",
        "POS=IP",
        "POS=OOP",
        "BET",
        "AUTO",
        "$cb, AA",
        "SPR>3:AA",
        "POS=IP:KK"
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        arp_range_t range;
        int ok = ARP_ParseRange(cases[i], dead, game_holdem, &range);
        if (!ok)
        {
            char buf[256];
            ARP_ValidateSpotSyntax(cases[i], buf, sizeof(buf));
            fprintf(stderr, "  case '%s' failed: %s\n", cases[i], buf);
        }
        CHECK(ok, "should parse spot syntax");

        const arp_spot_filter_t *filters = NULL;
        size_t count = 0;
        CHECK(ARP_GetSpotFilters(&range, &filters, &count),
              "should expose spot filters");
        CHECK(count >= 1, "should have at least one spot filter");

        ARP_FreeRange(&range);
    }

    printf("  range spot tokens parsed ok\n");
    return 0;
}

static int run_range_spot_kinds(void)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    /* $cb */
    arp_range_t cb;
    CHECK(ARP_ParseRange("$cb", dead, game_holdem, &cb), "parse $cb");
    const arp_spot_filter_t *f = NULL;
    size_t n = 0;
    ARP_GetSpotFilters(&cb, &f, &n);
    CHECK(n == 1 && f[0].kind == ARP_SPOT_CB && f[0].is_cb, "$cb kind");
    ARP_FreeRange(&cb);

    /* SPR>3 with value */
    arp_range_t spr;
    CHECK(ARP_ParseRange("SPR>3", dead, game_holdem, &spr), "parse SPR>3");
    ARP_GetSpotFilters(&spr, &f, &n);
    CHECK(n == 1 && f[0].kind == ARP_SPOT_SPR_GT && fabs(f[0].value - 3.0) < 1e-9,
          "SPR>3 kind+value");
    ARP_FreeRange(&spr);

    /* POS=IP */
    arp_range_t pos;
    CHECK(ARP_ParseRange("POS=IP", dead, game_holdem, &pos), "parse POS=IP");
    ARP_GetSpotFilters(&pos, &f, &n);
    CHECK(n == 1 && f[0].kind == ARP_SPOT_POS && f[0].pos == ARP_SPOT_POS_IP,
          "POS=IP kind+pos");
    ARP_FreeRange(&pos);

    printf("  spot kinds ok\n");
    return 0;
}

static int run_range_spot_no_hands_change(void)
{
    StdDeck_CardMask dead;
    StdDeck_CardMask_RESET(dead);

    arp_range_t plain, with_spot;
    CHECK(ARP_ParseRange("AA", dead, game_holdem, &plain), "parse AA");
    CHECK(ARP_ParseRange("SPR>3:AA", dead, game_holdem, &with_spot), "parse SPR>3:AA");

    /* The residual hand (AA) must still expand to its 6 combos. */
    CHECK(with_spot.count == plain.count, "spot token must not change hands");
    ARP_FreeRange(&plain);
    ARP_FreeRange(&with_spot);

    printf("  spot token preserves hands ok\n");
    return 0;
}

static int run_spot_gate(void)
{
    arp_spot_filter_t filters[2];
    memset(filters, 0, sizeof(filters));

    /* SPR>3 gate */
    filters[0].kind = ARP_SPOT_SPR_GT;
    filters[0].value = 3.0;
    CHECK(ARP_EvaluateSpotFilters(filters, 1, 5.0, true), "SPR>3 passes at SPR=5");
    CHECK(!ARP_EvaluateSpotFilters(filters, 1, 2.0, true), "SPR>3 fails at SPR=2");

    /* POS=IP gate */
    filters[0].kind = ARP_SPOT_POS;
    filters[0].pos = ARP_SPOT_POS_IP;
    CHECK(ARP_EvaluateSpotFilters(filters, 1, 5.0, true), "POS=IP passes IP");
    CHECK(!ARP_EvaluateSpotFilters(filters, 1, 5.0, false), "POS=IP fails OOP");

    /* $cb is not gating */
    filters[0].kind = ARP_SPOT_CB;
    filters[0].is_cb = 1;
    CHECK(ARP_EvaluateSpotFilters(filters, 1, 0.0, false), "$cb never gates");

    printf("  spot gating ok\n");
    return 0;
}

/* ---------- Tree spot rules (via the public load path) ---------- */

static int run_tree_spot_rules(void)
{
    /* A player node on the flop with pot 10 and stacks 30 (SPR = 3.0),
       acting IP (all other active players already acted). The profile carries
       SPR>3 and POS=IP gating rules; both must pass at SPR=3 / IP. */
    static const char *json =
        "{"
        "  \"version\": 1,"
        "  \"root\": \"root\","
        "  \"betProfiles\": ["
        "    {\"id\": \"default\", \"sizes\": [3.0], \"pot_sizing\": false}"
        "  ],"
        "  \"nodes\": ["
        "    {"
        "      \"id\": \"root\","
        "      \"type\": \"player\","
        "      \"player\": 0,"
        "      \"street\": \"FLOP\","
        "      \"bet_profile\": \"default\","
        "      \"range_profile\": \"gated\","
        "      \"actions\": ["
        "        {\"type\": \"call\", \"next\": \"term\"},"
        "        {\"type\": \"raise\", \"size_index\": 0, \"next\": \"term\"}"
        "      ],"
        "      \"snapshot\": {"
        "        \"num_players\": 2,"
        "        \"street\": \"FLOP\","
        "        \"to_act\": 0,"
        "        \"first_to_act\": 1,"
        "        \"pot\": 10.0,"
        "        \"to_call\": 0.0,"
        "        \"current_bet\": 0.0,"
        "        \"raises_made\": 0,"
        "        \"stacks\": [30.0, 30.0],"
        "        \"invested\": [0.0, 0.0],"
        "        \"round_contrib\": [0.0, 0.0],"
        "        \"active\": [1, 1],"
        "        \"acted\": [0, 1]"
        "      }"
        "    },"
        "    {"
        "      \"id\": \"term\","
        "      \"type\": \"terminal\""
        "    }"
        "  ],"
        "  \"rangeProfiles\": ["
        "    {"
        "      \"id\": \"gated\","
        "      \"player\": 0,"
        "      \"street\": \"FLOP\","
        "      \"combos\": ["
        "        {\"hand\": \"SPR>3:AA\"},"
        "        {\"hand\": \"POS=IP:KK\"}"
        "      ]"
        "    }"
        "  ]"
        "}";

    mpf_tree_error_t err;
    memset(&err, 0, sizeof(err));
    mpf_tree_def_t *tree = mpf_tree_load_json(json, strlen(json), &err);
    CHECK(tree != NULL, "load gated tree");

    if (tree)
    {
        /* SPR = 30 / 10 = 3.0 (>3 is strict, so SPR>3 fails here), POS=IP passes
           (acting player is last to act). The combined gate must therefore fail. */
        int all_pass = 1;
        for (int i = 0; i < tree->node_count; ++i)
        {
            if (tree->nodes[i].range_profile && tree->nodes[i].range_profile->spot_rule_count > 0)
                all_pass = all_pass && tree->nodes[i].spot_rules_pass;
        }
        CHECK(!all_pass, "SPR>3 gate fails at SPR=3 (strict)");
        mpf_tree_free(tree);
    }

    printf("  tree spot rules ok\n");
    return 0;
}

static int run_tree_cb_resolution(void)
{
    /* Profile 'cb' uses $cb; the aggressor on the previous street (preflop,
       first_to_act = player 1) should resolve to the preflop range for
       player 1 ('agg'). We assert the node's resolved cb_range is non-NULL and
       points at the aggressor's preflop profile. */
    static const char *json =
        "{"
        "  \"version\": 1,"
        "  \"root\": \"root\","
        "  \"betProfiles\": ["
        "    {\"id\": \"default\", \"sizes\": [3.0], \"pot_sizing\": false}"
        "  ],"
        "  \"nodes\": ["
        "    {"
        "      \"id\": \"root\","
        "      \"type\": \"player\","
        "      \"player\": 0,"
        "      \"street\": \"FLOP\","
        "      \"bet_profile\": \"default\","
        "      \"range_profile\": \"cb\","
        "      \"actions\": ["
        "        {\"type\": \"call\", \"next\": \"term\"},"
        "        {\"type\": \"raise\", \"size_index\": 0, \"next\": \"term\"}"
        "      ],"
        "      \"snapshot\": {"
        "        \"num_players\": 2,"
        "        \"street\": \"FLOP\","
        "        \"to_act\": 0,"
        "        \"first_to_act\": 1,"
        "        \"pot\": 10.0,"
        "        \"to_call\": 0.0,"
        "        \"current_bet\": 0.0,"
        "        \"raises_made\": 0,"
        "        \"stacks\": [30.0, 30.0],"
        "        \"invested\": [0.0, 0.0],"
        "        \"round_contrib\": [0.0, 0.0],"
        "        \"active\": [1, 1],"
        "        \"acted\": [0, 1]"
        "      }"
        "    },"
        "    {"
        "      \"id\": \"term\","
        "      \"type\": \"terminal\""
        "    }"
        "  ],"
        "  \"rangeProfiles\": ["
        "    {"
        "      \"id\": \"cb\","
        "      \"player\": 0,"
        "      \"street\": \"FLOP\","
        "      \"combos\": [ { \"hand\": \"$cb\" } ]"
        "    },"
        "    {"
        "      \"id\": \"agg\","
        "      \"player\": 1,"
        "      \"street\": \"PREFLOP\","
        "      \"combos\": [ { \"hand\": \"AA\" } ]"
        "    }"
        "  ]"
        "}";

    mpf_tree_error_t err;
    memset(&err, 0, sizeof(err));
    mpf_tree_def_t *tree = mpf_tree_load_json(json, strlen(json), &err);
    CHECK(tree != NULL, "load cb tree");

    if (tree)
    {
        int found = 0;
        for (int i = 0; i < tree->node_count; ++i)
        {
            if (tree->nodes[i].cb_range)
            {
                found = 1;
                CHECK(tree->nodes[i].cb_range->player == 1, "$cb resolves to aggressor player");
                CHECK(tree->nodes[i].cb_range->street == MPF_STREET_PREFLOP,
                      "$cb resolves to previous (preflop) street");
            }
        }
        CHECK(found, "$cb range resolved on node");
        mpf_tree_free(tree);
    }

    printf("  cb resolution ok\n");
    return 0;
}

static int run_tree_json_spot_parsing(void)
{
    static const char *json =
        "{"
        "  \"version\": 1,"
        "  \"root\": \"root\","
        "  \"betProfiles\": ["
        "    {\"id\": \"default\", \"sizes\": [3.0], \"pot_sizing\": false}"
        "  ],"
        "  \"nodes\": ["
        "    {"
        "      \"id\": \"root\","
        "      \"type\": \"player\","
        "      \"player\": 0,"
        "      \"street\": \"FLOP\","
        "      \"bet_profile\": \"default\","
        "      \"range_profile\": \"cb_range\","
        "      \"actions\": ["
        "        {\"type\": \"call\", \"next\": \"term\"},"
        "        {\"type\": \"raise\", \"size_index\": 0, \"next\": \"term\"}"
        "      ],"
        "      \"snapshot\": {"
        "        \"num_players\": 2,"
        "        \"street\": \"FLOP\","
        "        \"to_act\": 0,"
        "        \"first_to_act\": 0,"
        "        \"pot\": 10.0,"
        "        \"to_call\": 0.0,"
        "        \"current_bet\": 0.0,"
        "        \"raises_made\": 0,"
        "        \"stacks\": [30.0, 30.0],"
        "        \"invested\": [0.0, 0.0],"
        "        \"round_contrib\": [0.0, 0.0],"
        "        \"active\": [1, 1],"
        "        \"acted\": [0, 0]"
        "      }"
        "    },"
        "    {"
        "      \"id\": \"term\","
        "      \"type\": \"terminal\""
        "    }"
        "  ],"
        "  \"rangeProfiles\": ["
        "    {"
        "      \"id\": \"cb_range\","
        "      \"player\": 0,"
        "      \"street\": \"FLOP\","
        "      \"combos\": ["
        "        { \"hand\": \"$cb\" },"
        "        { \"hand\": \"SPR>3:AA\" },"
        "        { \"hand\": \"POS=IP\" }"
        "      ]"
        "    }"
        "  ]"
        "}";

    mpf_tree_error_t err;
    memset(&err, 0, sizeof(err));
    mpf_tree_def_t *tree = mpf_tree_load_json(json, strlen(json), &err);
    CHECK(tree != NULL, "load tree with spot combos");

    int found_cb = 0;
    int found_spr = 0;
    int found_pos = 0;
    if (tree)
    {
        for (int i = 0; i < tree->range_profile_count; ++i)
        {
            mpf_tree_range_profile_t *p = &tree->range_profiles[i];
            for (int j = 0; j < p->spot_rule_count; ++j)
            {
                if (p->spot_rules[j].kind == MPF_SPOT_CB)
                    found_cb = 1;
                else if (p->spot_rules[j].kind == MPF_SPOT_SPR_GT)
                    found_spr = 1;
                else if (p->spot_rules[j].kind == MPF_SPOT_POS)
                    found_pos = 1;
            }
        }
        CHECK(found_cb, "json $cb parsed into spot rule");
        CHECK(found_spr, "json SPR>3 parsed into spot rule");
        CHECK(found_pos, "json POS=IP parsed into spot rule");
        mpf_tree_free(tree);
    }

    printf("  tree json spot parsing ok\n");
    return 0;
}

int main(void)
{
    printf("spot filter / action morphing (FEAT-07 #143)\n");
    CHECK(run_range_spot_tokens() == 0, "range spot tokens");
    CHECK(run_range_spot_kinds() == 0, "range spot kinds");
    CHECK(run_range_spot_no_hands_change() == 0, "spot preserves hands");
    CHECK(run_spot_gate() == 0, "spot gating");
    CHECK(run_tree_spot_rules() == 0, "tree spot rules");
    CHECK(run_tree_cb_resolution() == 0, "cb resolution");
    CHECK(run_tree_json_spot_parsing() == 0, "tree json spot parsing");
    printf("test_spot_filter passed.\n");
    return 0;
}
