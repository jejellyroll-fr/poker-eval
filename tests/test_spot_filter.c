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

/* ---------- Tree spot rules ---------- */

static int run_tree_spot_rules(void)
{
    /* SPR computation */
    CHECK(fabs(mpf_tree_compute_spr(20.0, 60.0) - 3.0) < 1e-9, "SPR = stack/pot");
    CHECK(mpf_tree_compute_spr(0.0, 60.0) == 0.0, "SPR 0 when pot 0");

    /* Rule evaluation */
    mpf_tree_spot_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.kind = MPF_SPOT_SPR_GT;
    rule.value = 3.0;
    CHECK(mpf_tree_evaluate_spot_rules(&rule, 1, 5.0, 1) == 1, "tree SPR>3 pass");
    CHECK(mpf_tree_evaluate_spot_rules(&rule, 1, 2.0, 1) == 0, "tree SPR>3 fail");

    memset(&rule, 0, sizeof(rule));
    rule.kind = MPF_SPOT_POS;
    rule.pos = MPF_SPOT_POS_IP;
    CHECK(mpf_tree_evaluate_spot_rules(&rule, 1, 5.0, 1) == 1, "tree POS=IP pass");
    CHECK(mpf_tree_evaluate_spot_rules(&rule, 1, 5.0, 0) == 0, "tree POS=IP fail");

    /* No rules -> always pass */
    CHECK(mpf_tree_evaluate_spot_rules(NULL, 0, 0.0, 0) == 1, "no rules pass");

    printf("  tree spot rules ok\n");
    return 0;
}

static int run_tree_cb_resolution(void)
{
    static char id0[] = "preflop_agg";
    static char id1[] = "flop_ip";
    static char id2[] = "flop_oop";
    mpf_tree_range_profile_t profiles[3];
    memset(profiles, 0, sizeof(profiles));
    profiles[0].id = id0;
    profiles[0].player = 0;
    profiles[0].street = MPF_STREET_PREFLOP;
    profiles[1].id = id1;
    profiles[1].player = 0;
    profiles[1].street = MPF_STREET_FLOP;
    profiles[2].id = id2;
    profiles[2].player = 1;
    profiles[2].street = MPF_STREET_FLOP;

    /* Resolve the c-bet range to the previous street aggressor (player 0) on
       the flop -> flop_ip profile. */
    const mpf_tree_range_profile_t *r =
        mpf_tree_resolve_cb_range(profiles, 3, 0, MPF_STREET_FLOP, NULL);
    CHECK(r != NULL && r == &profiles[1], "cb resolves to aggressor flop range");

    /* Explicit target id wins. */
    const mpf_tree_range_profile_t *r2 =
        mpf_tree_resolve_cb_range(profiles, 3, 0, MPF_STREET_FLOP, "flop_oop");
    CHECK(r2 != NULL && r2 == &profiles[2], "cb explicit id wins");

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
