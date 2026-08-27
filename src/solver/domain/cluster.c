/* cluster.c - deterministic distributed-solver partition primitives */

#include <poker_eval/solver/pe_solver_cluster.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_unsigned_text(const char *text, unsigned *out)
{
    char *end;
    unsigned long value;
    if (text == NULL || out == NULL || text[0] == '\0')
        return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text ||
        (*end != '\0' && *end != '\n') || value > UINT_MAX)
        return 0;
    *out = (unsigned)value;
    return 1;
}

static int parse_u64_text(const char *text, uint64_t *out)
{
    char *end;
    unsigned long long value;
    if (text == NULL || out == NULL || text[0] == '\0')
        return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text ||
        (*end != '\0' && *end != '\n'))
        return 0;
    *out = (uint64_t)value;
    return 1;
}

static size_t bounded_length(const char *text, size_t capacity)
{
    size_t length = 0u;
    if (text == NULL)
        return 0u;
    while (length < capacity && text[length] != '\0')
        ++length;
    return length;
}

static int parse_manifest_field(const char *line, const char *name,
                                unsigned *out)
{
    size_t length;
    if (line == NULL || name == NULL || out == NULL)
        return 0;
    length = bounded_length(name, PE_SOLVER_CLUSTER_PATH_MAX);
    if (length == PE_SOLVER_CLUSTER_PATH_MAX ||
        bounded_length(line, PE_SOLVER_CLUSTER_PATH_MAX + 128u) ==
            PE_SOLVER_CLUSTER_PATH_MAX + 128u)
        return 0;
    return strncmp(line, name, length) == 0 && line[length] == '\t' &&
           parse_unsigned_text(line + length + 1u, out);
}

static int parse_manifest_u64_field(const char *line, const char *name,
                                    uint64_t *out)
{
    size_t length;
    if (line == NULL || name == NULL || out == NULL)
        return 0;
    length = bounded_length(name, PE_SOLVER_CLUSTER_PATH_MAX);
    if (length == PE_SOLVER_CLUSTER_PATH_MAX ||
        bounded_length(line, PE_SOLVER_CLUSTER_PATH_MAX + 128u) ==
            PE_SOLVER_CLUSTER_PATH_MAX + 128u)
        return 0;
    return strncmp(line, name, length) == 0 && line[length] == '\t' &&
           parse_u64_text(line + length + 1u, out);
}

static int parse_task_line(char *line, pe_solver_cluster_task_t *task)
{
    char *fields[7];
    char *cursor;
    char *separator;
    uint32_t shard_id;
    uint64_t begin;
    uint64_t end;
    uint64_t iteration;
    size_t i;

    if (line == NULL || task == NULL)
        return 0;
    cursor = line;
    for (i = 0u; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        fields[i] = cursor;
        if (i + 1u == sizeof(fields) / sizeof(fields[0]))
            break;
        separator = strchr(cursor, '\t');
        if (separator == NULL)
            return 0;
        *separator = '\0';
        cursor = separator + 1;
    }
    separator = strchr(fields[6], '\n');
    if (separator != NULL)
    {
        *separator = '\0';
        if (strchr(separator + 1, '\n') != NULL)
            return 0;
    }
    if (fields[0][0] == '\0' || strcmp(fields[0], "task") != 0 ||
        fields[5][0] == '\0' || fields[6][0] == '\0' ||
        bounded_length(fields[5], PE_SOLVER_CLUSTER_MAX_STATUS) >=
            PE_SOLVER_CLUSTER_MAX_STATUS ||
        bounded_length(fields[6], PE_SOLVER_CLUSTER_PATH_MAX) >=
            PE_SOLVER_CLUSTER_PATH_MAX ||
        !parse_unsigned_text(fields[1], &shard_id) ||
        !parse_u64_text(fields[2], &begin) ||
        !parse_u64_text(fields[3], &end) ||
        !parse_u64_text(fields[4], &iteration) || begin > end)
        return 0;
    task->shard_id = shard_id;
    task->begin = begin;
    task->end = end;
    task->iteration = iteration;
    for (i = 0u; i < sizeof(task->status); ++i)
        task->status[i] = fields[5][i];
    for (i = 0u; i < sizeof(task->checkpoint_path); ++i)
        task->checkpoint_path[i] = fields[6][i];
    return 1;
}

static int parse_manifest_magic(const char *line, char magic[64],
                                unsigned *version)
{
    const char *tab;
    size_t length;
    if (line == NULL || magic == NULL || version == NULL)
        return 0;
    tab = strchr(line, '\t');
    if (tab == NULL)
        return 0;
    length = (size_t)(tab - line);
    if (length == 0u || length >= 64u)
        return 0;
    for (size_t i = 0u; i < length; ++i)
        magic[i] = line[i];
    magic[length] = '\0';
    return parse_unsigned_text(tab + 1, version);
}

uint32_t pe_solver_shard_for_key(uint64_t key, uint32_t shard_count)
{
    uint64_t x = key;
    if (shard_count == 0u)
        return UINT32_MAX;
    /* SplitMix64 finalizer avoids pathological low-bit distributions while
       preserving a cheap, platform-independent routing rule. */
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (uint32_t)(x % (uint64_t)shard_count);
}

int pe_solver_shard_valid(pe_solver_shard_t shard)
{
    return shard.shard_count > 0u && shard.shard_id < shard.shard_count;
}

int pe_solver_shard_owns(pe_solver_shard_t shard, uint64_t key)
{
    return pe_solver_shard_valid(shard) &&
           pe_solver_shard_for_key(key, shard.shard_count) == shard.shard_id;
}

int pe_solver_shard_range(pe_solver_shard_t shard, uint64_t total,
                          uint64_t *out_begin, uint64_t *out_end)
{
    uint64_t base;
    uint64_t remainder;
    if (!pe_solver_shard_valid(shard) || !out_begin || !out_end)
        return -1;
    base = total / (uint64_t)shard.shard_count;
    remainder = total % (uint64_t)shard.shard_count;
    *out_begin = (uint64_t)shard.shard_id * base +
                 ((uint64_t)shard.shard_id < remainder
                      ? (uint64_t)shard.shard_id : remainder);
    *out_end = *out_begin + base +
               ((uint64_t)shard.shard_id < remainder ? 1u : 0u);
    return 0;
}

static int pe_cluster_path_valid(const char *path)
{
    return path && *path && !strchr(path, '\n') && !strchr(path, '\t');
}

int pe_solver_cluster_manifest_write(
    const char *path,
    const pe_solver_cluster_manifest_t *manifest,
    const pe_solver_cluster_task_t *tasks)
{
    char tmp_path[PE_SOLVER_CLUSTER_PATH_MAX + 32];
    FILE *f;
    uint32_t i;

    if (!pe_cluster_path_valid(path) || !manifest ||
        manifest->version == 0 || manifest->shard_count == 0 ||
        manifest->task_count != manifest->shard_count || !tasks)
    {
        errno = EINVAL;
        return -1;
    }
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) >=
        (int)sizeof(tmp_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    f = fopen(tmp_path, "wb");
    if (!f)
        return -1;
    if (fprintf(f, "PE_SOLVER_CLUSTER_MANIFEST\t%u\nversion\t%u\nshards\t%u\niteration\t%llu\ntasks\t%u\n",
                manifest->version, manifest->version, manifest->shard_count,
                (unsigned long long)manifest->iteration,
                manifest->task_count) < 0)
        goto fail;
    for (i = 0; i < manifest->task_count; ++i)
    {
        const pe_solver_cluster_task_t *task = &tasks[i];
        if (task->shard_id >= manifest->shard_count ||
            task->begin > task->end ||
            strchr(task->status, '\n') || strchr(task->status, '\t') ||
            strchr(task->checkpoint_path, '\n') ||
            strchr(task->checkpoint_path, '\t') ||
            fprintf(f, "task\t%u\t%llu\t%llu\t%llu\t%s\t%s\n",
                    task->shard_id,
                    (unsigned long long)task->begin,
                    (unsigned long long)task->end,
                    (unsigned long long)task->iteration,
                    task->status, task->checkpoint_path) < 0)
            goto fail;
    }
    if (fclose(f) != 0)
        return -1;
#ifdef _WIN32
    /* MSVCRT rename does not replace an existing destination. The temporary
       file is in the same directory, so the replacement remains atomic from
       readers' perspective after this platform-specific step. */
    remove(path);
#endif
    if (rename(tmp_path, path) != 0)
        return -1;
    return 0;

fail:
    fclose(f);
    remove(tmp_path);
    errno = EIO;
    return -1;
}

int pe_solver_cluster_manifest_read(
    const char *path,
    pe_solver_cluster_manifest_t *out_manifest,
    pe_solver_cluster_task_t *tasks,
    uint32_t task_capacity)
{
    FILE *f;
    char line[PE_SOLVER_CLUSTER_PATH_MAX + 128];
    char magic[64];
    pe_solver_cluster_manifest_t manifest;
    uint32_t parsed_tasks = 0;

    if (!pe_cluster_path_valid(path) || !out_manifest || !tasks ||
        task_capacity == 0)
    {
        errno = EINVAL;
        return -1;
    }
    f = fopen(path, "rb");
    if (!f)
        return -1;
    memset(&manifest, 0, sizeof(manifest));
    if (!fgets(line, sizeof(line), f) ||
        !parse_manifest_magic(line, magic, &manifest.version) ||
        strcmp(magic, "PE_SOLVER_CLUSTER_MANIFEST") != 0 ||
        !fgets(line, sizeof(line), f) || !parse_manifest_field(line, "version", &manifest.version) ||
        !fgets(line, sizeof(line), f) || !parse_manifest_field(line, "shards", &manifest.shard_count) ||
        !fgets(line, sizeof(line), f) || !parse_manifest_u64_field(line, "iteration", &manifest.iteration) ||
        !fgets(line, sizeof(line), f) || !parse_manifest_field(line, "tasks", &manifest.task_count) ||
        manifest.version == 0 || manifest.shard_count == 0 ||
        manifest.task_count != manifest.shard_count ||
        manifest.task_count > task_capacity)
    {
        fclose(f);
        errno = EINVAL;
        return -1;
    }
    while (parsed_tasks < manifest.task_count && fgets(line, sizeof(line), f))
    {
        pe_solver_cluster_task_t *task = &tasks[parsed_tasks];
        memset(task, 0, sizeof(*task));
        if (!parse_task_line(line, task) ||
            task->shard_id >= manifest.shard_count)
        {
            fclose(f);
            errno = EINVAL;
            return -1;
        }
        ++parsed_tasks;
    }
    fclose(f);
    if (parsed_tasks != manifest.task_count)
    {
        errno = EINVAL;
        return -1;
    }
    *out_manifest = manifest;
    return 0;
}
