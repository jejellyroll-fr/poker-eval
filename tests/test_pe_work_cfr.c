/* DIST-02/03: a real Hold'em river game through the WorkUnit worker path. */

#include <poker_eval/engine/solvers/cfr/cfr_work_adapter.h>
#include <poker_eval/engine/solvers/cfr/holdem_river_adapter.h>
#include <poker_eval/core/eval_context.h>
#include <poker_eval/solver/pe_work_executor.h>
#include <poker_eval/solver/pe_work_coordinator.h>

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int make_socket_pair(pe_work_socket_t sockets[2])
{
    pe_work_socket_t listener;
    pe_work_socket_t client;
    pe_work_socket_t server;
    uint16_t port = 0u;

    listener = pe_work_tcp_listen(0u, &port);
    if (listener == PE_WORK_SOCKET_INVALID || port == 0u)
        return -1;
    client = pe_work_tcp_connect("127.0.0.1", port);
    if (client == PE_WORK_SOCKET_INVALID)
    {
        (void)pe_work_socket_close(listener);
        return -1;
    }
    server = pe_work_tcp_accept(listener);
    (void)pe_work_socket_close(listener);
    if (server == PE_WORK_SOCKET_INVALID)
    {
        (void)pe_work_socket_close(client);
        return -1;
    }
    sockets[0] = client;
    sockets[1] = server;
    return 0;
}

static mask_t card(int rank, int suit)
{
    return mask_set(MASK_EMPTY, MODERN_MAKE_CARD(rank, suit));
}

static void put_be64(uint8_t *out, uint64_t value)
{
    size_t i;
    for (i = 0u; i < 8u; ++i)
        out[i] = (uint8_t)(value >> (56u - 8u * i));
}

static void make_unit(pe_work_unit_t *unit)
{
    const mask_t h0 = card(MODERN_RANK_A, MODERN_SUIT_SPADES) |
                      card(MODERN_RANK_K, MODERN_SUIT_SPADES);
    const mask_t h1 = card(MODERN_RANK_7, MODERN_SUIT_HEARTS) |
                      card(MODERN_RANK_7, MODERN_SUIT_CLUBS);
    const mask_t board = card(MODERN_RANK_2, MODERN_SUIT_DIAMONDS) |
                         card(MODERN_RANK_3, MODERN_SUIT_DIAMONDS) |
                         card(MODERN_RANK_4, MODERN_SUIT_DIAMONDS) |
                         card(MODERN_RANK_8, MODERN_SUIT_CLUBS) |
                         card(MODERN_RANK_9, MODERN_SUIT_CLUBS);
    pe_work_unit_init(unit);
    unit->public_state = 0x45u;
    unit->iteration_begin = 0u;
    unit->iteration_end = 32u;
    unit->player = 0u;
    unit->board_width = 8u;
    unit->board_count = 1u;
    unit->boards = (uint8_t *)malloc(8u);
    unit->ranges_size = 16u;
    unit->ranges = (uint8_t *)malloc(16u);
    assert(unit->boards && unit->ranges);
    put_be64(unit->boards, board);
    put_be64(unit->ranges, h0);
    put_be64(unit->ranges + 8u, h1);
    assert(pe_work_unit_validate(unit) == 0);
}

int main(void)
{
    EvalConfig eval_config = eval_config_holdem();
    EvalContext *context = eval_context_create(&eval_config);
    pe_cfr_holdem_river_work_context_t game_context;
    pe_cfr_work_executor_config_t execute_config;
    pe_runtime_capabilities_t runtime;
    pe_work_unit_t unit;
    pe_work_reducer_t reducer;
    pe_work_result_t result;
    cfr_storage_t *distributed = cfr_storage_create();
    cfr_storage_t *mono = cfr_storage_create();
    cfr_storage_t *cuda_distributed = NULL;
    cfr_game_t mono_game;
    holdem_river_state_t mono_state;
    pe_work_socket_t sockets[2];
    pe_work_socket_t listener;
    pe_work_socket_t client;
    uint16_t tcp_port;
    pe_work_coordinator_t coordinator;
    pe_work_worker_channel_t channel;
    pe_work_worker_assignment_t assignment;
    pe_work_reducer_t tcp_reducer;
    pe_work_reducer_t cuda_reducer;
    uint8_t *frame;
    size_t frame_size;
    uint64_t root_key;
    double distributed_strategy[5];
    double mono_strategy[5];
    int actions[5];
    int action_count;
    double mono_exploitability = 0.0;
    double distributed_exploitability;
    size_t applied = 0u;
    int i;

    assert(context != NULL);
    assert(distributed != NULL && mono != NULL);
    assert(pe_work_transport_init() == 0);
    assert(pe_runtime_probe(&runtime) == 0);
    runtime.backends[PE_COMPUTE_CPU_REF].compiled = 1;
    runtime.backends[PE_COMPUTE_CPU_REF].runtime_available = 1;
    runtime.backends[PE_COMPUTE_CPU_REF].validated = 1;
    runtime.backends[PE_COMPUTE_CPU_REF].update_elements_per_s = 1.0;
    runtime.backends[PE_COMPUTE_CPU_REF].kind = PE_COMPUTE_CPU_REF;
    memset(&execute_config, 0, sizeof(execute_config));
    execute_config.cfr.seed = 17;
    memset(&game_context, 0, sizeof(game_context));
    game_context.context = context;
    execute_config.build_game = pe_cfr_holdem_river_build_game;
    execute_config.destroy_game = pe_cfr_holdem_river_destroy_game;
    execute_config.user_data = &game_context;
    make_unit(&unit);
    pe_work_reducer_init(&reducer);
    assert(make_socket_pair(sockets) == 0);
    assert(pe_work_socket_send_work_unit((pe_work_socket_t)sockets[0],
                                         &unit) == 0);
    assert(pe_work_worker_run_once((pe_work_socket_t)sockets[1], &runtime,
                                   pe_cfr_work_execute, &execute_config) == 0);
    frame = (uint8_t *)malloc(PE_WORK_PROTOCOL_HEADER_SIZE +
                              PE_WORK_PROTOCOL_MAX_PAYLOAD);
    assert(frame != NULL);
    assert(pe_work_socket_recv_frame((pe_work_socket_t)sockets[0], frame,
                                     PE_WORK_PROTOCOL_HEADER_SIZE +
                                         PE_WORK_PROTOCOL_MAX_PAYLOAD,
                                     &frame_size) == 0);
    memset(&result, 0, sizeof(result));
    assert(pe_work_frame_decode_result(frame, frame_size, &result) == 0);
    distributed_exploitability = result.exploitability;
    assert(result.elapsed_ns > 0u && result.units_per_s > 0.0);
    assert(pe_work_reducer_accept(&reducer, 1u, &result) == 0);
    assert(pe_cfr_work_reducer_apply(&reducer, distributed, &applied) == 0);
    assert(applied == 1u);

    /* If a real CUDA evaluator is present, exercise the same WorkUnit through
     * the production compute port and compare its CFR result with the CPU
     * reference below. CPU-only builds keep this branch as a capability skip. */
    {
        const pe_compute_ops_t *cuda_ops = pe_compute_cuda_ops();
        pe_compute_config_t compute_config;
        void *cuda_self = NULL;
        int cuda_ready = 0;
        memset(&compute_config, 0, sizeof(compute_config));
        compute_config.cpu_threads = 0;
        compute_config.deterministic = 1;
        compute_config.terminal_batch_size = 1u;
        compute_config.update_batch_size = 1u;
        if (cuda_ops && cuda_ops->create(&cuda_self, &compute_config) == 0)
            cuda_ready = 1;
        if (cuda_ready) {
            runtime.backends[PE_COMPUTE_CUDA].compiled = 1;
            runtime.backends[PE_COMPUTE_CUDA].runtime_available = 1;
            runtime.backends[PE_COMPUTE_CUDA].validated = 1;
            runtime.backends[PE_COMPUTE_CUDA].capabilities =
                PE_CAP_GPU_TERMINAL_EVAL;
            runtime.backends[PE_COMPUTE_CUDA].update_elements_per_s = 10.0;
            runtime.backends[PE_COMPUTE_CUDA].kind = PE_COMPUTE_CUDA;
            game_context.compute_ops = cuda_ops;
            game_context.compute_self = cuda_self;
            cuda_distributed = cfr_storage_create();
            assert(cuda_distributed != NULL);
            pe_work_reducer_init(&cuda_reducer);
            assert(make_socket_pair(sockets) == 0);
            assert(pe_work_socket_send_work_unit((pe_work_socket_t)sockets[0],
                                                 &unit) == 0);
            assert(pe_work_worker_run_once((pe_work_socket_t)sockets[1],
                                           &runtime, pe_cfr_work_execute,
                                           &execute_config) == 0);
            assert(pe_work_socket_recv_frame((pe_work_socket_t)sockets[0],
                                             frame,
                                             PE_WORK_PROTOCOL_HEADER_SIZE +
                                                 PE_WORK_PROTOCOL_MAX_PAYLOAD,
                                             &frame_size) == 0);
            memset(&result, 0, sizeof(result));
            assert(pe_work_frame_decode_result(frame, frame_size, &result) == 0);
            assert(result.backend == PE_COMPUTE_CUDA);
            assert(pe_work_reducer_accept(&cuda_reducer, 3u, &result) == 0);
            assert(pe_cfr_work_reducer_apply(&cuda_reducer,
                                             cuda_distributed, &applied) == 0);
            assert(applied == 1u);
            pe_work_reducer_destroy(&cuda_reducer);
            (void)pe_work_socket_close((pe_work_socket_t)sockets[0]);
            (void)pe_work_socket_close((pe_work_socket_t)sockets[1]);
            cuda_ops->destroy(cuda_self);
            game_context.compute_ops = NULL;
            game_context.compute_self = NULL;
        }
    }

    /* Exercise the production TCP lifecycle: capability announcement,
     * coordinator accept, dispatch, worker execution and result collection. */
    listener = pe_work_tcp_listen(0u, &tcp_port);
    assert(listener != PE_WORK_SOCKET_INVALID && tcp_port != 0u);
    client = pe_work_tcp_connect("127.0.0.1", tcp_port);
    assert(client != PE_WORK_SOCKET_INVALID);
    assert(pe_work_socket_send_capabilities(client, &runtime) == 0);
    pe_work_coordinator_init(&coordinator);
    assert(pe_work_coordinator_accept_tcp(&coordinator, listener, 2u,
                                         &channel) == 0);
    assert(pe_work_coordinator_dispatch(&coordinator, &unit, 1u, &channel,
                                       1u, &assignment, 1u) == 1);
    assert(pe_work_worker_run_once(client, &runtime, pe_cfr_work_execute,
                                   &execute_config) == 0);
    pe_work_reducer_init(&tcp_reducer);
    assert(pe_work_coordinator_collect_results(&unit, 1u, &assignment, 1u,
                                              &channel, 1u,
                                              &tcp_reducer) == 0);
    assert(pe_work_reducer_count(&tcp_reducer) == 1u);
    pe_work_reducer_destroy(&tcp_reducer);
    (void)pe_work_socket_close(channel.socket);
    (void)pe_work_socket_close(client);
    (void)pe_work_socket_close(listener);
    pe_work_transport_cleanup();

    memset(&mono_game, 0, sizeof(mono_game));
    hr_build_game(context,
                  card(MODERN_RANK_A, MODERN_SUIT_SPADES) |
                      card(MODERN_RANK_K, MODERN_SUIT_SPADES),
                  card(MODERN_RANK_7, MODERN_SUIT_HEARTS) |
                      card(MODERN_RANK_7, MODERN_SUIT_CLUBS),
                  card(MODERN_RANK_2, MODERN_SUIT_DIAMONDS) |
                      card(MODERN_RANK_3, MODERN_SUIT_DIAMONDS) |
                      card(MODERN_RANK_4, MODERN_SUIT_DIAMONDS) |
                      card(MODERN_RANK_8, MODERN_SUIT_CLUBS) |
                      card(MODERN_RANK_9, MODERN_SUIT_CLUBS),
                  &mono_game, &mono_state);
    {
        cfr_config_t config = execute_config.cfr;
        config.max_iterations = 32;
        assert(cfr_solve(&mono_game, mono, &config,
                         &mono_exploitability) >= 0.0);
    }
    root_key = mono_game.get_infoset_key(mono_game.initial_state);
    action_count = mono_game.get_actions(&mono_game,
                                         (uint64_t)(uintptr_t)mono_game.initial_state,
                                         actions, 5, mono_game.game_data);
    assert(action_count == 5);
    cfr_storage_get_avg_strategy(distributed, root_key, action_count,
                                 distributed_strategy);
    cfr_storage_get_avg_strategy(mono, root_key, action_count, mono_strategy);
    for (i = 0; i < action_count; ++i)
        assert(memcmp(&distributed_strategy[i], &mono_strategy[i],
                      sizeof(double)) == 0);
    assert(memcmp(&distributed_exploitability, &mono_exploitability,
                  sizeof(double)) == 0);
    if (cuda_distributed) {
        cfr_storage_get_avg_strategy(cuda_distributed, root_key, action_count,
                                     distributed_strategy);
        for (i = 0; i < action_count; ++i)
            assert(memcmp(&distributed_strategy[i], &mono_strategy[i],
                          sizeof(double)) == 0);
        cfr_storage_destroy(cuda_distributed);
    }

    free(frame);
    pe_work_reducer_destroy(&reducer);
    pe_work_unit_destroy(&unit);
    (void)pe_work_socket_close((pe_work_socket_t)sockets[0]);
    (void)pe_work_socket_close((pe_work_socket_t)sockets[1]);
    cfr_storage_destroy(distributed);
    cfr_storage_destroy(mono);
    eval_context_destroy(context);
    return 0;
}
