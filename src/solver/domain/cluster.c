/* cluster.c - deterministic distributed-solver partition primitives */

#include <poker_eval/solver/pe_solver_cluster.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

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
        sscanf(line, "%63[^\t]\t%u", magic, &manifest.version) != 2 ||
        strcmp(magic, "PE_SOLVER_CLUSTER_MANIFEST") != 0 ||
        !fgets(line, sizeof(line), f) || sscanf(line, "version\t%u", &manifest.version) != 1 ||
        !fgets(line, sizeof(line), f) || sscanf(line, "shards\t%u", &manifest.shard_count) != 1 ||
        !fgets(line, sizeof(line), f) || sscanf(line, "iteration\t%llu", (unsigned long long *)&manifest.iteration) != 1 ||
        !fgets(line, sizeof(line), f) || sscanf(line, "tasks\t%u", &manifest.task_count) != 1 ||
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
        unsigned long long begin, end, iteration;
        int consumed = 0;
        memset(task, 0, sizeof(*task));
        if (sscanf(line, "task\t%u\t%llu\t%llu\t%llu\t%15[^\t]\t%1023[^\n]%n",
                   &task->shard_id, &begin, &end, &iteration,
                   task->status, task->checkpoint_path, &consumed) != 6 ||
            task->shard_id >= manifest.shard_count || begin > end ||
            (line[consumed] != '\n' && line[consumed] != '\0'))
        {
            fclose(f);
            errno = EINVAL;
            return -1;
        }
        task->begin = (uint64_t)begin;
        task->end = (uint64_t)end;
        task->iteration = (uint64_t)iteration;
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
