/*
 * work_protocol.c - versioned binary framing for distributed work (DIST-03)
 */

#include <poker_eval/solver/pe_work_protocol.h>

#include <math.h>
#include <stddef.h>
#include <string.h>

static const uint8_t pe_work_magic[4] = {'P', 'E', 'W', '1'};

static int valid_type(pe_work_message_type_t type)
{
    return type >= PE_WORK_MESSAGE_CAPABILITIES &&
           type <= PE_WORK_MESSAGE_RESULT;
}

static void put_u64_be(uint8_t *out, uint64_t value)
{
    size_t i;
    for (i = 0u; i < 8u; ++i)
        out[i] = (uint8_t)(value >> (56u - 8u * i));
}

static uint64_t get_u64_be(const uint8_t *in)
{
    size_t i;
    uint64_t value = 0u;
    for (i = 0u; i < 8u; ++i)
        value = (value << 8u) | (uint64_t)in[i];
    return value;
}

static void put_double_be(uint8_t *out, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_u64_be(out, bits);
}

static double get_double_be(const uint8_t *in)
{
    uint64_t bits = get_u64_be(in);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

size_t pe_work_frame_size(size_t payload_size)
{
    if (payload_size > PE_WORK_PROTOCOL_MAX_PAYLOAD ||
        payload_size > SIZE_MAX - PE_WORK_PROTOCOL_HEADER_SIZE)
        return 0u;
    return PE_WORK_PROTOCOL_HEADER_SIZE + payload_size;
}

int pe_work_frame_encode(pe_work_message_type_t type,
                         const uint8_t *payload,
                         size_t payload_size,
                         uint8_t *out,
                         size_t capacity,
                         size_t *out_size)
{
    size_t frame_size;
    size_t i;

    if (!out || !out_size || !valid_type(type) ||
        (payload_size != 0u && !payload))
        return -1;
    frame_size = pe_work_frame_size(payload_size);
    if (frame_size == 0u || capacity < frame_size)
        return -1;

    for (i = 0u; i < sizeof(pe_work_magic); ++i)
        out[i] = pe_work_magic[i];
    out[4] = PE_WORK_PROTOCOL_VERSION;
    out[5] = (uint8_t)type;
    out[6] = 0u;
    out[7] = 0u;
    put_u64_be(out + 8u, (uint64_t)payload_size);
    for (i = 0u; i < payload_size; ++i)
        out[PE_WORK_PROTOCOL_HEADER_SIZE + i] = payload[i];
    *out_size = frame_size;
    return 0;
}

int pe_work_frame_decode(const uint8_t *frame,
                         size_t frame_size,
                         pe_work_message_type_t *out_type,
                         const uint8_t **out_payload,
                         size_t *out_payload_size)
{
    uint64_t encoded_size;
    size_t i;

    if (!frame || !out_type || !out_payload || !out_payload_size ||
        frame_size < PE_WORK_PROTOCOL_HEADER_SIZE)
        return -1;
    for (i = 0u; i < sizeof(pe_work_magic); ++i) {
        if (frame[i] != pe_work_magic[i])
            return -1;
    }
    if (frame[4] != PE_WORK_PROTOCOL_VERSION || frame[6] != 0u ||
        frame[7] != 0u || !valid_type((pe_work_message_type_t)frame[5]))
        return -1;

    encoded_size = get_u64_be(frame + 8u);
    if (encoded_size > PE_WORK_PROTOCOL_MAX_PAYLOAD ||
        encoded_size != (uint64_t)(frame_size - PE_WORK_PROTOCOL_HEADER_SIZE))
        return -1;

    *out_type = (pe_work_message_type_t)frame[5];
    *out_payload = frame + PE_WORK_PROTOCOL_HEADER_SIZE;
    *out_payload_size = (size_t)encoded_size;
    return 0;
}

int pe_work_frame_encode_work_unit(const pe_work_unit_t *unit,
                                   uint8_t *out,
                                   size_t capacity,
                                   size_t *out_size)
{
    size_t descriptor_size;
    size_t payload_size;
    size_t frame_size;

    if (!unit || !out || !out_size)
        return -1;
    descriptor_size = pe_work_unit_to_string(unit, NULL, 0u);
    if (descriptor_size == 0u || descriptor_size == SIZE_MAX)
        return -1;
    payload_size = descriptor_size + 1u;
    frame_size = pe_work_frame_size(payload_size);
    if (frame_size == 0u || capacity < frame_size)
        return -1;
    if (pe_work_unit_to_string(unit, (char *)(out + PE_WORK_PROTOCOL_HEADER_SIZE),
                               payload_size) != descriptor_size)
        return -1;
    return pe_work_frame_encode(PE_WORK_MESSAGE_UNIT,
                                out + PE_WORK_PROTOCOL_HEADER_SIZE,
                                payload_size, out, capacity, out_size);
}

int pe_work_frame_decode_work_unit(const uint8_t *frame,
                                   size_t frame_size,
                                   pe_work_unit_t *out)
{
    pe_work_message_type_t type;
    const uint8_t *payload;
    size_t payload_size;

    if (!out || pe_work_frame_decode(frame, frame_size, &type, &payload,
                                     &payload_size) != 0 ||
        type != PE_WORK_MESSAGE_UNIT || payload_size == 0u ||
        payload[payload_size - 1u] != '\0')
        return -1;
    return pe_work_unit_from_string((const char *)payload, out);
}

int pe_work_frame_encode_result(const pe_work_result_t *result,
                                uint8_t *out,
                                size_t capacity,
                                size_t *out_size)
{
    size_t payload_size;
    size_t frame_size;
    uint8_t *payload;

    if (!result || !out || !out_size ||
        result->backend <= PE_COMPUTE_AUTO ||
        result->backend >= PE_COMPUTE_COUNT ||
        (result->constraints_satisfied != 0 &&
         result->constraints_satisfied != 1) ||
        result->iteration_end < result->iteration_begin ||
        !isfinite(result->exploitability) ||
        !isfinite(result->worst_margin) ||
        !isfinite(result->mean_margin) ||
        (result->delta_size != 0u && !result->delta))
        return -1;
    if (result->delta_size > PE_WORK_PROTOCOL_MAX_PAYLOAD -
                             PE_WORK_PROTOCOL_RESULT_FIXED_SIZE)
        return -1;
    payload_size = PE_WORK_PROTOCOL_RESULT_FIXED_SIZE + result->delta_size;
    frame_size = pe_work_frame_size(payload_size);
    if (frame_size == 0u || capacity < frame_size)
        return -1;

    payload = out + PE_WORK_PROTOCOL_HEADER_SIZE;
    put_u64_be(payload, result->public_state);
    put_u64_be(payload + 8u, result->iteration_begin);
    put_u64_be(payload + 16u, result->iteration_end);
    put_u64_be(payload + 24u, result->iterations);
    put_u64_be(payload + 32u, result->infosets_trained);
    payload[40] = (uint8_t)result->backend;
    payload[41] = (uint8_t)result->constraints_satisfied;
    memset(payload + 42u, 0, 6u);
    put_double_be(payload + 48u, result->exploitability);
    put_double_be(payload + 56u, result->worst_margin);
    put_double_be(payload + 64u, result->mean_margin);
    put_u64_be(payload + 72u, (uint64_t)result->delta_size);
    if (result->delta_size != 0u)
        memcpy(payload + PE_WORK_PROTOCOL_RESULT_FIXED_SIZE, result->delta,
               result->delta_size);
    return pe_work_frame_encode(PE_WORK_MESSAGE_RESULT, payload, payload_size,
                                out, capacity, out_size);
}

int pe_work_frame_decode_result(const uint8_t *frame,
                                size_t frame_size,
                                pe_work_result_t *out)
{
    pe_work_message_type_t type;
    const uint8_t *payload;
    size_t payload_size;
    uint64_t delta_size;

    if (!out || pe_work_frame_decode(frame, frame_size, &type, &payload,
                                     &payload_size) != 0 ||
        type != PE_WORK_MESSAGE_RESULT ||
        payload_size < PE_WORK_PROTOCOL_RESULT_FIXED_SIZE)
        return -1;
    delta_size = get_u64_be(payload + 72u);
    if (delta_size > PE_WORK_PROTOCOL_MAX_PAYLOAD -
                     PE_WORK_PROTOCOL_RESULT_FIXED_SIZE ||
        delta_size != (uint64_t)(payload_size -
                                 PE_WORK_PROTOCOL_RESULT_FIXED_SIZE) ||
        payload[42] != 0u || payload[43] != 0u || payload[44] != 0u ||
        payload[45] != 0u || payload[46] != 0u || payload[47] != 0u ||
        payload[40] <= PE_COMPUTE_AUTO ||
        payload[40] >= PE_COMPUTE_COUNT ||
        (payload[41] != 0u && payload[41] != 1u))
        return -1;

    out->public_state = get_u64_be(payload);
    out->iteration_begin = get_u64_be(payload + 8u);
    out->iteration_end = get_u64_be(payload + 16u);
    out->iterations = get_u64_be(payload + 24u);
    out->infosets_trained = get_u64_be(payload + 32u);
    out->backend = (pe_compute_kind_t)payload[40];
    out->constraints_satisfied = (int)payload[41];
    out->exploitability = get_double_be(payload + 48u);
    out->worst_margin = get_double_be(payload + 56u);
    out->mean_margin = get_double_be(payload + 64u);
    out->delta = payload + PE_WORK_PROTOCOL_RESULT_FIXED_SIZE;
    out->delta_size = (size_t)delta_size;
    if (out->iteration_end < out->iteration_begin ||
        !isfinite(out->exploitability) || !isfinite(out->worst_margin) ||
        !isfinite(out->mean_margin))
        return -1;
    return 0;
}
