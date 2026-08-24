#include <poker_eval/solver/pe_solver_cluster.h>

#include <stdio.h>

int main(void)
{
    pe_solver_shard_t a = {2u, 5u};
    uint64_t begin = 0u;
    uint64_t end = 0u;
    if (!pe_solver_shard_valid(a) || pe_solver_shard_owns(a, UINT64_C(42)) !=
            (pe_solver_shard_for_key(UINT64_C(42), 5u) == 2u) ||
        pe_solver_shard_range(a, 103u, &begin, &end) != 0 ||
        begin >= end || end > 103u)
    {
        fprintf(stderr, "solver shard contract regression failed\n");
        return 1;
    }
    return 0;
}
