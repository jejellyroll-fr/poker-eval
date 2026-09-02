/*
 * test_monker_filters.c - MKR-06: node-filter grammar
 */

#include <poker_eval/solver/pe_monker.h>

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                         \
            fputc('\n', stderr);                                  \
            failures++;                                            \
        }                                                          \
    } while (0)

static void test_keywords(void)
{
    static const char *const keywords[] = {
        "street", "player", "pot", "spr", "invested", "committed",
        "callers", "action", "numtoact", "players", "bets", "prebets",
        "flopbets", "turnbets", "riverbets", "open", "ip", "oop", "agg",
        "ipagg", "oopagg", "pre", "flop", "turn", "river", "call", "raise",
        "fold", "check", "allin", "limit", "rake", "dead", "stack", "board",
        "prevstreet"
    };
    pe_monker_filter_t filter;
    char expression[64];
    size_t index;

    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); ++index) {
        (void)snprintf(expression, sizeof(expression), "%s=1", keywords[index]);
        CHECK(pe_monker_filter_parse(expression, &filter) == PE_MONKER_FILTER_OK,
              "keyword rejected: %s", keywords[index]);
        CHECK(filter.atom_count == 1u, "wrong atom count for %s", keywords[index]);
        pe_monker_filter_free(&filter);
    }
    CHECK(pe_monker_filter_parse("unknown=1", &filter) ==
              PE_MONKER_FILTER_ERR_UNKNOWN_KEYWORD,
          "unknown keyword was accepted");
    CHECK(pe_monker_filter_parse("", &filter) == PE_MONKER_FILTER_ERR_EMPTY,
          "empty filter was accepted");
}

static void test_operators_and_prefixes(void)
{
    pe_monker_filter_t filter;
    pe_monker_filter_status_t status;

    status = pe_monker_filter_parse("!^pre:street>=flop,(pot<3,spr=2)",
                                    &filter);
    CHECK(status == PE_MONKER_FILTER_OK,
          "valid boolean filter was rejected (%d)", status);
    CHECK(filter.atom_count == 4u && filter.atoms[0].negated != 0u &&
              filter.atoms[0].previous != 0u &&
              filter.atoms[0].operator == PE_MONKER_FILTER_OP_NONE &&
              filter.atoms[1].operator == PE_MONKER_FILTER_OP_GE &&
              filter.atoms[2].operator == PE_MONKER_FILTER_OP_LT &&
              filter.atoms[3].operator == PE_MONKER_FILTER_OP_EQ,
          "filter operators or prefixes were decoded incorrectly");
    pe_monker_filter_free(&filter);

    status = pe_monker_filter_parse("prevstreet=1", &filter);
    CHECK(status == PE_MONKER_FILTER_OK && filter.atom_count == 1u &&
              strcmp(filter.atoms[0].keyword, "street") == 0 &&
              filter.atoms[0].previous != 0u,
          "prevstreet was not lowered to ^street (%d, atoms=%zu, keyword=%s, previous=%u)",
          status, filter.atom_count,
          filter.atom_count == 0u ? "<none>" : filter.atoms[0].keyword,
          filter.atom_count == 0u ? 0u : filter.atoms[0].previous);
    pe_monker_filter_free(&filter);

    CHECK(pe_monker_filter_parse("^street=1", &filter) == PE_MONKER_FILTER_OK,
          "previous-street prefix was rejected");
    pe_monker_filter_free(&filter);
    CHECK(pe_monker_filter_parse("street=^1", &filter) ==
              PE_MONKER_FILTER_ERR_BAD_VALUE,
          "prefix on a value was accepted");
    CHECK(pe_monker_filter_parse("^!pre", &filter) ==
              PE_MONKER_FILTER_ERR_SYNTAX,
          "prefix order ^! was accepted");
}

static void test_separator_asymmetry(void)
{
    pe_monker_filter_t filter;
    CHECK(pe_monker_filter_parse("pre,", &filter) == PE_MONKER_FILTER_OK,
          "trailing OR separator was rejected");
    pe_monker_filter_free(&filter);
    CHECK(pe_monker_filter_parse("pre:", &filter) == PE_MONKER_FILTER_OK,
          "trailing AND separator was rejected");
    pe_monker_filter_free(&filter);
    CHECK(pe_monker_filter_parse(",pre", &filter) == PE_MONKER_FILTER_ERR_SYNTAX,
          "leading OR separator was accepted");
    CHECK(pe_monker_filter_parse(":pre", &filter) == PE_MONKER_FILTER_ERR_SYNTAX,
          "leading AND separator was accepted");
    CHECK(pe_monker_filter_parse("(pre,)", &filter) == PE_MONKER_FILTER_OK,
          "trailing separator in parentheses was rejected");
    pe_monker_filter_free(&filter);
}

int main(void)
{
    test_keywords();
    test_operators_and_prefixes();
    test_separator_asymmetry();
    if (failures != 0)
        return 1;
    puts("test_monker_filters: grammar and keyword validation passed");
    return 0;
}
