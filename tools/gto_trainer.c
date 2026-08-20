/* gto_trainer.c - lightweight interactive trainer for a compact .pe_sol */
#include <poker_eval/engine/solvers/cfr/mpf_compact_storage.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --solution FILE [--rounds N] [--seed N]\n", program);
}

static unsigned next_random(unsigned *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

int main(int argc, char **argv)
{
    const char *solution = NULL;
    int rounds = 10;
    unsigned seed = 1u;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--solution") == 0 && i + 1 < argc)
            solution = argv[++i];
        else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc)
            rounds = atoi(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else
        {
            usage(argv[0]);
            return 2;
        }
    }
    if (!solution || rounds <= 0)
    {
        usage(argv[0]);
        return 2;
    }

    pe_sol_mmap_t *view = NULL;
    if (pe_sol_open_mmap(solution, &view) != 0)
    {
        fprintf(stderr, "Unable to open %s: %s\n", solution, strerror(errno));
        return 1;
    }
    size_t count = pe_sol_mmap_infoset_count(view);
    if (count == 0)
    {
        fprintf(stderr, "Solution contains no infosets\n");
        pe_sol_close_mmap(view);
        return 1;
    }

    printf("GTO trainer: %zu infosets, %d rounds\n", count, rounds);
    printf("Actions are shown generically because .pe_sol stores strategy, not tree labels.\n");
    printf("Enter an action number, or q to stop.\n\n");

    int answered = 0;
    int correct = 0;
    double probability_loss = 0.0;
    unsigned random_state = seed;
    for (int round = 0; round < rounds; ++round)
    {
        size_t index = (size_t)(next_random(&random_state) % (unsigned)count);
        double probabilities[256];
        uint64_t key = 0;
        int actions = 0;
        if (pe_sol_mmap_get_strategy(view, index, &key, 256,
                                     probabilities, &actions) != 0 ||
            actions <= 0 || actions > 256)
        {
            fprintf(stderr, "Unable to read infoset %zu\n", index);
            pe_sol_close_mmap(view);
            return 1;
        }
        int best = 0;
        for (int action = 1; action < actions; ++action)
            if (probabilities[action] > probabilities[best])
                best = action;

        printf("Spot %d/%d  infoset=0x%016llx\n", round + 1, rounds,
               (unsigned long long)key);
        printf("Available actions:");
        for (int action = 0; action < actions; ++action)
            printf(" %d", action);
        printf("\nYour action: ");
        fflush(stdout);
        char answer[32];
        if (!fgets(answer, sizeof(answer), stdin) || answer[0] == 'q' || answer[0] == 'Q')
            break;
        char *end = NULL;
        long selected = strtol(answer, &end, 10);
        if (end == answer || selected < 0 || selected >= actions)
        {
            printf("Invalid action.\n\n");
            --round;
            continue;
        }
        ++answered;
        printf("Solved strategy:\n");
        for (int action = 0; action < actions; ++action)
            printf("  action %d: %.1f%%\n", action, probabilities[action] * 100.0);
        if (probabilities[selected] >= probabilities[best])
        {
            ++correct;
            printf("Correct: action %ld has the highest solved frequency.\n\n",
                   selected);
        }
        else
        {
            probability_loss += probabilities[best] - probabilities[selected];
            printf("Best action: %d (%.1f%%).\n\n", best, probabilities[best] * 100.0);
        }
    }
    printf("Session: %d answered, %d best-action answers (%.1f%%)\n",
           answered, correct, answered ? 100.0 * (double)correct / answered : 0.0);
    printf("Cumulative strategy-probability loss: %.4f\n", probability_loss);
    pe_sol_close_mmap(view);
    return 0;
}
