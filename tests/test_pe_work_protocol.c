#include <poker_eval/solver/pe_work_protocol.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
    test_round_trip();
    test_rejects_invalid_frames();
    test_work_unit_frame();
    return 0;
}
