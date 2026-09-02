#include <poker_eval/economics/pko.h>

#include <math.h>
#include <string.h>

#include "../solver/domain/finite_double.h"

int pe_pko_calculate(const pe_pko_input_t *input, pe_pko_result_t *result)
{
    icm_asymmetric_result_t icm_result;
    int players;
    if (!input || !result || pe_icm_calculate_asymmetric(&input->icm, &icm_result) != 0 ||
        !pe_finite_double(input->bounty_multiplier) || input->bounty_multiplier < 0.0)
        return -1;
    players = input->icm.num_players;
    memset(result, 0, sizeof(*result));
    for (int player = 0; player < players; ++player) {
        result->icm_ev[player] = icm_result.ev[player];
        for (int victim = 0; victim < players; ++victim) {
            double probability = input->elimination_probability[player][victim];
            double contribution;
            if (!pe_finite_double(probability) || probability < 0.0 || probability > 1.0 ||
                (player == victim && probability > 0.0) ||
                !pe_finite_double(input->bounties[victim]) || input->bounties[victim] < 0.0) return -1;
            contribution = probability * input->bounties[victim];
            contribution *= input->bounty_multiplier;
            if (!pe_finite_double(contribution)) return -1;
            result->bounty_ev[player] += contribution;
            if (!pe_finite_double(result->bounty_ev[player])) return -1;
        }
        result->total_ev[player] = result->icm_ev[player] + result->bounty_ev[player];
        if (!pe_finite_double(result->icm_ev[player]) ||
            !pe_finite_double(result->total_ev[player])) return -1;
    }
    for (int victim = 0; victim < players; ++victim) {
        double probability_sum = 0.0;
        for (int player = 0; player < players; ++player) {
            probability_sum += input->elimination_probability[player][victim];
            if (!pe_finite_double(probability_sum)) return -1;
        }
        if (probability_sum > 1.0 + 1e-9) return -1;
    }
    return 0;
}
