/*
 * work_executor.c - one-shot worker execution boundary
 */

#include <poker_eval/solver/pe_work_executor.h>

#include <math.h>
#include <stddef.h>

static double backend_rate(const pe_runtime_backend_info_t *backend)
{
    double rate = backend->update_elements_per_s;
    if (!(rate > 0.0) || !isfinite(rate))
        rate = backend->strategy_elements_per_s;
    if (!(rate > 0.0) || !isfinite(rate))
        rate = backend->terminal_elements_per_s;
    if (!(rate > 0.0) || !isfinite(rate))
        rate = 1.0;
    return rate;
}

static int backend_usable(const pe_runtime_backend_info_t *backend,
                          pe_compute_kind_t kind)
{
    if (!backend || !backend->compiled || !backend->runtime_available ||
        !backend->validated)
        return 0;
    if ((kind == PE_COMPUTE_CUDA || kind == PE_COMPUTE_OPENCL) &&
        !(backend->capabilities & PE_CAP_GPU_TERMINAL_EVAL))
        return 0;
    return 1;
}

pe_compute_kind_t pe_work_worker_backend(
    const pe_runtime_capabilities_t *runtime)
{
    pe_compute_kind_t best = PE_COMPUTE_AUTO;
    double best_rate = 0.0;
    size_t i;

    if (!runtime)
        return PE_COMPUTE_AUTO;
    for (i = 1u; i < PE_COMPUTE_COUNT; ++i) {
        const pe_runtime_backend_info_t *info = &runtime->backends[i];
        double rate;
        if (!backend_usable(info, (pe_compute_kind_t)i))
            continue;
        rate = backend_rate(info);
        if (best == PE_COMPUTE_AUTO || rate > best_rate) {
            best = (pe_compute_kind_t)i;
            best_rate = rate;
        }
    }
    return best;
}

int pe_work_worker_announce(pe_work_socket_t socket,
                            const pe_runtime_capabilities_t *runtime)
{
    if (socket == PE_WORK_SOCKET_INVALID || !runtime)
        return -1;
    return pe_work_socket_send_capabilities(socket, runtime);
}

int pe_work_worker_run_once(pe_work_socket_t socket,
                            const pe_runtime_capabilities_t *runtime,
                            pe_work_execute_fn execute,
                            void *user_data)
{
    pe_work_unit_t unit;
    pe_work_result_t result = {0};
    pe_compute_kind_t backend;
    int rc;

    if (socket == PE_WORK_SOCKET_INVALID || !runtime || !execute)
        return -1;
    backend = pe_work_worker_backend(runtime);
    if (backend == PE_COMPUTE_AUTO)
        return -1;
    pe_work_unit_init(&unit);
    rc = pe_work_socket_recv_work_unit(socket, &unit);
    if (rc == 0) {
        result.public_state = unit.public_state;
        result.iteration_begin = unit.iteration_begin;
        result.iteration_end = unit.iteration_end;
        result.backend = backend;
        rc = execute(&unit, backend, &result, user_data);
    }
    if (rc == 0 && pe_work_result_validate(&result) != 0)
        rc = -1;
    if (rc == 0)
        rc = pe_work_socket_send_result(socket, &result);
    pe_work_result_release(&result);
    pe_work_unit_destroy(&unit);
    return rc;
}

int pe_work_worker_run_batch(pe_work_socket_t socket,
                             const pe_runtime_capabilities_t *runtime,
                             pe_work_execute_fn execute,
                             void *user_data,
                             size_t unit_count,
                             size_t *processed)
{
    size_t i;

    if (processed)
        *processed = 0u;
    if (socket == PE_WORK_SOCKET_INVALID || !runtime || !execute)
        return -1;
    for (i = 0u; i < unit_count; ++i) {
        if (pe_work_worker_run_once(socket, runtime, execute, user_data) != 0)
            return -1;
        if (processed)
            *processed = i + 1u;
    }
    return 0;
}

int pe_work_worker_serve(pe_work_socket_t socket,
                         const pe_runtime_capabilities_t *runtime,
                         pe_work_execute_fn execute,
                         void *user_data,
                         size_t unit_count,
                         size_t *processed)
{
    int rc;
    if (pe_work_worker_announce(socket, runtime) != 0)
        return -1;
    rc = pe_work_worker_run_batch(socket, runtime, execute, user_data,
                                  unit_count, processed);
    (void)pe_work_socket_close(socket);
    return rc;
}
