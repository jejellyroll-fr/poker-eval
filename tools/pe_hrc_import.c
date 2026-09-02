#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poker_eval/economics/hrc_import.h>

static int validate_terminal(const pe_hrc_tree_t *tree, int node,
                             const pe_hrc_profile_t *profile,
                             const uint16_t *path, size_t length,
                             double *out, void *user)
{
    (void)tree; (void)node; (void)profile; (void)path; (void)length; (void)user;
    memset(out, 0, sizeof(double) * PE_HRC_MAX_PLAYERS);
    return 0;
}

int main(int argc, char **argv)
{
    pe_hrc_import_t *imported;
    pe_hrc_import_error_t error = {0};
    if (argc != 2) {
        fprintf(stderr, "usage: pe-hrc-import TREE.json\n");
        return 2;
    }
    imported = calloc(1u, sizeof(*imported));
    if (!imported) {
        fprintf(stderr, "pe-hrc-import: out of memory\n");
        return 1;
    }
    if (pe_hrc_import_json_file(argv[1], validate_terminal, NULL,
                                imported, &error) != 0) {
        fprintf(stderr, "pe-hrc-import: offset %zu: %s\n",
                error.offset, error.message[0] ? error.message : "invalid document");
        free(imported);
        return 1;
    }
    printf("pe-tree/v1 players=%d nodes=%zu profiles=%zu payouts=%d\n",
           imported->config.tree.num_players, imported->config.tree.node_count,
           imported->config.max_profiles, imported->num_payouts);
    for (int p = 0; p < imported->pot_model.player_count; ++p)
        printf("player[%d] stack=%.6f ante=%.6f combos=%zu bounty=%.6f\n",
               p, imported->pot_model.stacks[p], imported->pot_model.antes[p],
               imported->config.ranges[p].count, imported->bounties[p]);
    pe_hrc_import_free(imported);
    free(imported);
    return 0;
}
