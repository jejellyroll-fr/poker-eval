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
    char runout[64];
    char position[32];
    double pot;
    int has_pot;
    uint64_t next_key;
    int has_next;
} trainer_label_t;

typedef struct
{
    uint64_t key;
    int selected;
    int best;
    double selected_probability;
    double best_probability;
} trainer_event_t;

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --solution FILE [--labels CSV] [--rounds N] [--seed N]"
                    " [--session-json FILE] [--resume-session FILE] [--export-html FILE]\n"
                    "       labels CSV: key,action,label or key,street,board,action,label,next_key\n"
                    "       rich labels: key,street,board,runout,position,pot,action,label,next_key\n",
            program);
}

/* Read the small scalar summary without depending on a JSON library.  The
 * session writer owns the schema and emits these fields as JSON numbers, so a
 * bounded field lookup is sufficient and keeps the trainer portable. */
static int load_session_summary(const char *path, int *answered,
                                int *best_answers, double *probability_loss)
{
    FILE *file;
    long size;
    char *data;
    const char *fields[] = {"\"answered\":", "\"best_answers\":",
                            "\"probability_loss\":"};
    double parsed[3];
    if (!path || !answered || !best_answers || !probability_loss)
        return -1;
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0)
    {
        if (file) fclose(file);
        return -1;
    }
    size = ftell(file);
    if (size < 0 || size > 16 * 1024 * 1024)
    {
        fclose(file);
        return -1;
    }
    rewind(file);
    data = (char *)malloc((size_t)size + 1u);
    if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size)
    {
        free(data); fclose(file); return -1;
    }
    fclose(file);
    data[size] = '\0';
    for (size_t i = 0u; i < 3u; ++i)
    {
        const char *at = strstr(data, fields[i]);
        char *end = NULL;
        if (!at) { free(data); return -1; }
        parsed[i] = strtod(at + strlen(fields[i]), &end);
        if (end == at + strlen(fields[i])) { free(data); return -1; }
    }
    *answered = parsed[0] < 0.0 ? 0 : (int)parsed[0];
    *best_answers = parsed[1] < 0.0 ? 0 : (int)parsed[1];
    *probability_loss = parsed[2] >= 0.0 ? parsed[2] : 0.0;
    free(data);
    return 0;
}

static void json_string(FILE *file, const char *value)
{
    const unsigned char *p = (const unsigned char *)value;
    fputc('"', file);
    while (*p != 0u)
    {
        if (*p == '"' || *p == '\\') { fputc('\\', file); fputc((int)*p, file); }
        else if (*p == '\n') fputs("\\n", file);
        else if (*p == '\r') fputs("\\r", file);
        else if (*p == '\t') fputs("\\t", file);
        else if (*p < 32u) fputc(' ', file);
        else fputc((int)*p, file);
        ++p;
    }
    fputc('"', file);
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
        char *fields[10] = {0};
        char *field = strtok(line, ",\r\n");
        int field_count = 0;
        while (field && field_count < 10)
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
        memset(&labels[used], 0, sizeof(labels[used]));
        labels[used].key = strtoull(fields[0], NULL, 0);
        if (field_count >= 5)
        {
            if (field_count >= 9)
            {
                snprintf(labels[used].street, sizeof(labels[used].street), "%s", fields[1]);
                snprintf(labels[used].board, sizeof(labels[used].board), "%s", fields[2]);
                snprintf(labels[used].runout, sizeof(labels[used].runout), "%s", fields[3]);
                snprintf(labels[used].position, sizeof(labels[used].position), "%s", fields[4]);
                labels[used].pot = strtod(fields[5], NULL);
                labels[used].has_pot = 1;
                labels[used].action = atoi(fields[6]);
                snprintf(labels[used].label, sizeof(labels[used].label), "%s", fields[7]);
                if (fields[8][0])
                {
                    labels[used].next_key = strtoull(fields[8], NULL, 0);
                    labels[used].has_next = 1;
                }
            }
            else
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

static int write_session(const char *path, const char *solution,
                         unsigned seed, int rounds, int answered, int correct,
                         double probability_loss, int difficulty,
                         const char *resumed_from,
                         const trainer_event_t *events, size_t event_count)
{
    FILE *file = fopen(path, "w");
    if (!file) return -1;
    fprintf(file, "{\"schema\":\"pe-trainer-session/v1\",\"solution\":\"");
    for (const char *p = solution; *p; ++p)
    {
        if (*p == '"' || *p == '\\') fputc('\\', file);
        fputc(*p, file);
    }
    fprintf(file, "\",\"seed\":%u,\"rounds\":%d,\"answered\":%d,"
                  "\"best_answers\":%d,\"probability_loss\":%.17g,"
                  "\"difficulty\":%d,\"resumed_from\":",
            seed, rounds, answered, correct, probability_loss, difficulty);
    json_string(file, resumed_from ? resumed_from : "");
    fputs(",\"events\":[", file);
    for (size_t i = 0u; i < event_count; ++i)
    {
        if (i) fputc(',', file);
        fprintf(file, "{\"key\":\"0x%016llx\",\"selected\":%d,\"best\":%d,"
                      "\"selected_probability\":%.17g,\"best_probability\":%.17g}",
                (unsigned long long)events[i].key, events[i].selected,
                events[i].best, events[i].selected_probability,
                events[i].best_probability);
    }
    fputs("]}\n", file);
    return fclose(file) == 0 ? 0 : -1;
}

/* Generates a self-contained local browser trainer.  It is intentionally
 * static: opening the file never uploads solution data or needs a server. */
static int write_html_app(const char *path, pe_sol_mmap_t *view,
                          const trainer_label_t *labels, size_t label_count)
{
    FILE *file = fopen(path, "w");
    const size_t count = pe_sol_mmap_infoset_count(view);
    if (!file) return -1;
    fputs("<!doctype html><html><head><meta charset='utf-8'><title>poker-eval trainer</title>"
          "<style>body{font:16px system-ui;max-width:900px;margin:2rem auto;padding:0 1rem;"
          "background:#10131a;color:#eef}button{margin:.35rem;padding:.7rem 1rem;border:0;"
          "border-radius:.4rem;background:#385b9b;color:white;cursor:pointer}.card{background:#191e29;"
          "padding:1rem;border-radius:.6rem;margin:1rem 0}.muted{color:#9aa}</style></head><body>"
          "<h1>poker-eval trainer</h1><p class='muted'>Local adaptive play-vs-solution session</p>"
          "<div class='card'><div id='spot'></div><div id='actions'></div><div id='feedback'></div></div>"
          "<p>Score: <span id='score'>0</span> / <span id='seen'>0</span> · Difficulty: "
          "<span id='difficulty'>1</span></p><button onclick='nextSpot()'>Next spot</button>"
          "<script>const spots=[", file);
    for (size_t i = 0u; i < count; ++i)
    {
        uint64_t key = 0u;
        double probabilities[256];
        int actions = 0;
        if (pe_sol_mmap_get_strategy(view, i, &key, 256, probabilities, &actions) != 0) continue;
        if (i != 0u) fputc(',', file);
        fprintf(file, "{key:\"0x%016llx\",actions:[", (unsigned long long)key);
        for (int action = 0; action < actions; ++action)
        {
            const trainer_label_t *label = NULL;
            for (size_t j = 0u; j < label_count; ++j)
                if (labels[j].key == key && labels[j].action == action) { label = &labels[j]; break; }
            if (action != 0) fputc(',', file);
            fprintf(file, "{n:%d,p:%g,label:", action, probabilities[action]);
            json_string(file, label && label->label[0] ? label->label : "action");
            fputs(",next:", file);
            if (label && label->has_next) fprintf(file, "\"0x%016llx\"", (unsigned long long)label->next_key);
            else fputs("null", file);
            fputc('}', file);
        }
        fputs("]", file);
        const trainer_label_t *spot = spot_for(labels, label_count, key);
        if (spot)
        {
            fputs(",street:", file); json_string(file, spot->street);
            fputs(",board:", file); json_string(file, spot->board);
            fputs(",runout:", file); json_string(file, spot->runout);
            fputs(",position:", file); json_string(file, spot->position);
        }
        fputc('}', file);
    }
    fputs("];let current=null,score=0,seen=0,streak=0,difficulty=1;"
          "function choose(){let pool=spots;if(difficulty>1){let h=spots.filter(s=>s.actions.some(a=>a.p<.5));"
          "if(h.length)pool=h;}return pool[Math.floor(Math.random()*pool.length)];}"
          "function nextSpot(){current=choose();render();}function render(){if(!current)return;"
          "let m=[current.street,current.board,current.runout,current.position].filter(Boolean).join(' · ');"
          "document.getElementById('spot').innerHTML='<b>'+current.key+'</b> '+m;let box=document.getElementById('actions');"
          "box.innerHTML='';current.actions.forEach(a=>{let b=document.createElement('button');b.textContent=a.label+' ('+(100*a.p).toFixed(1)+'%)';"
          "b.onclick=()=>answer(a);box.appendChild(b);});document.getElementById('feedback').textContent='Choose your action.';}"
          "function answer(a){let best=current.actions.reduce((x,y)=>y.p>x.p?y:x);seen++;if(a.p>=best.p){score++;streak++;"
          "document.getElementById('feedback').textContent='Correct — highest-frequency action.';}else{streak=0;"
          "document.getElementById('feedback').textContent='Review: '+best.label+' is '+(100*best.p).toFixed(1)+'%.';}"
          "if(streak>=3)difficulty=Math.min(5,difficulty+1);if(!streak)difficulty=Math.max(1,difficulty-1);"
          "document.getElementById('score').textContent=score;document.getElementById('seen').textContent=seen;"
          "document.getElementById('difficulty').textContent=difficulty;setTimeout(nextSpot,700);}nextSpot();</script></body></html>\n", file);
    return fclose(file) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *solution = NULL;
    const char *labels_path = NULL;
    trainer_label_t *labels = NULL;
    size_t label_count = 0u;
    int rounds = 10;
    unsigned seed = 1u;
    const char *session_path = NULL;
    const char *resume_path = NULL;
    const char *export_html = NULL;
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
        else if (strcmp(argv[i], "--session-json") == 0 && i + 1 < argc)
            session_path = argv[++i];
        else if (strcmp(argv[i], "--resume-session") == 0 && i + 1 < argc)
            resume_path = argv[++i];
        else if (strcmp(argv[i], "--export-html") == 0 && i + 1 < argc)
            export_html = argv[++i];
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

    int answered = 0;
    int correct = 0;
    int streak = 0;
    int difficulty = 1;
    double probability_loss = 0.0;
    if (resume_path && load_session_summary(resume_path, &answered, &correct,
                                            &probability_loss) != 0)
    {
        fprintf(stderr, "Unable to read trainer session %s\n", resume_path);
        free(labels);
        return 1;
    }
    if (answered > 0)
    {
        double accuracy = (double)correct / (double)answered;
        difficulty = accuracy >= 0.80 ? 3 : accuracy >= 0.60 ? 2 : 1;
    }

    pe_sol_mmap_t *view = NULL;
    if (pe_sol_open_mmap(solution, &view) != 0)
    {
        fprintf(stderr, "Unable to open %s: %s\n", solution, strerror(errno));
        free(labels);
        return 1;
    }
    size_t count = pe_sol_mmap_infoset_count(view);
    if (count == 0)
    {
        fprintf(stderr, "Solution contains no infosets\n");
        pe_sol_close_mmap(view);
        return 1;
    }

    if (export_html)
    {
        const int result = write_html_app(export_html, view, labels, label_count);
        pe_sol_close_mmap(view);
        free(labels);
        if (result != 0) fprintf(stderr, "Unable to write trainer GUI %s\n", export_html);
        else printf("Wrote local trainer GUI: %s\n", export_html);
        return result == 0 ? 0 : 1;
    }

    printf("GTO trainer: %zu infosets, %d rounds\n", count, rounds);
    printf("Action labels: %s\n", label_count ? "loaded from metadata" : "numeric fallback");
    printf("Enter an action number, or q to stop.\n\n");

    trainer_event_t *events = NULL;
    size_t event_count = 0u;
    size_t event_capacity = 0u;
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
        if (spot && spot->runout[0]) printf(" runout=%s", spot->runout);
        if (spot && spot->position[0]) printf(" position=%s", spot->position);
        if (spot && spot->has_pot) printf(" pot=%.2f", spot->pot);
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
        if (event_count == event_capacity)
        {
            size_t next = event_capacity ? event_capacity * 2u : 16u;
            trainer_event_t *grown = realloc(events, next * sizeof(*events));
            if (!grown)
            {
                fprintf(stderr, "Unable to record trainer session\n");
                free(events);
                pe_sol_close_mmap(view);
                free(labels);
                return 1;
            }
            events = grown;
            event_capacity = next;
        }
        events[event_count].key = key;
        events[event_count].selected = (int)selected;
        events[event_count].best = best;
        events[event_count].selected_probability = probabilities[selected];
        events[event_count].best_probability = probabilities[best];
        ++event_count;
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
            ++streak;
            if (streak >= 3 && difficulty < 5) ++difficulty;
            printf("Correct: action %ld has the highest solved frequency.\n\n",
                   selected);
        }
        else
        {
            streak = 0;
            if (difficulty > 1) --difficulty;
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
    if (session_path && write_session(session_path, solution, seed, rounds,
                                      answered, correct, probability_loss,
                                      difficulty, resume_path,
                                      events, event_count) != 0)
        fprintf(stderr, "Unable to write trainer session %s\n", session_path);
    pe_sol_close_mmap(view);
    free(labels);
    free(events);
    return 0;
}
