#include <poker_eval/solver/pe_work_executor.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/socket.h>
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

int main(void)
{
    test_one_shot_worker();
    return 0;
}
