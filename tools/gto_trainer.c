/* gto_trainer.c - lightweight interactive trainer for a compact .pe_sol */
#include <poker_eval/engine/solvers/cfr/mpf_compact_storage.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct trainer_label_t
{
    uint64_t key;
    int action;
    char label[96];
    char street[24];
    char board[64];
    uint64_t next_key;
    int has_next;
} trainer_label_t;

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --solution FILE [--labels CSV] [--rounds N] [--seed N]\n"
                    "       labels CSV: key,action,label or key,street,board,action,label,next_key\n",
            program);
}

static int load_labels(const char *path, trainer_label_t **out, size_t *count)
{
    FILE *file;
    trainer_label_t *labels = NULL;
    size_t used = 0u, capacity = 0u;
    char line[256];
    *out = NULL; *count = 0u;
    if (!path) return 0;
    file = fopen(path, "r");
    if (!file) return -1;
    while (fgets(line, sizeof(line), file))
    {
        char *fields[6] = {0};
        char *field = strtok(line, ",\r\n");
        int field_count = 0;
        while (field && field_count < 6)
        {
            fields[field_count++] = field;
            field = strtok(NULL, ",\r\n");
        }
        if (field_count < 3 || strcmp(fields[0], "key") == 0) continue;
        if (used == capacity)
        {
            size_t next = capacity ? capacity * 2u : 32u;
            trainer_label_t *grown = (trainer_label_t *)realloc(labels, next * sizeof(*labels));
            if (!grown) { free(labels); fclose(file); return -1; }
            labels = grown; capacity = next;
        }
        labels[used].key = strtoull(fields[0], NULL, 0);
        if (field_count >= 5)
        {
            snprintf(labels[used].street, sizeof(labels[used].street), "%s", fields[1]);
            snprintf(labels[used].board, sizeof(labels[used].board), "%s", fields[2]);
            labels[used].action = atoi(fields[3]);
            snprintf(labels[used].label, sizeof(labels[used].label), "%s", fields[4]);
            if (field_count >= 6 && fields[5][0])
            {
                labels[used].next_key = strtoull(fields[5], NULL, 0);
                labels[used].has_next = 1;
            }
        }
        else
        {
            labels[used].action = atoi(fields[1]);
            snprintf(labels[used].label, sizeof(labels[used].label), "%s", fields[2]);
        }
        ++used;
    }
    fclose(file);
    *out = labels; *count = used;
    return 0;
}

static const char *label_for(const trainer_label_t *labels, size_t count,
                             uint64_t key, int action)
{
    for (size_t i = 0u; i < count; ++i)
        if (labels[i].key == key && labels[i].action == action)
            return labels[i].label;
    return NULL;
}

static const trainer_label_t *spot_for(const trainer_label_t *labels,
                                       size_t count, uint64_t key)
{
    for (size_t i = 0u; i < count; ++i)
        if (labels[i].key == key) return &labels[i];
    return NULL;
}

static const trainer_label_t *transition_for(const trainer_label_t *labels,
                                             size_t count, uint64_t key,
                                             int action)
{
    for (size_t i = 0u; i < count; ++i)
        if (labels[i].key == key && labels[i].action == action && labels[i].has_next)
            return &labels[i];
    return NULL;
}

static unsigned next_random(unsigned *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

int main(int argc, char **argv)
{
    const char *solution = NULL;
    const char *labels_path = NULL;
    trainer_label_t *labels = NULL;
    size_t label_count = 0u;
    int rounds = 10;
    unsigned seed = 1u;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--solution") == 0 && i + 1 < argc)
            solution = argv[++i];
        else if (strcmp(argv[i], "--labels") == 0 && i + 1 < argc)
            labels_path = argv[++i];
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
    if (load_labels(labels_path, &labels, &label_count) != 0)
    {
        fprintf(stderr, "Unable to open labels file\n");
        return 1;
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
    printf("Action labels: %s\n", label_count ? "loaded from metadata" : "numeric fallback");
    printf("Enter an action number, or q to stop.\n\n");

    int answered = 0;
    int correct = 0;
    double probability_loss = 0.0;
    unsigned random_state = seed;
    uint64_t path_key = 0u;
    for (int round = 0; round < rounds; ++round)
    {
        size_t index = SIZE_MAX;
        if (path_key != 0u)
            for (size_t candidate = 0u; candidate < count; ++candidate)
            {
                uint64_t candidate_key = 0u;
                double ignored[256];
                int ignored_actions = 0;
                if (pe_sol_mmap_get_strategy(view, candidate, &candidate_key, 256,
                                             ignored, &ignored_actions) != 0)
                    continue;
                if (candidate_key == path_key) { index = candidate; break; }
            }
        if (index == SIZE_MAX)
            index = (size_t)(next_random(&random_state) % (unsigned)count);
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

        const trainer_label_t *spot = spot_for(labels, label_count, key);
        printf("Spot %d/%d  infoset=0x%016llx", round + 1, rounds,
               (unsigned long long)key);
        if (spot && (spot->street[0] || spot->board[0]))
            printf("  street=%s board=%s", spot->street[0] ? spot->street : "?",
                   spot->board[0] ? spot->board : "?");
        putchar('\n');
        printf("Available actions:");
        for (int action = 0; action < actions; ++action)
        {
            const char *label = label_for(labels, label_count, key, action);
            if (label) printf(" %d=%s", action, label);
            else printf(" %d", action);
        }
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
        {
            const char *label = label_for(labels, label_count, key, action);
            printf("  action %d%s%s%s: %.1f%%\n", action, label ? " (" : "",
                   label ? label : "", label ? ")" : "", probabilities[action] * 100.0);
        }
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
        {
            const trainer_label_t *transition = transition_for(
                labels, label_count, key, (int)selected);
            path_key = transition ? transition->next_key : 0u;
        }
    }
    printf("Session: %d answered, %d best-action answers (%.1f%%)\n",
           answered, correct, answered ? 100.0 * (double)correct / answered : 0.0);
    printf("Cumulative strategy-probability loss: %.4f\n", probability_loss);
    pe_sol_close_mmap(view);
    free(labels);
    return 0;
}
