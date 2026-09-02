#include <poker_eval/solver/pe_work_executor.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static int execute_unit(const pe_work_unit_t *unit,
                        pe_compute_kind_t backend,
                        pe_work_result_t *result,
                        void *user_data)
{
    static const uint8_t delta[] = {9u, 8u};
    (void)user_data;
    assert(unit->public_state == 0x123u);
    assert(backend == PE_COMPUTE_CUDA);
    result->iterations = unit->iteration_end - unit->iteration_begin;
    result->infosets_trained = 2u;
    result->constraints_satisfied = 1;
    result->exploitability = 0.5;
    result->worst_margin = 0.0;
    result->mean_margin = 0.0;
    result->delta = delta;
    result->delta_size = sizeof(delta);
    return 0;
}

static void test_one_shot_worker(void)
{
#if defined(_WIN32)
    return;
#else
    pe_runtime_capabilities_t runtime = {0};
    pe_work_unit_t unit;
    pe_work_result_t result;
    uint8_t boards[] = {1u, 2u};
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE +
                  PE_WORK_PROTOCOL_RESULT_FIXED_SIZE + 2u];
    size_t frame_size;
    int sockets[2];

    runtime.backends[PE_COMPUTE_CUDA].compiled = 1;
    runtime.backends[PE_COMPUTE_CUDA].runtime_available = 1;
    runtime.backends[PE_COMPUTE_CUDA].validated = 1;
    runtime.backends[PE_COMPUTE_CUDA].capabilities =
        PE_CAP_GPU_TERMINAL_EVAL;
    runtime.backends[PE_COMPUTE_CUDA].update_elements_per_s = 10.0;
    assert(pe_work_worker_backend(&runtime) == PE_COMPUTE_CUDA);
    pe_work_unit_init(&unit);
    unit.public_state = 0x123u;
    unit.iteration_end = 4u;
    unit.boards = boards;
    unit.board_count = 1u;
    unit.board_width = 2u;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(pe_work_socket_send_work_unit((pe_work_socket_t)sockets[0],
                                         &unit) == 0);
    assert(pe_work_worker_run_once((pe_work_socket_t)sockets[1], &runtime,
                                   execute_unit, NULL) == 0);
    assert(pe_work_socket_recv_frame((pe_work_socket_t)sockets[0], frame,
                                     sizeof(frame), &frame_size) == 0);
    assert(pe_work_frame_decode_result(frame, frame_size, &result) == 0);
    assert(result.public_state == unit.public_state);
    assert(result.backend == PE_COMPUTE_CUDA);
    assert(result.iterations == 4u);
    assert(result.delta_size == 2u);
    assert(result.delta[0] == 9u && result.delta[1] == 8u);
    pe_work_socket_close((pe_work_socket_t)sockets[0]);
    pe_work_socket_close((pe_work_socket_t)sockets[1]);
#endif
}

static int execute_batch_unit(const pe_work_unit_t *unit,
                              pe_compute_kind_t backend,
                              pe_work_result_t *result,
                              void *user_data)
{
    (void)user_data;
    assert(backend == PE_COMPUTE_CPU_REF);
    result->iterations = unit->iteration_end - unit->iteration_begin;
    result->infosets_trained = 1u;
    result->constraints_satisfied = 1;
    result->exploitability = 0.1;
    return 0;
}

static void test_batch_worker(void)
{
#if defined(_WIN32)
    return;
#else
    pe_runtime_capabilities_t runtime = {0};
    pe_work_unit_t units[3];
    pe_work_result_t result;
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE +
                  PE_WORK_PROTOCOL_RESULT_FIXED_SIZE];
    size_t frame_size;
    size_t processed;
    int sockets[2];
    size_t i;

    runtime.backends[PE_COMPUTE_CPU_REF].compiled = 1;
    runtime.backends[PE_COMPUTE_CPU_REF].runtime_available = 1;
    runtime.backends[PE_COMPUTE_CPU_REF].validated = 1;
    for (i = 0u; i < 3u; ++i) {
        pe_work_unit_init(&units[i]);
        units[i].public_state = i + 10u;
        units[i].iteration_end = 1u;
    }
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    for (i = 0u; i < 3u; ++i)
        assert(pe_work_socket_send_work_unit((pe_work_socket_t)sockets[0],
                                              &units[i]) == 0);
    assert(pe_work_worker_run_batch((pe_work_socket_t)sockets[1], &runtime,
                                    execute_batch_unit, NULL, 3u,
                                    &processed) == 0);
    assert(processed == 3u);
    for (i = 0u; i < 3u; ++i) {
        assert(pe_work_socket_recv_frame((pe_work_socket_t)sockets[0], frame,
                                         sizeof(frame), &frame_size) == 0);
        assert(pe_work_frame_decode_result(frame, frame_size, &result) == 0);
        assert(result.public_state == i + 10u);
        assert(result.backend == PE_COMPUTE_CPU_REF);
    }
    for (i = 0u; i < 3u; ++i)
        pe_work_unit_destroy(&units[i]);
    pe_work_socket_close((pe_work_socket_t)sockets[0]);
    pe_work_socket_close((pe_work_socket_t)sockets[1]);
#endif
}

static void test_persistent_worker(void)
{
#if defined(_WIN32)
    return;
#else
    pe_runtime_capabilities_t runtime;
    pe_runtime_capabilities_t announced = {0};
    pe_work_unit_t units[2];
    pe_work_result_t result;
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE +
                  PE_WORK_PROTOCOL_RESULT_FIXED_SIZE];
    size_t frame_size;
    size_t processed = 0u;
    int sockets[2];
    pid_t child;
    int status;
    size_t i;

    assert(pe_runtime_probe(&runtime) == 0);
    runtime.backends[PE_COMPUTE_CPU_REF].compiled = 1;
    runtime.backends[PE_COMPUTE_CPU_REF].runtime_available = 1;
    runtime.backends[PE_COMPUTE_CPU_REF].validated = 1;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        int rc;
        pe_work_socket_close((pe_work_socket_t)sockets[0]);
        rc = pe_work_worker_serve_forever((pe_work_socket_t)sockets[1],
                                          &runtime, execute_batch_unit, NULL,
                                          &processed);
        pe_work_socket_close((pe_work_socket_t)sockets[1]);
        _exit(rc == 0 && processed == 2u ? 0 : 1);
    }

    pe_work_socket_close((pe_work_socket_t)sockets[1]);
    assert(pe_work_socket_recv_capabilities((pe_work_socket_t)sockets[0],
                                             &announced) == 0);
    assert(announced.backends[PE_COMPUTE_CPU_REF].validated == 1);
    for (i = 0u; i < 2u; ++i) {
        pe_work_unit_init(&units[i]);
        units[i].public_state = i + 20u;
        units[i].iteration_end = 2u;
        assert(pe_work_socket_send_work_unit((pe_work_socket_t)sockets[0],
                                              &units[i]) == 0);
    }
    for (i = 0u; i < 2u; ++i) {
        assert(pe_work_socket_recv_frame((pe_work_socket_t)sockets[0], frame,
                                         sizeof(frame), &frame_size) == 0);
        assert(pe_work_frame_decode_result(frame, frame_size, &result) == 0);
        assert(result.public_state == i + 20u);
        assert(result.backend == PE_COMPUTE_CPU_REF);
    }
    assert(pe_work_socket_send_shutdown((pe_work_socket_t)sockets[0]) == 0);
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    for (i = 0u; i < 2u; ++i)
        pe_work_unit_destroy(&units[i]);
    pe_work_socket_close((pe_work_socket_t)sockets[0]);
#endif
}

int main(void)
{
    test_one_shot_worker();
    test_batch_worker();
    test_persistent_worker();
    return 0;
}
