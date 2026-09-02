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

static int reject_fractional_value(const char *json, const char *needle,
                                   const char *replacement,
                                   pe_hrc_terminal_fn terminal_value)
{
    char mutated[2048];
    pe_hrc_import_t imported;
    pe_hrc_import_error_t error;
    const char *at = strstr(json, needle);
    size_t prefix;
    size_t suffix;
    size_t replacement_length = strlen(replacement);
    if (!at || strlen(json) >= sizeof(mutated))
        return 0;
    prefix = (size_t)(at - json);
    suffix = strlen(at + strlen(needle));
    if (prefix + replacement_length + suffix >= sizeof(mutated))
        return 0;
    memcpy(mutated, json, prefix);
    memcpy(mutated + prefix, replacement, replacement_length);
    memcpy(mutated + prefix + replacement_length, at + strlen(needle), suffix + 1u);
    if (pe_hrc_import_json(mutated, strlen(mutated), terminal_value, NULL,
                           &imported, &error) == 0) {
        pe_hrc_import_free(&imported);
        return 0;
    }
    return 1;
}

static int replace_json_value(const char *json, const char *needle,
                              const char *replacement, char *out,
                              size_t out_size)
{
    const char *at = strstr(json, needle);
    size_t prefix;
    size_t suffix;
    size_t replacement_length = strlen(replacement);
    if (!at || strlen(json) >= out_size)
        return 0;
    prefix = (size_t)(at - json);
    suffix = strlen(at + strlen(needle));
    if (prefix + replacement_length + suffix >= out_size)
        return 0;
    memcpy(out, json, prefix);
    memcpy(out + prefix, replacement, replacement_length);
    memcpy(out + prefix + replacement_length, at + strlen(needle), suffix + 1u);
    return 1;
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
    pe_hrc_pot_model_t invalid_model;
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
    invalid_model = imported.pot_model;
    invalid_model.initial_pot = -1.0;
    assert(pe_hrc_trace_pot(&imported.config.tree, &invalid_model,
                            allin_path, 1, &trace) != 0);
    invalid_model.initial_pot = NAN;
    assert(pe_hrc_trace_pot(&imported.config.tree, &invalid_model,
                            allin_path, 1, &trace) != 0);
    assert(pe_hrc_import_make_pko_input(&imported, 8, pko_outcome, NULL,
                                        &pko_input) == 0);
    assert(pe_pko_calculate_from_ranges(&pko_input, &pko_result) == 0);
    assert(fabs(pko_result.elimination_probability[0][1] - 1.0) < 1e-9);
    assert(fabs(pko_result.pko.bounty_ev[0] - 25.0) < 1e-9);
    pe_hrc_import_free(&imported);

    {
        char zero_multiplier_json[2048];
        assert(replace_json_value(json, "\"bounty_multiplier\":1",
                                  "\"bounty_multiplier\":0",
                                  zero_multiplier_json,
                                  sizeof(zero_multiplier_json)));
        assert(pe_hrc_import_json(zero_multiplier_json,
                                  strlen(zero_multiplier_json), zero_terminal,
                                  NULL, &imported, &error) == 0);
        assert(imported.has_bounty_multiplier == 1 &&
               fabs(imported.bounty_multiplier) < 1e-12);
        assert(pe_hrc_import_make_pko_input(&imported, 8, pko_outcome, NULL,
                                            &pko_input) == 0);
        assert(fabs(pko_input.base.bounty_multiplier) < 1e-12);
        pe_hrc_import_free(&imported);
        assert(reject_fractional_value(json, "\"bounty_multiplier\":1",
                                       "\"bounty_multiplier\":-1",
                                       zero_terminal));
    }

    /* The parser must honor the caller-provided byte length even when the
       input is not NUL-terminated or ends in the middle of a number. */
    assert(pe_hrc_import_json(json, strlen(json) - 1u, zero_terminal, NULL,
                              &imported, &error) != 0);
    assert(reject_fractional_value(json, "\"root\":0", "\"root\":0.5",
                                   zero_terminal));
    assert(reject_fractional_value(json, "\"iterations\":8", "\"iterations\":8.5",
                                   zero_terminal));
    assert(reject_fractional_value(json, "\"max_profiles\":8", "\"max_profiles\":8.5",
                                   zero_terminal));
    assert(reject_fractional_value(json, "\"player\":0", "\"player\":0.5",
                                   zero_terminal));
    assert(reject_fractional_value(json, "\"child\":1", "\"child\":1.5",
                                   zero_terminal));
    puts("HRC/PKO JSON import and pot trace tests passed");
    return 0;
}
