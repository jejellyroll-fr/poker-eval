#include <poker_eval/engine/solvers/cfr/cfr_core.h>
#include <poker_eval/engine/solvers/cfr/mpf_compact_storage.h>
#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#define ASSERT_TRUE(cond, msg)                         \
    do                                                 \
    {                                                  \
        if (!(cond))                                   \
        {                                              \
            fprintf(stderr, "Assertion failed: %s\n", \
                    msg);                              \
            return 1;                                  \
        }                                              \
    } while (0)

#define ASSERT_NEAR(val, expected, tol, msg)                              \
    do                                                                    \
    {                                                                     \
        if (fabs((val) - (expected)) > (tol))                             \
        {                                                                 \
            fprintf(stderr, "Assertion failed: %s (got %.12f, want %.12f)\n", \
                    msg, (double)(val), (double)(expected));              \
            return 1;                                                     \
        }                                                                 \
    } while (0)

static int make_tmp_path(char *buffer, size_t len, const char *suffix)
{
    if (!buffer || len == 0)
        return -1;
#if defined(_WIN32)
    char tmp_path[MAX_PATH];
    DWORD path_len = GetTempPathA((DWORD)sizeof(tmp_path), tmp_path);
    if (path_len == 0 || path_len > sizeof(tmp_path))
        return -1;
    char base[MAX_PATH];
    if (GetTempFileNameA(tmp_path, "pes", 0, base) == 0)
        return -1;
    int written = snprintf(buffer, len, "%s%s", base, suffix);
    if (written < 0 || (size_t)written >= len)
        return -1;
    FILE *f = fopen(buffer, "wb");
    if (!f)
        return -1;
    fclose(f);
    return 0;
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    int written = snprintf(buffer, len, "%s/pesol_test_XXXXXX", tmpdir);
    if (written < 0 || (size_t)written >= len)
        return -1;
    int fd = mkstemp(buffer);
    if (fd < 0)
        return -1;
    close(fd);
    /* mkstemp chose a name in place of XXXXXX; append the extension. */
    size_t cur = strlen(buffer);
    if (cur + strlen(suffix) >= len)
        return -1;
    strcat(buffer, suffix);
    return 0;
#endif
}

int main(void)
{
    char sol_path[512];
    char tree_path[512];
    ASSERT_TRUE(make_tmp_path(sol_path, sizeof(sol_path), ".pe_sol") == 0,
                "sol tmp path");
    ASSERT_TRUE(make_tmp_path(tree_path, sizeof(tree_path), ".pe_tree") == 0,
                "tree tmp path");

    /* ---- Build a storage with a few infosets ---- */
    cfr_storage_t *storage = cfr_storage_create();
    ASSERT_TRUE(storage != NULL, "storage allocation");

    const uint64_t KEY_A = 0x1234ull;
    const uint64_t KEY_B = 0x4567ull;
    const uint64_t KEY_C = 0x99aabul;

    /* Asymmetric distribution to stress quantization. */
    double avg_a[2] = {1.0, 3.0};          /* -> 0.25 / 0.75 */
    double avg_b[3] = {0.1, 0.2, 0.7};     /* -> 0.1 / 0.2 / 0.7 */
    double avg_c[4] = {0.05, 0.15, 0.3, 0.5};
    cfr_storage_update_avg(storage, KEY_A, 2, avg_a, 1.0);
    cfr_storage_update_avg(storage, KEY_B, 3, avg_b, 1.0);
    cfr_storage_update_avg(storage, KEY_C, 4, avg_c, 1.0);

    size_t before = cfr_storage_count_infosets(storage);
    ASSERT_TRUE(before == 3, "infoset count before");

    /* ---- Save compact .pe_sol ---- */
    ASSERT_TRUE(pe_cfr_save_storage(storage, sol_path) == 0, "save storage");

    cfr_storage_destroy(storage);

    /* ---- Load compact .pe_sol (heap copy) ---- */
    cfr_storage_t *loaded = cfr_storage_create();
    ASSERT_TRUE(loaded != NULL, "loaded allocation");
    ASSERT_TRUE(pe_cfr_load_storage(loaded, sol_path) == 0, "load storage");
    ASSERT_TRUE(cfr_storage_count_infosets(loaded) == 3, "infoset count after load");

    double strat_a[2], strat_b[3], strat_c[4];
    cfr_storage_get_avg_strategy(loaded, KEY_A, 2, strat_a);
    cfr_storage_get_avg_strategy(loaded, KEY_B, 3, strat_b);
    cfr_storage_get_avg_strategy(loaded, KEY_C, 4, strat_c);

    /* Verify on-disk size is >= 4x smaller than an equivalent JSON dump
     * (acceptance criterion in issue #145). A real solved tree holds millions
     * of infosets, so the fixed 32-byte header is negligible and the ratio is
     * dominated by per-probability cost: 2 bytes (16-bit fixed point) vs ~19
     * bytes for a full-precision JSON float. We exercise that regime with a
     * bulk storage of many infosets so the comparison reflects real usage. */
    {
        cfr_storage_t *bulk = cfr_storage_create();
        ASSERT_TRUE(bulk != NULL, "bulk storage");
        /* C's block-scope const is not an integer constant expression on
         * MSVC, so use enum constants for the fixed array bound. */
        enum { BULK_N = 2000, ACTIONS = 4 };
        double probs[ACTIONS];
        long json_bytes = 0;
        for (int i = 0; i < BULK_N; ++i)
        {
            double total = 0.0;
            for (int a = 0; a < ACTIONS; ++a)
            {
                probs[a] = 0.2 + 0.05 * ((i + a) % ACTIONS);
                total += probs[a];
            }
            cfr_storage_update_avg(bulk, (uint64_t)(0x10000ull + (uint64_t)i),
                                   ACTIONS, probs, 1.0);
            /* Model a JSON dump of this infoset at full precision. */
            json_bytes += (long)strlen("{\"key\":0000000000000000,\"n\":4,\"p\":[");
            for (int a = 0; a < ACTIONS; ++a)
            {
                char tmp[32];
                int n = snprintf(tmp, sizeof(tmp), "%.17g", probs[a] / total);
                json_bytes += n;
                if (a + 1 < ACTIONS)
                    json_bytes += 1; /* comma */
            }
            json_bytes += (long)strlen("]},");
        }
        json_bytes += (long)strlen("{\"infosets\":[]}");

        char bulk_path[512];
        ASSERT_TRUE(make_tmp_path(bulk_path, sizeof(bulk_path), ".pe_sol") == 0,
                    "bulk tmp path");
        ASSERT_TRUE(pe_cfr_save_storage(bulk, bulk_path) == 0, "save bulk");

        FILE *bsz = fopen(bulk_path, "rb");
        fseek(bsz, 0, SEEK_END);
        long bulk_file = ftell(bsz);
        fclose(bsz);

        double bulk_shrink = (double)json_bytes / (double)bulk_file;
        printf("  size reduction vs JSON dump (bulk, %d infosets): %.2fx\n",
               BULK_N, bulk_shrink);
        ASSERT_TRUE(bulk_shrink >= 4.0, ">=4x size reduction vs JSON dump");
        remove(bulk_path);
        cfr_storage_destroy(bulk);
    }

    /* Quantization must keep every probability within 0.01% of the source. */
    ASSERT_NEAR(strat_a[0], 0.25, 1e-4, "strat_a[0]");
    ASSERT_NEAR(strat_a[1], 0.75, 1e-4, "strat_a[1]");
    ASSERT_NEAR(strat_b[0], 0.1, 1e-4, "strat_b[0]");
    ASSERT_NEAR(strat_b[1], 0.2, 1e-4, "strat_b[1]");
    ASSERT_NEAR(strat_b[2], 0.7, 1e-4, "strat_b[2]");
    ASSERT_NEAR(strat_c[0], 0.05, 1e-4, "strat_c[0]");
    ASSERT_NEAR(strat_c[1], 0.15, 1e-4, "strat_c[1]");
    ASSERT_NEAR(strat_c[2], 0.3, 1e-4, "strat_c[2]");
    ASSERT_NEAR(strat_c[3], 0.5, 1e-4, "strat_c[3]");

    /* Rows must still sum to 1 (distribution). */
    double sum_a = strat_a[0] + strat_a[1];
    ASSERT_NEAR(sum_a, 1.0, 1e-9, "strat_a sums to 1");

    cfr_storage_destroy(loaded);

    /* ---- Memory-mapped view ---- */
    pe_sol_mmap_t *view = NULL;
    double mmap_probs[4];
    ASSERT_TRUE(pe_sol_open_mmap(sol_path, &view) == 0, "open mmap");
    ASSERT_TRUE(view != NULL, "mmap view non-null");
    ASSERT_TRUE(pe_sol_mmap_infoset_count(view) == 3, "mmap infoset count");

    /* Locate KEY_B record by scanning the mapped view. */
    int found = -1;
    for (size_t i = 0; i < pe_sol_mmap_infoset_count(view); ++i)
    {
        uint64_t k = 0;
        ASSERT_TRUE(pe_sol_mmap_get_strategy(view, i, &k, 4, mmap_probs, NULL) == 0,
                    "mmap get strategy");
        if (k == KEY_B)
        {
            found = (int)i;
            break;
        }
    }
    ASSERT_TRUE(found >= 0, "KEY_B found in mmap view");
    ASSERT_NEAR(mmap_probs[0], 0.1, 1e-4, "mmap strat_b[0]");
    ASSERT_NEAR(mmap_probs[2], 0.7, 1e-4, "mmap strat_b[2]");

    /* Out-of-range action buffer must be rejected cleanly (ERANGE). */
    double tiny[1];
    errno = 0;
    int rc = pe_sol_mmap_get_strategy(view, (size_t)found, NULL, 1, tiny, NULL);
    ASSERT_TRUE(rc == -1 && errno == ERANGE, "mmap ERANGE on small buffer");

    pe_sol_close_mmap(view);

    /* ---- .pe_tree round-trip ---- */
    mpf_tree_def_t tree;
    memset(&tree, 0, sizeof(tree));
    tree.version = 1;
    tree.root_index = 0;
    tree.node_count = 2;
    tree.nodes = (mpf_tree_node_t *)calloc(2, sizeof(mpf_tree_node_t));
    tree.nodes[0].id = strdup("root");
    tree.nodes[0].type = MPF_TREE_NODE_PLAYER;
    tree.nodes[0].street = MPF_STREET_FLOP;
    tree.nodes[0].acting_player = 0;
    tree.nodes[0].has_snapshot = 1;
    tree.nodes[0].snapshot.defined = 1;
    tree.nodes[0].snapshot.has_street = 1;
    tree.nodes[0].snapshot.has_pot = 1;
    tree.nodes[0].snapshot.has_to_call = 1;
    tree.nodes[0].snapshot.has_board = 1;
    tree.nodes[0].snapshot.has_stacks = 1;
    tree.nodes[0].snapshot.has_invested = 1;
    tree.nodes[0].snapshot.street = MPF_STREET_FLOP;
    tree.nodes[0].snapshot.num_players = 2;
    tree.nodes[0].snapshot.to_act = 0;
    tree.nodes[0].snapshot.pot = 100.0;
    tree.nodes[0].snapshot.to_call = 10.0;
    tree.nodes[0].snapshot.current_bet = 10.0;
    tree.nodes[0].snapshot.raises_made = 2;
    tree.nodes[0].snapshot.board_len = 3;
    tree.nodes[0].snapshot.board_cards[0] = 2;
    tree.nodes[0].snapshot.board_cards[1] = 5;
    tree.nodes[0].snapshot.board_cards[2] = 13;
    tree.nodes[0].snapshot.stacks_len = 2;
    tree.nodes[0].snapshot.stacks[0] = 200.0;
    tree.nodes[0].snapshot.stacks[1] = 150.0;
    tree.nodes[0].snapshot.invested_len = 2;
    tree.nodes[0].snapshot.invested[0] = 20.0;
    tree.nodes[0].snapshot.invested[1] = 10.0;
    tree.nodes[0].action_count = 2;
    tree.nodes[0].actions = (mpf_tree_action_t *)calloc(2, sizeof(mpf_tree_action_t));
    tree.nodes[0].actions[0].type = MPF_TREE_ACTION_FOLD;
    tree.nodes[0].actions[0].next_index = 1;
    tree.nodes[0].actions[1].type = MPF_TREE_ACTION_CALL;
    tree.nodes[0].state_key = 0xDEADBEEFCAFEF00Dull;

    tree.nodes[1].id = strdup("child");
    tree.nodes[1].type = MPF_TREE_NODE_TERMINAL;
    tree.nodes[1].acting_player = -1;
    tree.nodes[1].action_count = 0;

    tree.profile_count = 1;
    tree.profiles = (mpf_tree_bet_profile_t *)calloc(1, sizeof(mpf_tree_bet_profile_t));
    tree.profiles[0].id = strdup("default");
    tree.profiles[0].bet_size_count = 2;
    tree.profiles[0].bet_sizes = (double *)malloc(2 * sizeof(double));
    tree.profiles[0].bet_sizes[0] = 0.5;
    tree.profiles[0].bet_sizes[1] = 1.0;
    tree.profiles[0].use_pot_sizing = 1;

    ASSERT_TRUE(pe_tree_save(&tree, tree_path) == 0, "save tree");

    mpf_tree_def_t *rt = pe_tree_load(tree_path);
    ASSERT_TRUE(rt != NULL, "load tree");
    ASSERT_TRUE(rt->node_count == 2, "tree node count");
    ASSERT_TRUE(rt->profile_count == 1, "tree profile count");
    ASSERT_TRUE(strcmp(rt->nodes[0].id, "root") == 0, "tree node0 id");
    ASSERT_TRUE(rt->nodes[0].type == MPF_TREE_NODE_PLAYER, "tree node0 type");
    ASSERT_TRUE(rt->nodes[0].street == MPF_STREET_FLOP, "tree node0 street");
    ASSERT_TRUE(rt->nodes[0].has_snapshot == 1, "tree node0 snapshot flag");
    /* Full snapshot round-trip (previously dropped defined / has_ flags / stacks). */
    ASSERT_TRUE(rt->nodes[0].snapshot.defined == 1, "tree node0 snapshot defined");
    ASSERT_TRUE(rt->nodes[0].snapshot.has_pot == 1, "tree node0 has_pot");
    ASSERT_TRUE(rt->nodes[0].snapshot.has_stacks == 1, "tree node0 has_stacks");
    ASSERT_TRUE(rt->nodes[0].snapshot.has_invested == 1, "tree node0 has_invested");
    ASSERT_TRUE(rt->nodes[0].snapshot.street == MPF_STREET_FLOP, "tree node0 snap street");
    ASSERT_TRUE(rt->nodes[0].snapshot.num_players == 2, "tree node0 num_players");
    ASSERT_NEAR(rt->nodes[0].snapshot.pot, 100.0, 1e-12, "tree node0 pot");
    ASSERT_NEAR(rt->nodes[0].snapshot.to_call, 10.0, 1e-12, "tree node0 to_call");
    ASSERT_NEAR(rt->nodes[0].snapshot.current_bet, 10.0, 1e-12, "tree node0 current_bet");
    ASSERT_TRUE(rt->nodes[0].snapshot.raises_made == 2, "tree node0 raises_made");
    ASSERT_TRUE(rt->nodes[0].snapshot.board_len == 3, "tree node0 board len");
    ASSERT_TRUE(rt->nodes[0].snapshot.board_cards[1] == 5, "tree node0 board card");
    ASSERT_TRUE(rt->nodes[0].snapshot.stacks_len == 2, "tree node0 stacks_len");
    ASSERT_NEAR(rt->nodes[0].snapshot.stacks[0], 200.0, 1e-12, "tree node0 stack0");
    ASSERT_NEAR(rt->nodes[0].snapshot.stacks[1], 150.0, 1e-12, "tree node0 stack1");
    ASSERT_TRUE(rt->nodes[0].snapshot.invested_len == 2, "tree node0 invested_len");
    ASSERT_NEAR(rt->nodes[0].snapshot.invested[1], 10.0, 1e-12, "tree node0 invested1");
    ASSERT_TRUE(rt->nodes[0].action_count == 2, "tree node0 action count");
    ASSERT_TRUE(rt->nodes[0].actions[0].type == MPF_TREE_ACTION_FOLD, "tree action0 type");
    ASSERT_TRUE(rt->nodes[0].actions[0].next_index == 1, "tree action0 next");
    ASSERT_TRUE(rt->nodes[0].state_key == 0xDEADBEEFCAFEF00Dull, "tree node0 state_key 64-bit");
    ASSERT_TRUE(rt->nodes[1].type == MPF_TREE_NODE_TERMINAL, "tree node1 type");
    ASSERT_TRUE(strcmp(rt->profiles[0].id, "default") == 0, "tree profile id");
    ASSERT_TRUE(rt->profiles[0].bet_size_count == 2, "tree profile bet count");
    ASSERT_NEAR(rt->profiles[0].bet_sizes[1], 1.0, 1e-12, "tree profile bet size");

    mpf_tree_free(rt);

    /* ---- Truncated .pe_sol must be rejected by the mmap loader ---- */
    {
        char trunc_path[512];
        ASSERT_TRUE(make_tmp_path(trunc_path, sizeof(trunc_path), ".pe_sol") == 0,
                    "trunc tmp path");
        /* Write a valid header but claim one infoset with a payload that is
         * missing from the file. */
        FILE *tf = fopen(trunc_path, "wb");
        ASSERT_TRUE(tf != NULL, "open trunc file");
        unsigned char hdr[32];
        memset(hdr, 0, sizeof(hdr));
        memcpy(hdr, "PESOL001", 8);
        hdr[8] = 1;  /* version */
        /* infoset_count = 1 at offset 16 */
        hdr[16] = 1;
        fwrite(hdr, 1, sizeof(hdr), tf);
        fclose(tf);
        pe_sol_mmap_t *tv = NULL;
        ASSERT_TRUE(pe_sol_open_mmap(trunc_path, &tv) == -1, "truncated mmap rejected");
        ASSERT_TRUE(tv == NULL, "truncated mmap returns no view");
        remove(trunc_path);
    }

    /* ---- Corrupt file rejection ---- */
    FILE *cf = fopen(sol_path, "r+b");
    if (cf)
    {
        fputc('X', cf);
        fclose(cf);
    }
    cfr_storage_t *bad = cfr_storage_create();
    ASSERT_TRUE(pe_cfr_load_storage(bad, sol_path) == -1, "load rejects corrupt");
    cfr_storage_destroy(bad);

    remove(sol_path);
    remove(tree_path);

    printf("Compact binary storage (.pe_sol / .pe_tree) test passed.\n");
    return 0;
}
