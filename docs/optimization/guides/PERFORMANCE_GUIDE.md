# Performance Guide: Advanced Range Parser

This guide provides best practices for maximizing the performance of the Advanced Range Parser (ARP) in the poker-eval library. The library includes several optimizations introduced in Phase 3 (Performance Optimizations) that can significantly speed up range evaluation.

## 1. Percentage Cache

The parser includes a built-in cache for percentage-based ranges (e.g., "20%"). Parsing percentages requires iterating through hand rankings and expanding ranges, which can be expensive. The cache stores previously computed results for percentage queries.

### Usage

The cache is enabled automatically when using `ARP_GetTopPercentage()`.

```c
/* First call - computes and caches */
ARP_GetTopPercentage(0.20f, game_holdem, dead_cards, &range);

/* Subsequent calls with same parameters - returns from cache (100x+ faster) */
ARP_GetTopPercentage(0.20f, game_holdem, dead_cards, &range);
```

**Note**: The cache is only used if `dead_cards` is empty. If you provide dead cards, the result is unique to those dead cards and cannot be safely cached globally without a more complex key.

### Cache Management

- **Initialization**: The cache initializes automatically on first use. You can explicitly call `ARP_InitCache()` at startup.
- **Clearing**: Call `ARP_ClearCache()` to free all cached memory. This is useful when memory pressure is high or when shutting down.
- **Statistics**: Use `ARP_GetCacheStats()` to monitor cache usage (hits, memory).

```c
arp_cache_stats_t stats;
ARP_GetCacheStats(&stats);
printf("Cached entries: %d\n", stats.valid_entries);
```

### Best Practices

- Reuse percentage values where possible.
- If you need to apply dead cards to a percentage range repeatedly, consider fetching the generic range (no dead cards) once, caching it yourself (or using the internal cache), and then filtering dead cards from the result manually, or just accepting the slight overhead if dead cards change every hand.

## 2. Memory Optimization

The parser uses smart estimation for memory allocation. Instead of reallocating memory as ranges grow, it estimates the required capacity based on the input string tokens.

### How it works

- **Pair Ranges** (`AA-TT`): Allocates exact size (6 combos per pair).
- **Hand Ranges** (`AK-AJ`): Allocates exact size based on suited/offsuit logic.
- **Percentages**: Allocates based on a pre-calculated curve of hands vs percentage.

### Tuning

You typically don't need to tune this, but be aware that extremely complex expressions might trigger reallocations. The parser handles this automatically.

## 3. Hash Table for Duplicate Detection

When adding hands to a range, the parser must check for duplicates to avoid double-counting weights. For small ranges, a linear scan is fast. For large ranges, the parser automatically switches to a hash table.

### Performance Impact

- **Small Ranges (< 50 hands)**: Uses linear scan (CPU cache friendly).
- **Large Ranges (> 50 hands)**: Builds a hash table (O(1) lookup).

This prevents O(N^2) behavior when parsing large lists of hands or complex unions.

## 4. Utility Functions

New utility functions in Phase 4 provide efficient ways to manipulate ranges without full re-parsing.

- `ARP_CountCombinations()`: Quickly count combos.
- `ARP_CloneRange()`: Efficient deep copy.
- `ARP_IntersectRanges()`: Compute intersection of two ranges.
- `ARP_ContainsHand()`: Fast lookup (uses hash table if available).

## 5. Benchmarks

We provide a benchmark suite to measure performance:

```bash
./tests/bench_range_parsing
```

Typical results (Modern CPU):
- **Simple Range**: < 0.005 ms
- **Percentage (uncached)**: ~0.01 ms
- **Percentage (cached)**: < 0.0002 ms (50x speedup)

## Summary

To get the best performance:
1. Use `ARP_GetTopPercentage` for "Top X%" queries.
2. Avoid passing dead cards to `ARP_GetTopPercentage` if you can filter them later (to leverage caching).
3. Use `ARP_FreeRange` to prevent memory leaks.
4. Use `ARP_ClearCache` if your application runs for a long time and memory usage is a concern.
