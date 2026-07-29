#ifndef PLO_INTEGRATION_H
#define PLO_INTEGRATION_H

#include <poker_eval/distributions/plo_nomenclature.h>
#include <poker_eval/distributions/omaha_distributions.h>
#include <poker_eval/equity/RangeEquity.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert a PLO pattern string to an OmahaHandQuery
 * @param pattern PLO pattern string (e.g., "AAxxds", "JT98r", "AsKdQhJc")
 * @param query Output OmahaHandQuery structure
 * @return 1 on success, 0 on failure
 */
int PLO_PatternToOmahaQuery(const char* pattern, OmahaHandQuery* query);

/**
 * Generate all hands matching a PLO pattern
 * @param pattern PLO pattern string
 * @param deadCards Cards to exclude
 * @param handList Output list of hands (must be initialized)
 * @return Number of hands generated, or -1 on error
 */
int PLO_GenerateHands(const char* pattern, StdDeck_CardMask deadCards, OmahaHandList* handList);

/**
 * Create a PlayerRange from a PLO pattern
 * @param pattern PLO pattern string
 * @param deadCards Cards to exclude
 * @param range Output PlayerRange (caller must free hand_masks)
 * @return 1 on success, 0 on failure
 */
int PLO_CreateRange(const char* pattern, StdDeck_CardMask deadCards, PlayerRange* range);

/**
 * Free a PlayerRange created by PLO_CreateRange
 * @param range PlayerRange to free
 */
void PLO_FreeRange(PlayerRange* range);

/**
 * Calculate equity for PLO ranges
 * @param patterns Array of PLO pattern strings, one per player
 * @param num_players Number of players
 * @param board Known community cards
 * @param dead_cards Known dead cards
 * @param nboard_cards_to_deal Number of additional board cards to deal
 * @param use_montecarlo If true, use Monte Carlo simulation
 * @param iterations Number of iterations for Monte Carlo
 * @param results Output equity results
 * @return Number of valid matchups evaluated, or -1 on error
 */
int PLO_CalculateRangeEquity(
    const char* patterns[],
    int num_players,
    StdDeck_CardMask board,
    StdDeck_CardMask dead_cards,
    int nboard_cards_to_deal,
    bool use_montecarlo,
    int iterations,
    enum_result_t* results
);

/**
 * Get all hands in a PLO category
 * @param category PLO hand category
 * @param deadCards Cards to exclude
 * @param handList Output list of hands (must be initialized)
 * @return Number of hands generated, or -1 on error
 */
int PLO_GetCategoryHands(PLOHandCategory category, StdDeck_CardMask deadCards, OmahaHandList* handList);

/**
 * Filter an OmahaHandList by PLO category
 * @param inputList Input list of hands
 * @param category PLO category to filter by
 * @param outputList Output list of filtered hands (must be initialized)
 * @return Number of hands in output list
 */
int PLO_FilterByCategory(const OmahaHandList* inputList, PLOHandCategory category, OmahaHandList* outputList);

/**
 * Get a descriptive string for a PLO range
 * @param pattern PLO pattern string
 * @param buffer Output buffer
 * @param bufsize Size of output buffer
 * @return Pointer to buffer on success, NULL on failure
 */
char* PLO_GetRangeDescription(const char* pattern, char* buffer, size_t bufsize);

#ifdef __cplusplus
}
#endif

#endif /* PLO_INTEGRATION_H */
