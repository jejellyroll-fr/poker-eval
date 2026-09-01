#include <poker_eval/solver/pe_work_coordinator.h>
#include <poker_eval/solver/pe_work_executor.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(_WIN32)
#include <pthread.h>
#include <sys/socket.h>
#endif

static void mark_backend(pe_runtime_capabilities_t *runtime,
                         pe_compute_kind_t kind,
                         double update_rate)
{
    runtime->backends[kind].compiled = 1;
    runtime->backends[kind].runtime_available = 1;
    runtime->backends[kind].validated = 1;
    runtime->backends[kind].update_elements_per_s = update_rate;
    if (kind == PE_COMPUTE_CUDA || kind == PE_COMPUTE_OPENCL)
        runtime->backends[kind].capabilities = PE_CAP_GPU_TERMINAL_EVAL;
}

static void test_heterogeneous_schedule(void)
{
    pe_runtime_capabilities_t cpu = {0};
    pe_runtime_capabilities_t gpu = {0};
    pe_work_coordinator_t coordinator;
    pe_work_worker_assignment_t assignments[2];
    int count;

    mark_backend(&cpu, PE_COMPUTE_CPU_REF, 1.0);
    mark_backend(&gpu, PE_COMPUTE_CUDA, 9.0);
    pe_work_coordinator_init(&coordinator);
    assert(pe_work_coordinator_register(&coordinator, 11u, &cpu) == 0);
    assert(pe_work_coordinator_register(&coordinator, 22u, &gpu) == 0);

    count = pe_work_coordinator_schedule(&coordinator, 10u, assignments, 2u);
    assert(count == 2);
    assert(assignments[0].worker_id == 11u);
    assert(assignments[0].backend == PE_COMPUTE_CPU_REF);
    assert(assignments[0].first_unit == 0u);
    assert(assignments[0].unit_count == 1u);
    assert(assignments[1].worker_id == 22u);
    assert(assignments[1].backend == PE_COMPUTE_CUDA);
    assert(assignments[1].first_unit == 1u);
    assert(assignments[1].unit_count == 9u);
}

static void test_registry_and_filters(void)
{
    pe_runtime_capabilities_t runtime = {0};
    pe_work_coordinator_t coordinator;
    pe_work_worker_assignment_t assignment;

    mark_backend(&runtime, PE_COMPUTE_CPU_REF, 2.0);
    pe_work_coordinator_init(&coordinator);
    assert(pe_work_coordinator_register(NULL, 1u, &runtime) == -1);
    assert(pe_work_coordinator_register(&coordinator, 0u, &runtime) == -1);
    assert(pe_work_coordinator_register(&coordinator, 7u, &runtime) == 0);
    runtime.backends[PE_COMPUTE_CPU_REF].update_elements_per_s = 4.0;
    assert(pe_work_coordinator_register(&coordinator, 7u, &runtime) == 0);
    assert(coordinator.worker_count == 1u);
    assert(pe_work_coordinator_schedule(&coordinator, 1u, &assignment, 1u) == 1);
    assert(assignment.units_per_s == 4.0);
    assert(pe_work_coordinator_unregister(&coordinator, 7u) == 0);
    assert(pe_work_coordinator_unregister(&coordinator, 7u) == -1);
    assert(pe_work_coordinator_schedule(&coordinator, 1u, &assignment, 1u) == 0);
}

static void test_capacity_counts_nonempty_assignments(void)
{
    pe_runtime_capabilities_t runtime = {0};
    pe_work_coordinator_t coordinator;
    pe_work_worker_assignment_t assignment;

    mark_backend(&runtime, PE_COMPUTE_CPU_REF, 1.0);
    pe_work_coordinator_init(&coordinator);
    assert(pe_work_coordinator_register(&coordinator, 11u, &runtime) == 0);
    assert(pe_work_coordinator_register(&coordinator, 22u, &runtime) == 0);
    /* One unit is assigned to one of the two workers; a one-entry output
       buffer is therefore sufficient even though two workers are usable. */
    assert(pe_work_coordinator_schedule(&coordinator, 1u, &assignment, 1u) == 1);
    assert(assignment.unit_count == 1u);
}

static void test_capability_handshake(void)
{
#if defined(_WIN32)
    return;
#else
    pe_runtime_capabilities_t runtime = {0};
    pe_work_coordinator_t coordinator;
    int sockets[2];

    assert(pe_runtime_probe(&runtime) == 0);
    mark_backend(&runtime, PE_COMPUTE_CPU_REF, 2.0);
    pe_work_coordinator_init(&coordinator);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(pe_work_worker_announce((pe_work_socket_t)sockets[0],
                                   &runtime) == 0);
    assert(pe_work_coordinator_accept_announcement(
               &coordinator, 42u, (pe_work_socket_t)sockets[1]) == 0);
    assert(coordinator.worker_count == 1u);
    assert(coordinator.workers[0].worker_id == 42u);
    assert(coordinator.workers[0].runtime.backends[PE_COMPUTE_CPU_REF]
               .update_elements_per_s == 2.0);
    pe_work_socket_close((pe_work_socket_t)sockets[0]);
    pe_work_socket_close((pe_work_socket_t)sockets[1]);
#endif
}

static void test_dispatch_channels(void)
{
#if defined(_WIN32)
    return;
#else
    pe_runtime_capabilities_t cpu = {0};
    pe_runtime_capabilities_t gpu = {0};
    pe_work_coordinator_t coordinator;
    pe_work_worker_channel_t channels[2];
    pe_work_worker_assignment_t assignments[2];
    pe_work_unit_t units[10];
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE +
                  PE_WORK_UNIT_DESCRIPTOR_MAX + 1u];
    size_t frame_size;
    int cpu_sockets[2];
    int gpu_sockets[2];
    size_t i;
    int count;

    mark_backend(&cpu, PE_COMPUTE_CPU_REF, 1.0);
    mark_backend(&gpu, PE_COMPUTE_CUDA, 9.0);
    pe_work_coordinator_init(&coordinator);
    assert(pe_work_coordinator_register(&coordinator, 11u, &cpu) == 0);
    assert(pe_work_coordinator_register(&coordinator, 22u, &gpu) == 0);
    for (i = 0u; i < 10u; ++i) {
        pe_work_unit_init(&units[i]);
        units[i].public_state = (uint64_t)i + 1u;
        units[i].iteration_end = 1u;
    }
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, cpu_sockets) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, gpu_sockets) == 0);
    channels[0].worker_id = 11u;
    channels[0].socket = (pe_work_socket_t)cpu_sockets[0];
    channels[1].worker_id = 22u;
    channels[1].socket = (pe_work_socket_t)gpu_sockets[0];
    count = pe_work_coordinator_dispatch(&coordinator, units, 10u,
                                         channels, 2u, assignments, 2u);
    assert(count == 2);
    assert(assignments[0].worker_id == 11u &&
           assignments[0].unit_count == 1u);
    assert(assignments[1].worker_id == 22u &&
           assignments[1].unit_count == 9u);
    assert(pe_work_socket_recv_frame((pe_work_socket_t)cpu_sockets[1], frame,
                                     sizeof(frame), &frame_size) == 0);
    assert(pe_work_frame_decode_work_unit(frame, frame_size, &units[0]) == 0);
    assert(pe_work_socket_recv_frame((pe_work_socket_t)gpu_sockets[1], frame,
                                     sizeof(frame), &frame_size) == 0);
    assert(pe_work_frame_decode_work_unit(frame, frame_size, &units[0]) == 0);
    pe_work_unit_destroy(&units[0]);
    for (i = 1u; i < 10u; ++i)
        pe_work_unit_destroy(&units[i]);
    pe_work_socket_close((pe_work_socket_t)cpu_sockets[0]);
    pe_work_socket_close((pe_work_socket_t)cpu_sockets[1]);
    pe_work_socket_close((pe_work_socket_t)gpu_sockets[0]);
    pe_work_socket_close((pe_work_socket_t)gpu_sockets[1]);
#endif
}

static void test_collect_results(void)
{
#if defined(_WIN32)
    return;
#else
    pe_runtime_capabilities_t cpu = {0};
    pe_runtime_capabilities_t gpu = {0};
    pe_work_coordinator_t coordinator;
    pe_work_worker_channel_t channels[2];
    pe_work_worker_assignment_t assignments[2];
    pe_work_reducer_t reducer;
    pe_work_unit_t units[4];
    pe_work_unit_t received;
    pe_work_result_t result;
    uint8_t delta = 7u;
    int cpu_sockets[2];
    int gpu_sockets[2];
    size_t i;
    int count;

    mark_backend(&cpu, PE_COMPUTE_CPU_REF, 1.0);
    mark_backend(&gpu, PE_COMPUTE_CUDA, 3.0);
    pe_work_coordinator_init(&coordinator);
    assert(pe_work_coordinator_register(&coordinator, 11u, &cpu) == 0);
    assert(pe_work_coordinator_register(&coordinator, 22u, &gpu) == 0);
    for (i = 0u; i < 4u; ++i) {
        pe_work_unit_init(&units[i]);
        units[i].public_state = i + 1u;
        units[i].iteration_end = 1u;
    }
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, cpu_sockets) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, gpu_sockets) == 0);
    channels[0].worker_id = 11u;
    channels[0].socket = (pe_work_socket_t)cpu_sockets[0];
    channels[1].worker_id = 22u;
    channels[1].socket = (pe_work_socket_t)gpu_sockets[0];
    count = pe_work_coordinator_dispatch(&coordinator, units, 4u,
                                         channels, 2u, assignments, 2u);
    assert(count == 2);
    pe_work_reducer_init(&reducer);
    for (i = 0u; i < 2u; ++i) {
        pe_work_socket_t worker_socket = i == 0u
            ? (pe_work_socket_t)cpu_sockets[1]
            : (pe_work_socket_t)gpu_sockets[1];
        size_t j;
        for (j = 0u; j < assignments[i].unit_count; ++j) {
            pe_work_unit_init(&received);
            assert(pe_work_socket_recv_work_unit(worker_socket, &received) == 0);
            result = (pe_work_result_t){0};
            result.public_state = received.public_state;
            result.iteration_begin = received.iteration_begin;
            result.iteration_end = received.iteration_end;
            result.backend = assignments[i].backend;
            result.exploitability = 1.0;
            result.delta = &delta;
            result.delta_size = 1u;
            assert(pe_work_socket_send_result(worker_socket, &result) == 0);
            pe_work_unit_destroy(&received);
        }
    }
    assert(pe_work_coordinator_collect_results(
               units, 4u, assignments, 2u, channels, 2u, &reducer) == 0);
    assert(pe_work_reducer_count(&reducer) == 4u);
    pe_work_reducer_sort(&reducer);
    assert(pe_work_reducer_get(&reducer, 0u)->result.public_state == 1u);
    pe_work_reducer_destroy(&reducer);
    for (i = 0u; i < 4u; ++i)
        pe_work_unit_destroy(&units[i]);
    pe_work_socket_close((pe_work_socket_t)cpu_sockets[0]);
    pe_work_socket_close((pe_work_socket_t)cpu_sockets[1]);
    pe_work_socket_close((pe_work_socket_t)gpu_sockets[0]);
    pe_work_socket_close((pe_work_socket_t)gpu_sockets[1]);
#endif
}

#if !defined(_WIN32)
typedef struct coordinator_worker_thread_t {
    pe_work_socket_t socket;
    const pe_runtime_capabilities_t *runtime;
    size_t unit_count;
    int result;
} coordinator_worker_thread_t;

static int coordinator_test_execute(const pe_work_unit_t *unit,
                                    pe_compute_kind_t backend,
                                    pe_work_result_t *result,
                                    void *user_data)
{
    (void)user_data;
    result->iterations = unit->iteration_end - unit->iteration_begin;
    result->infosets_trained = 1u;
    result->constraints_satisfied = 1;
    result->backend = backend;
    result->exploitability = 0.0;
    result->worst_margin = 0.0;
    result->mean_margin = 0.0;
    return 0;
}

static void *coordinator_worker_thread(void *user_data)
{
    coordinator_worker_thread_t *worker = user_data;
    worker->result = pe_work_worker_run_batch(
        worker->socket, worker->runtime, coordinator_test_execute, NULL,
        worker->unit_count, NULL);
    return NULL;
}

static void test_dispatch_and_collect(void)
{
    pe_runtime_capabilities_t runtime = {0};
    pe_work_coordinator_t coordinator;
    pe_work_worker_channel_t channels[2];
    pe_work_worker_assignment_t assignments[2];
    pe_work_reducer_t reducer;
    pe_work_unit_t units[4];
    coordinator_worker_thread_t workers[2];
    pthread_t threads[2];
    int sockets[2][2];
    size_t i;

    mark_backend(&runtime, PE_COMPUTE_CPU_REF, 1.0);
    pe_work_coordinator_init(&coordinator);
    assert(pe_work_coordinator_register(&coordinator, 11u, &runtime) == 0);
    assert(pe_work_coordinator_register(&coordinator, 22u, &runtime) == 0);
    for (i = 0u; i < 4u; ++i) {
        pe_work_unit_init(&units[i]);
        units[i].public_state = i + 1u;
        units[i].iteration_end = 1u;
    }
    /* The socket pairs are per worker, not per unit. */
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets[0]) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets[1]) == 0);
    channels[0] = (pe_work_worker_channel_t){11u, (pe_work_socket_t)sockets[0][0]};
    channels[1] = (pe_work_worker_channel_t){22u, (pe_work_socket_t)sockets[1][0]};
    workers[0] = (coordinator_worker_thread_t){
        (pe_work_socket_t)sockets[0][1], &runtime, 2u, -1};
    workers[1] = (coordinator_worker_thread_t){
        (pe_work_socket_t)sockets[1][1], &runtime, 2u, -1};
    assert(pthread_create(&threads[0], NULL, coordinator_worker_thread,
                          &workers[0]) == 0);
    assert(pthread_create(&threads[1], NULL, coordinator_worker_thread,
                          &workers[1]) == 0);
    pe_work_reducer_init(&reducer);
    assert(pe_work_coordinator_dispatch_and_collect(
               &coordinator, units, 4u, channels, 2u, assignments, 2u,
               &reducer) == 2);
    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    assert(workers[0].result == 0 && workers[1].result == 0);
    assert(pe_work_reducer_count(&reducer) == 4u);
    pe_work_reducer_destroy(&reducer);
    for (i = 0u; i < 4u; ++i)
        pe_work_unit_destroy(&units[i]);
    for (i = 0u; i < 2u; ++i) {
        pe_work_socket_close((pe_work_socket_t)sockets[i][0]);
        pe_work_socket_close((pe_work_socket_t)sockets[i][1]);
    }
}
#endif

static int execute_distributed_unit(const pe_work_unit_t *unit,
                                    pe_compute_kind_t backend,
                                    pe_work_result_t *result,
                                    void *user_data)
{
    const pe_compute_kind_t expected = *(const pe_compute_kind_t *)user_data;
    assert(backend == expected);
    result->iterations = unit->iteration_end - unit->iteration_begin;
    result->infosets_trained = 1u;
    result->constraints_satisfied = 1;
    result->exploitability = 0.25;
    result->worst_margin = 0.0;
    result->mean_margin = 0.0;
    return 0;
}

static void test_end_to_end_dispatch(void)
{
#if defined(_WIN32)
    return;
#else
    pe_runtime_capabilities_t cpu = {0};
    pe_runtime_capabilities_t gpu = {0};
    pe_work_coordinator_t coordinator;
    pe_work_worker_channel_t channels[2];
    pe_work_worker_assignment_t assignments[2];
    pe_work_reducer_t reducer;
    pe_work_unit_t units[4];
    int cpu_sockets[2];
    int gpu_sockets[2];
    size_t i;
    int count;

    mark_backend(&cpu, PE_COMPUTE_CPU_REF, 1.0);
    mark_backend(&gpu, PE_COMPUTE_CUDA, 3.0);
    pe_work_coordinator_init(&coordinator);
    assert(pe_work_coordinator_register(&coordinator, 11u, &cpu) == 0);
    assert(pe_work_coordinator_register(&coordinator, 22u, &gpu) == 0);
    for (i = 0u; i < 4u; ++i) {
        pe_work_unit_init(&units[i]);
        units[i].public_state = i + 1u;
        units[i].iteration_end = 1u;
    }
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, cpu_sockets) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, gpu_sockets) == 0);
    channels[0].worker_id = 11u;
    channels[0].socket = (pe_work_socket_t)cpu_sockets[0];
    channels[1].worker_id = 22u;
    channels[1].socket = (pe_work_socket_t)gpu_sockets[0];
    count = pe_work_coordinator_dispatch(&coordinator, units, 4u,
                                         channels, 2u, assignments, 2u);
    assert(count == 2);
    for (i = 0u; i < 2u; ++i) {
        pe_work_socket_t worker_socket = i == 0u
            ? (pe_work_socket_t)cpu_sockets[1]
            : (pe_work_socket_t)gpu_sockets[1];
        const pe_runtime_capabilities_t *runtime = i == 0u ? &cpu : &gpu;
        size_t j;
        for (j = 0u; j < assignments[i].unit_count; ++j)
            assert(pe_work_worker_run_once(
                       worker_socket, runtime, execute_distributed_unit,
                       &assignments[i].backend) == 0);
    }
    pe_work_reducer_init(&reducer);
    assert(pe_work_coordinator_collect_results(
               units, 4u, assignments, 2u, channels, 2u, &reducer) == 0);
    assert(pe_work_reducer_count(&reducer) == 4u);
    pe_work_reducer_destroy(&reducer);
    for (i = 0u; i < 4u; ++i)
        pe_work_unit_destroy(&units[i]);
    pe_work_socket_close((pe_work_socket_t)cpu_sockets[0]);
    pe_work_socket_close((pe_work_socket_t)cpu_sockets[1]);
    pe_work_socket_close((pe_work_socket_t)gpu_sockets[0]);
    pe_work_socket_close((pe_work_socket_t)gpu_sockets[1]);
#endif
}

int main(void)
{
    test_heterogeneous_schedule();
    test_registry_and_filters();
    test_capacity_counts_nonempty_assignments();
    test_capability_handshake();
    test_dispatch_channels();
    test_collect_results();
#if !defined(_WIN32)
    test_dispatch_and_collect();
#endif
    test_end_to_end_dispatch();
    return 0;
}
