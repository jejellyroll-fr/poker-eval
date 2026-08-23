/*
 * test_range_ppt.c - MKR-07: PPT Omaha range counts
 */

#include <poker_eval/solver/pe_monker.h>

#include <stdint.h>
#include <stdio.h>

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

static void expect_count(const char *expression, uint64_t expected)
{
    uint64_t count = 0u;
    pe_monker_ppt_status_t status = pe_monker_ppt_count(expression, &count);
    CHECK(status == PE_MONKER_PPT_OK && count == expected,
          "%s: status=%d count=%llu expected=%llu", expression, status,
          (unsigned long long)count, (unsigned long long)expected);
}

int main(void)
{
    expect_count("*", 270725u);
    expect_count("A", 76145u);
    expect_count("AA", 6961u);
    expect_count("AAA", 193u);
    expect_count("AAAA", 1u);
    expect_count("AK", 17316u);
    expect_count("AAKK", 36u);
    expect_count("AKQJ", 256u);
    expect_count("AA,KK", 13886u);
    expect_count("AA:KK", 36u);
    expect_count("AA!KK", 6925u);
    expect_count("s", 188474u);
    expect_count("ss", 69667u);
    expect_count("ds", 121173u);
    expect_count("xxxx", 2860u);
    expect_count("rr", 87685u);
    expect_count("AKs", 4894u);
    expect_count("!AA", 0u);

    {
        uint64_t count;
        CHECK(pe_monker_ppt_count("AKQJT", &count) ==
                  PE_MONKER_PPT_ERR_TOO_MANY_CARDS,
              "five-card PPT definition was accepted");
        CHECK(pe_monker_ppt_count("", &count) == PE_MONKER_PPT_ERR_EMPTY,
              "empty PPT definition was accepted");
    }
    if (failures != 0)
        return 1;
    puts("test_range_ppt: Omaha PPT counts passed");
    return 0;
}
