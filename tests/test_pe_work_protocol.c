#include <poker_eval/solver/pe_work_protocol.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <unistd.h>
#endif

static void test_work_unit_frame(void)
{
    pe_work_unit_t source;
    pe_work_unit_t decoded;
    uint8_t boards[] = {1u, 2u, 3u, 4u};
    double regrets[] = {1.0, -0.0};
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE +
                  PE_WORK_UNIT_DESCRIPTOR_MAX + 1u];
    size_t frame_size;

    pe_work_unit_init(&source);
    pe_work_unit_init(&decoded);
    source.public_state = 0x1234u;
    source.player = 1u;
    source.iteration_end = 4u;
    source.boards = boards;
    source.board_count = 2u;
    source.board_width = 2u;
    source.regret_snapshot = regrets;
    source.regret_count = 2u;
    assert(pe_work_frame_encode_work_unit(&source, frame, sizeof(frame),
                                          &frame_size) == 0);
    assert(pe_work_frame_decode_work_unit(frame, frame_size, &decoded) == 0);
    assert(decoded.public_state == source.public_state);
    assert(decoded.player == source.player);
    assert(decoded.board_count == source.board_count);
    assert(memcmp(decoded.boards, boards, sizeof(boards)) == 0);
    assert(memcmp(decoded.regret_snapshot, regrets, sizeof(regrets)) == 0);
    pe_work_unit_destroy(&decoded);
}

static void test_result_frame(void)
{
    const uint8_t delta[] = {0xde, 0xad, 0xbe, 0xef};
    pe_work_result_t source = {0};
    pe_work_result_t decoded = {0};
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE +
                  PE_WORK_PROTOCOL_RESULT_FIXED_SIZE + sizeof(delta)];
    size_t frame_size;

    source.public_state = 0x55u;
    source.iteration_begin = 4u;
    source.iteration_end = 8u;
    source.iterations = 4u;
    source.infosets_trained = 17u;
    source.backend = PE_COMPUTE_CPU_PAR;
    source.constraints_satisfied = 1;
    source.exploitability = 1.25;
    source.worst_margin = -0.0;
    source.mean_margin = -3.5;
    source.delta = delta;
    source.delta_size = sizeof(delta);
    assert(pe_work_frame_encode_result(&source, frame, sizeof(frame),
                                       &frame_size) == 0);
    assert(pe_work_frame_decode_result(frame, frame_size, &decoded) == 0);
    assert(decoded.public_state == source.public_state);
    assert(decoded.backend == source.backend);
    assert(decoded.iteration_begin == source.iteration_begin);
    assert(decoded.iteration_end == source.iteration_end);
    assert(memcmp(&decoded.worst_margin, &source.worst_margin,
                  sizeof(double)) == 0);
    assert(decoded.delta_size == sizeof(delta));
    assert(memcmp(decoded.delta, delta, sizeof(delta)) == 0);
}

static void test_socket_transport(void)
{
#if defined(_WIN32)
    /* The codec remains fully covered on Windows; socket integration is
       exercised by the POSIX loopback test and compiled through ws2_32. */
    return;
#else
    const uint8_t payload[] = {0x10, 0x20, 0x30};
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE + sizeof(payload)];
    uint8_t received[sizeof(frame)];
    size_t frame_size;
    size_t received_size;
    int sockets[2];

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(pe_work_frame_encode(PE_WORK_MESSAGE_CAPABILITIES, payload,
                                sizeof(payload), frame, sizeof(frame),
                                &frame_size) == 0);
    assert(pe_work_socket_send_frame((pe_work_socket_t)sockets[0], frame,
                                     frame_size) == 0);
    assert(pe_work_socket_recv_frame((pe_work_socket_t)sockets[1], received,
                                     sizeof(received), &received_size) == 0);
    assert(received_size == frame_size);
    assert(memcmp(received, frame, frame_size) == 0);
    assert(pe_work_socket_close((pe_work_socket_t)sockets[0]) == 0);
    assert(pe_work_socket_close((pe_work_socket_t)sockets[1]) == 0);
#endif
}

static void test_socket_payload_helpers(void)
{
#if defined(_WIN32)
    return;
#else
    pe_runtime_capabilities_t source_runtime;
    pe_runtime_capabilities_t decoded_runtime;
    pe_work_unit_t source_unit;
    pe_work_unit_t decoded_unit;
    uint8_t boards[] = {1u, 2u, 3u, 4u};
    double regrets[] = {2.0, -1.0};
    int sockets[2];

    assert(pe_runtime_probe(&source_runtime) == 0);
    pe_work_unit_init(&source_unit);
    pe_work_unit_init(&decoded_unit);
    source_unit.public_state = 0xabcdu;
    source_unit.iteration_end = 7u;
    source_unit.boards = boards;
    source_unit.board_count = 2u;
    source_unit.board_width = 2u;
    source_unit.regret_snapshot = regrets;
    source_unit.regret_count = 2u;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(pe_work_socket_send_capabilities(
               (pe_work_socket_t)sockets[0], &source_runtime) == 0);
    assert(pe_work_socket_recv_capabilities(
               (pe_work_socket_t)sockets[1], &decoded_runtime) == 0);
    assert(decoded_runtime.logical_cpus == source_runtime.logical_cpus);
    assert(pe_work_socket_send_work_unit(
               (pe_work_socket_t)sockets[0], &source_unit) == 0);
    assert(pe_work_socket_recv_work_unit(
               (pe_work_socket_t)sockets[1], &decoded_unit) == 0);
    assert(decoded_unit.public_state == source_unit.public_state);
    assert(decoded_unit.board_count == source_unit.board_count);
    assert(memcmp(decoded_unit.boards, boards, sizeof(boards)) == 0);
    assert(memcmp(decoded_unit.regret_snapshot, regrets,
                  sizeof(regrets)) == 0);
    pe_work_unit_destroy(&decoded_unit);
    assert(pe_work_socket_close((pe_work_socket_t)sockets[0]) == 0);
    assert(pe_work_socket_close((pe_work_socket_t)sockets[1]) == 0);
#endif
}

static void test_capabilities_frame(void)
{
    pe_runtime_capabilities_t source;
    pe_runtime_capabilities_t decoded;
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE +
                  PE_RUNTIME_DESCRIPTOR_MAX + 1u];
    size_t frame_size;

    assert(pe_runtime_probe(&source) == 0);
    assert(pe_work_frame_encode_capabilities(&source, frame, sizeof(frame),
                                             &frame_size) == 0);
    assert(pe_work_frame_decode_capabilities(frame, frame_size, &decoded) == 0);
    assert(decoded.logical_cpus == source.logical_cpus);
    assert(decoded.openmp_available == source.openmp_available);
    assert(decoded.simd == source.simd);
    assert(decoded.backends[PE_COMPUTE_CPU_REF].capabilities ==
           source.backends[PE_COMPUTE_CPU_REF].capabilities);
}

static void test_round_trip(void)
{
    const uint8_t payload[] = {0x00, 0x01, 0x7f, 0x80, 0xff};
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE + sizeof(payload)];
    const uint8_t *decoded_payload = NULL;
    pe_work_message_type_t decoded_type;
    size_t decoded_size;
    size_t frame_size;

    assert(pe_work_frame_size(sizeof(payload)) == sizeof(frame));
    assert(pe_work_frame_encode(PE_WORK_MESSAGE_UNIT, payload,
                                sizeof(payload), frame, sizeof(frame),
                                &frame_size) == 0);
    assert(frame_size == sizeof(frame));
    assert(frame[0] == 'P' && frame[1] == 'E' &&
           frame[2] == 'W' && frame[3] == '1');
    assert(frame[8] == 0u && frame[15] == sizeof(payload));
    assert(pe_work_frame_decode(frame, frame_size, &decoded_type,
                                &decoded_payload, &decoded_size) == 0);
    assert(decoded_type == PE_WORK_MESSAGE_UNIT);
    assert(decoded_size == sizeof(payload));
    assert(memcmp(decoded_payload, payload, sizeof(payload)) == 0);
}

static void test_rejects_invalid_frames(void)
{
    uint8_t frame[PE_WORK_PROTOCOL_HEADER_SIZE] = {'P', 'E', 'W', '1',
                                                    PE_WORK_PROTOCOL_VERSION,
                                                    PE_WORK_MESSAGE_RESULT,
                                                    0, 0, 0, 0, 0, 0, 0, 0,
                                                    0, 0};
    const uint8_t *payload;
    pe_work_message_type_t type;
    size_t payload_size;
    size_t encoded_size;

    assert(pe_work_frame_decode(frame, sizeof(frame), &type, &payload,
                                &payload_size) == 0);
    assert(payload_size == 0u);
    frame[4] = 2u;
    assert(pe_work_frame_decode(frame, sizeof(frame), &type, &payload,
                                &payload_size) == -1);
    frame[4] = PE_WORK_PROTOCOL_VERSION;
    frame[5] = 0u;
    assert(pe_work_frame_decode(frame, sizeof(frame), &type, &payload,
                                &payload_size) == -1);
    assert(pe_work_frame_encode(PE_WORK_MESSAGE_UNIT, NULL, 1u, frame,
                                sizeof(frame), &encoded_size) == -1);
    assert(pe_work_frame_encode(PE_WORK_MESSAGE_UNIT, NULL, 0u, frame,
                                sizeof(frame) - 1u, &encoded_size) == -1);
    assert(pe_work_frame_size(PE_WORK_PROTOCOL_MAX_PAYLOAD + 1u) == 0u);
}

int main(void)
{
    assert(pe_work_transport_init() == 0);
    test_round_trip();
    test_rejects_invalid_frames();
    test_work_unit_frame();
    test_result_frame();
    test_socket_transport();
    test_socket_payload_helpers();
    test_capabilities_frame();
    pe_work_transport_cleanup();
    return 0;
}
