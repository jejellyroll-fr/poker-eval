#include <poker_eval/solver/pe_solver_cluster.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    pe_solver_shard_t a = {2u, 5u};
    uint64_t begin = 0u;
    uint64_t end = 0u;
    const char *manifest_path = "pe_solver_cluster_test.manifest";
    pe_solver_cluster_manifest_t manifest = {1u, 2u, 64u, 2u};
    pe_solver_cluster_task_t tasks[2] = {
        {0u, 0u, 32u, 64u, "ready", "worker-0.pe_sol"},
        {1u, 32u, 64u, 64u, "pending", "worker-1.pe_sol"}
    };
    pe_solver_cluster_manifest_t loaded;
    pe_solver_cluster_task_t loaded_tasks[2];
    if (!pe_solver_shard_valid(a) || pe_solver_shard_owns(a, UINT64_C(42)) !=
            (pe_solver_shard_for_key(UINT64_C(42), 5u) == 2u) ||
        pe_solver_shard_range(a, 103u, &begin, &end) != 0 ||
        begin >= end || end > 103u)
    {
        fprintf(stderr, "solver shard contract regression failed\n");
        return 1;
    }
    if (pe_solver_cluster_manifest_write(manifest_path, &manifest, tasks) != 0 ||
        pe_solver_cluster_manifest_read(manifest_path, &loaded, loaded_tasks, 2u) != 0 ||
        loaded.version != manifest.version || loaded.shard_count != 2u ||
        loaded.iteration != 64u || loaded.task_count != 2u ||
        loaded_tasks[1].shard_id != 1u ||
        strcmp(loaded_tasks[1].status, "pending") != 0 ||
        strcmp(loaded_tasks[1].checkpoint_path, "worker-1.pe_sol") != 0)
    {
        fprintf(stderr, "solver cluster manifest contract failed\n");
        remove(manifest_path);
        return 1;
    }
    remove(manifest_path);
    return 0;
}
