/*
 * The four OpenCL kernel files each carry their own copy of the shared macro
 * blocks -- types, HandVal macros, deck ranks -- wrapped in one #ifndef guard
 * per block so that concatenating them for a single clBuildProgram call keeps
 * exactly one copy. The file loaded first wins the guard and every later copy
 * is skipped.
 *
 * That makes a macro added to only one copy invisible: it compiles when the
 * file is built alone, and silently disappears in the combined program that
 * production actually builds. Adding LowHandVal's HANDTYPE accessor to just
 * eval_low_kernel.cl did exactly that -- eval_kernel.cl is concatenated first,
 * so the accessor was never defined and the combined program failed to build,
 * while both the standalone syntax check and the host-side parity test passed.
 *
 * So: every copy of a guarded block must define the same macros. Comments and
 * spacing are ignored -- only the directives matter, and a test that failed on
 * a reworded comment would soon be deleted rather than obeyed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OPENCL_KERNEL_DIR
#error "OPENCL_KERNEL_DIR must be defined by the build"
#endif

#define MAX_SOURCE (512 * 1024)
#define MAX_BLOCK (64 * 1024)

static const char *kernel_files[] = {
    "eval_kernel.cl",
    "eval_low_kernel.cl",
    "eval_omaha_kernel.cl",
    "eval_generic_kernel.cl",
};
#define KERNEL_COUNT ((int)(sizeof(kernel_files) / sizeof(kernel_files[0])))

/* Guards shared between several kernel files. */
static const char *shared_guards[] = {
    "OPENCL_POKER_TYPES_DEFINED",
    "OPENCL_HANDVAL_MACROS_DEFINED",
    "OPENCL_STDDECK_RANKS_DEFINED",
};
#define GUARD_COUNT ((int)(sizeof(shared_guards) / sizeof(shared_guards[0])))

static char *read_file(const char *name) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", OPENCL_KERNEL_DIR, name);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }

    char *buffer = malloc(MAX_SOURCE);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t got = fread(buffer, 1, MAX_SOURCE - 1, f);
    buffer[got] = '\0';
    fclose(f);
    return buffer;
}

/*
 * Append one normalised "#define ..." line to out: comment stripped, runs of
 * whitespace collapsed to a single space, trailing space removed.
 */
static void append_directive(const char *line, size_t length, char *out) {
    size_t used = strlen(out);
    int pending_space = 0;

    for (size_t i = 0; i < length; ++i) {
        if (line[i] == '/' && i + 1 < length && (line[i + 1] == '*' || line[i + 1] == '/')) {
            if (line[i + 1] == '/')
                break;
            const char *end = strstr(line + i + 2, "*/");
            if (!end || (size_t)(end - line) >= length)
                break;
            i = (size_t)(end - line) + 1;
            pending_space = 1;
            continue;
        }

        if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r') {
            pending_space = 1;
            continue;
        }

        if (pending_space && used > 0 && out[used - 1] != '\n') {
            out[used++] = ' ';
        }
        pending_space = 0;
        out[used++] = line[i];
    }

    while (used > 0 && out[used - 1] == ' ')
        used--;
    out[used++] = '\n';
    out[used] = '\0';
}

/*
 * Collect the normalised #define directives between "#ifndef <guard>" and its
 * matching "#endif" into out.
 * Returns 1 when the guard is present, 0 when the file does not use it.
 */
static int extract_block(const char *source, const char *guard, char *out) {
    char opening[256];
    snprintf(opening, sizeof(opening), "#ifndef %s", guard);

    const char *start = strstr(source, opening);
    if (!start)
        return 0;

    out[0] = '\0';

    const char *cursor = start;
    int depth = 0;
    while (*cursor) {
        const char *newline = strchr(cursor, '\n');
        size_t line_length = newline ? (size_t)(newline - cursor) : strlen(cursor);

        if (strncmp(cursor, "#ifndef", 7) == 0 || strncmp(cursor, "#ifdef", 6) == 0 ||
            strncmp(cursor, "#if", 3) == 0) {
            depth++;
        } else if (strncmp(cursor, "#endif", 6) == 0) {
            depth--;
            if (depth == 0)
                return 1;
        } else if (strncmp(cursor, "#define", 7) == 0) {
            /* The guard's own #define is skipped: it names the guard, and every
               copy defines it, so it carries no information. */
            if (strncmp(cursor + 8, guard, strlen(guard)) != 0) {
                if (strlen(out) + line_length + 2 >= MAX_BLOCK) {
                    fprintf(stderr, "block for %s is too large\n", guard);
                    return -1;
                }
                append_directive(cursor, line_length, out);
            }
        }

        if (!newline)
            break;
        cursor = newline + 1;
    }

    fprintf(stderr, "unterminated #ifndef %s\n", guard);
    return -1;
}

int main(void) {
    char *sources[KERNEL_COUNT];
    for (int i = 0; i < KERNEL_COUNT; ++i) {
        sources[i] = read_file(kernel_files[i]);
        if (!sources[i])
            return 1;
    }

    static char reference[MAX_BLOCK];
    static char candidate[MAX_BLOCK];
    int status = 0;

    for (int g = 0; g < GUARD_COUNT; ++g) {
        const char *guard = shared_guards[g];
        int reference_file = -1;
        int copies = 0;

        for (int i = 0; i < KERNEL_COUNT; ++i) {
            int found = extract_block(sources[i], guard,
                                      reference_file < 0 ? reference : candidate);
            if (found < 0) {
                status = 1;
                continue;
            }
            if (!found)
                continue;

            copies++;
            if (reference_file < 0) {
                reference_file = i;
                continue;
            }
            if (strcmp(reference, candidate) != 0) {
                fprintf(stderr,
                        "%s differs between %s and %s.\n"
                        "Every copy of a guarded block must be identical: the file "
                        "concatenated first wins the guard, so anything defined in "
                        "only one copy vanishes from the combined program.\n",
                        guard, kernel_files[reference_file], kernel_files[i]);
                status = 1;
            }
        }

        if (copies < 2) {
            fprintf(stderr,
                    "%s appears in %d kernel file(s); it is no longer shared, so this "
                    "test is not checking what it claims. Update shared_guards[].\n",
                    guard, copies);
            status = 1;
        }
    }

    for (int i = 0; i < KERNEL_COUNT; ++i)
        free(sources[i]);

    if (status == 0)
        printf("OpenCL kernel guard blocks are consistent.\n");
    return status;
}
