/* Issue #258: the priority layer is domain-level and must stay backend-free.
 *
 * The issue is explicit that the prioritisation "should operate above
 * CPU/SIMD/GPU execution details" and must not "embed CUDA/HIP/Metal/OpenCL
 * specific policy logic in the domain layer". That is easy to promise and easy
 * to break later, so it is pinned here instead: this translation unit includes
 * the public header and no backend header, and it is compiled on every
 * platform. If the header ever reaches for a CUDA, Metal, HIP or OpenCL
 * header - or stops being self-contained - the failure lands here, on every
 * job, rather than in the one backend-specific job that happens to build.
 *
 * It also pins the defaults the guide quotes, because a caller that
 * zero-initialises the struct and never resolves it must still get FIFO.
 */

#include <poker_eval/solver/pe_work_priority.h>

#include <string.h>

int main(void)
{
    pe_work_priority_config_t config;
    pe_work_priority_config_t resolved;
    pe_work_priority_item_t item;
    pe_work_priority_stats_t stats;
    double score;

    memset(&config, 0, sizeof(config));
    memset(&item, 0, sizeof(item));
    memset(&stats, 0, sizeof(stats));

    /* A zero-initialised config resolves to the documented defaults. */
    if (pe_work_priority_resolve(&config, &resolved) != 0)
        return 1;
    if (resolved.policy != PE_WORK_SCHED_FIFO)
        return 1;
    if (resolved.buckets != PE_WORK_PRIORITY_DEFAULT_BUCKETS)
        return 1;
    if (resolved.min_visits != PE_WORK_PRIORITY_DEFAULT_MIN_VISITS)
        return 1;

    /* A zero-initialised item has no actions, so there is nothing to decide. */
    score = pe_work_priority_score(&resolved, &item);
    if (!(score >= 0.0 && score <= 0.0))
        return 1;

    /* An empty histogram has no percentile to report. */
    if (pe_work_priority_percentile_bucket(&stats, resolved.buckets, 0.5) != 0u)
        return 1;

    return 0;
}
