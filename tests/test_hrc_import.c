#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/economics/hrc_import.h>

static int zero_terminal(const pe_hrc_tree_t *tree, int node,
                         const pe_hrc_profile_t *profile,
                         const uint16_t *path, size_t path_length,
                         double *out, void *user)
{
    (void)tree; (void)node; (void)profile; (void)path; (void)path_length; (void)user;
    memset(out, 0, sizeof(double) * PE_HRC_MAX_PLAYERS);
    return 0;
}

static int pko_outcome(const pe_pko_range_profile_t *profile, int players,
                       double out[ICM_MAX_PLAYERS][ICM_MAX_PLAYERS], void *user)
{
    (void)profile; (void)user;
    memset(out, 0, sizeof(double) * ICM_MAX_PLAYERS * ICM_MAX_PLAYERS);
    if (players != 2) return -1;
    out[0][1] = 1.0;
    return 0;
}

int main(void)
{
    const char *json =
        "{\"schema\":\"pe-pko/v1\",\"variant\":\"holdem\",\"root\":0,"
        "\"initial_pot\":2,\"iterations\":8,\"max_profiles\":8,"
        "\"payouts\":[70,30],\"bounty_multiplier\":1,"
        "\"players\":[{\"stack\":100,\"ante\":1,\"range\":\"AhKh\",\"bounty\":0},"
        "{\"stack\":50,\"ante\":1,\"range\":\"QsQc\",\"bounty\":25}],"
        "\"nodes\":[{\"player\":0,\"actions\":["
        "{\"label\":\"fold\",\"amount\":0,\"child\":1},"
        "{\"label\":\"allin\",\"amount\":99,\"child\":2}]},"
        "{\"terminal\":true},{\"terminal\":true}]}";
    pe_hrc_import_t imported;
    pe_hrc_import_error_t error;
    pe_hrc_pot_trace_t trace;
    pe_pko_range_input_t pko_input;
    pe_pko_range_result_t pko_result;
    uint16_t allin_path[] = {1};
    assert(pe_hrc_import_json(json, strlen(json), zero_terminal, NULL,
                              &imported, &error) == 0);
    assert(imported.config.tree.num_players == 2);
    assert(imported.config.ranges[0].count == 1);
    assert(imported.config.tree.nodes[0].actions[1].amount == 99.0);
    assert(pe_hrc_trace_pot(&imported.config.tree, &imported.pot_model,
                            allin_path, 1, &trace) == 0);
    assert(trace.slice_count == 2);
    assert(fabs(trace.slices[0].amount - 4.0) < 1e-9);
    assert(fabs(trace.slices[1].amount - 99.0) < 1e-9);
    assert(trace.slices[0].eligible_mask == 3);
    assert(trace.slices[1].eligible_mask == 1);
    assert(pe_hrc_import_make_pko_input(&imported, 8, pko_outcome, NULL,
                                        &pko_input) == 0);
    assert(pe_pko_calculate_from_ranges(&pko_input, &pko_result) == 0);
    assert(fabs(pko_result.elimination_probability[0][1] - 1.0) < 1e-9);
    assert(fabs(pko_result.pko.bounty_ev[0] - 25.0) < 1e-9);
    pe_hrc_import_free(&imported);
    puts("HRC/PKO JSON import and pot trace tests passed");
    return 0;
}
