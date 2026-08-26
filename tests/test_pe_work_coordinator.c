#include <poker_eval/solver/pe_work_coordinator.h>

#include <assert.h>

#if !defined(_WIN32)
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

int main(void)
{
    test_heterogeneous_schedule();
    test_registry_and_filters();
    test_dispatch_channels();
    return 0;
}
