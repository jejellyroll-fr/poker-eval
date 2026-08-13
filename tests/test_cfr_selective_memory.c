#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <stdio.h>

int main(void)
{
    cfr_storage_t *s = cfr_storage_create();
    if (!s) return 1;
    cfr_storage_set_memory_masks(s, 1u << CFR_STREET_FLOP,
                                    1u << CFR_STREET_TURN);
    double regret[2] = {1.0, 0.0};
    double avg[2] = {1.0, 0.0};
    cfr_storage_update_regret_at_street(s, 10, 2, CFR_STREET_FLOP, regret, 1.0);
    cfr_storage_update_avg_at_street(s, 10, 2, CFR_STREET_FLOP, avg, 1.0);
    cfr_storage_update_regret_at_street(s, 11, 2, CFR_STREET_TURN, regret, 1.0);
    cfr_storage_update_avg_at_street(s, 11, 2, CFR_STREET_TURN, avg, 1.0);
    entry_t *flop = NULL, *turn = NULL;
    for (size_t i = 0; i < s->cap; ++i) if (s->tab[i].used) {
        if (s->tab[i].key == 10) flop = &s->tab[i];
        if (s->tab[i].key == 11) turn = &s->tab[i];
    }
    if (!flop || !turn || !flop->avg || turn->avg) { cfr_storage_destroy(s); return 1; }
    double p[2];
    cfr_storage_get_avg_strategy_at_street(s, 11, 2, CFR_STREET_TURN, p);
    if (p[0] < 0.99 || p[1] > 0.01) { cfr_storage_destroy(s); return 1; }
    cfr_storage_accumulate_ev_at_street(s, 10, CFR_STREET_FLOP, 3.0);
    double mean = 0.0;
    uint64_t count = 0;
    if (cfr_storage_get_ev_stats(s, 10, &mean, NULL, &count) != 0 || count != 0) { cfr_storage_destroy(s); return 1; }
    cfr_storage_accumulate_ev_at_street(s, 11, CFR_STREET_TURN, 3.0);
    if (cfr_storage_get_ev_stats(s, 11, &mean, NULL, NULL) != 0 || mean != 3.0) { cfr_storage_destroy(s); return 1; }
    cfr_storage_destroy(s);
    return 0;
}
