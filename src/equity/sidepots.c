#include <poker_eval/equity/sidepots.h>
#include <stdlib.h>
#include <math.h>

#define PE_SIDEPOT_EPSILON 1e-9

static int compare_double(const void *a, const void *b) {
    const double da = *(const double *)a;
    const double db = *(const double *)b;
    if (da < db)
        return -1;
    if (da > db)
        return 1;
    return 0;
}

int pe_calculate_sidepots(
    const double invested[],
    int num_players,
    sidepot_t* out_pots,
    int max_pots,
    double* out_total_pot
) {
    if (!invested || !out_pots || num_players <= 0 || num_players > ENUM_MAXPLAYERS || max_pots <= 0) {
        if (out_total_pot)
            *out_total_pot = 0.0;
        return 0;
    }

    double unique_levels[ENUM_MAXPLAYERS];
    int level_count = 0;

    for (int i = 0; i < num_players; ++i) {
        if (invested[i] <= PE_SIDEPOT_EPSILON)
            continue;
        int exists = 0;
        for (int j = 0; j < level_count; ++j) {
            if (fabs(unique_levels[j] - invested[i]) <= PE_SIDEPOT_EPSILON) {
                exists = 1;
                break;
            }
        }
        if (!exists) {
            unique_levels[level_count++] = invested[i];
        }
    }

    if (level_count == 0) {
        if (out_total_pot)
            *out_total_pot = 0.0;
        return 0;
    }

    qsort(unique_levels, (size_t)level_count, sizeof(double), compare_double);

    double total_pot = 0.0;
    double previous_level = 0.0;
    int pots_created = 0;

    for (int level_idx = 0; level_idx < level_count; ++level_idx) {
        double level = unique_levels[level_idx];
        double contribution = level - previous_level;
        if (contribution <= PE_SIDEPOT_EPSILON) {
            previous_level = level;
            continue;
        }

        int eligible[ENUM_MAXPLAYERS];
        int eligible_count = 0;
        for (int player = 0; player < num_players; ++player) {
            if (invested[player] >= level - PE_SIDEPOT_EPSILON) {
                eligible[eligible_count++] = player;
            }
        }

        if (eligible_count >= 2) {
            if (pots_created >= max_pots) {
                break;
            }
            double pot_amount = contribution * eligible_count;
            sidepot_t *pot = &out_pots[pots_created++];
            pot->amount = pot_amount;
            pot->num_eligible = eligible_count;
            for (int k = 0; k < eligible_count; ++k) {
                pot->eligible_players[k] = eligible[k];
            }
            total_pot += pot_amount;
        }

        previous_level = level;
    }

    if (out_total_pot)
        *out_total_pot = total_pot;

    return pots_created;
}
